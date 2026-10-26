# BattleBang ESP Firmware Template

This folder is a template/reference for standardized ESP32 firmware families.
It is not a PlatformIO build target and it is not a folder to copy wholesale into every firmware.

The goal is **composition from capability libraries**:

- shared foundations live in repo-private PlatformIO libraries named `bb_esp_*` under `lib/`;
- each firmware folder owns only firmware identity, domain config, domain commands/status, hardware profiles, and thin adapters that are genuinely different;
- PlatformIO envs include only the capability libraries that the selected firmware actually needs.

This avoids copy/paste drift while keeping ESP images small.

## Library naming and include convention

Use this convention for shared embedded code:

```text
lib/<library_name>/
  library.json
  src/<library_name>/...
```

Rules:

- Library folders use lower_snake_case: `bb_esp_core`, `bb_esp_ota`, etc.
- Header include prefix matches the library folder: `#include <bb_esp_ota/ota_manifest.h>`.
- C++ namespace uses `battlebang::esp` with a capability subnamespace, e.g. `battlebang::esp::ota`.
- Do not name a root library `common`, `shared`, or `utils`; use the capability name.
- Split `.cpp`-heavy capabilities into separate private libraries so a firmware that only needs MQTT topic helpers does not compile OTA/Wi-Fi code.

## Shared library suite

Implemented shared-code layout:

```text
lib/
  bb_esp_core/
    library.json
    src/bb_esp_core/
      app/firmware_identity.h
      config/build_time_config.h
      config/common_runtime_config.h
      config/json_getters.h
      config/runtime_config_json.h
      config/string_buffer.h
      mqtt/topic_utils.h
      mqtt/device_topics.h
  bb_esp_nvs/
    library.json
    src/bb_esp_nvs/
      common_runtime_config_store.h
      common_runtime_config_store.cpp
  bb_esp_net/
    library.json
    src/bb_esp_net/
      wifi_manager.h
      wifi_manager.cpp
  bb_esp_ota/
    library.json
    src/bb_esp_ota/
      ota_manifest.h
      ota_manifest.cpp
      http_ota.h
      http_ota.cpp
      reboot_marker.h
      reboot_marker.cpp
  bb_esp_hw/
    library.json
    src/bb_esp_hw/
      relay_pin_utils.h
```

`bb_esp_mqtt` is not implemented. Add it only when a repeated PubSubClient JSON/device bus pattern justifies
a shared helper; do not create an empty library or framework. MQTT product adapters remain local meanwhile.

Boundary rules:

- `bb_esp_core` is header-first and safe to include broadly; NVS `.cpp` code lives in `bb_esp_nvs`.
- `bb_esp_core/config/build_time_config.h` is only a compatibility adapter for existing profile/local-secret firmware while it migrates to NVS.
- `bb_esp_core/config/runtime_config_json.h` owns common runtime-config JSON apply/serialize policy, including required `config_version`, stale-version rejection, topic-root validation, and secret masking.
- `bb_esp_core/config/string_buffer.h` provides fixed-buffer copy/truncation status for ESP clients that still use stack/static char arrays.
- `.cpp` modules that depend on `WiFi`, `PubSubClient`, `Preferences`, `HTTPClient`, `Update`, or `WiFiClientSecure` belong in separate capability libraries such as `bb_esp_net`, `bb_esp_nvs`, and `bb_esp_ota`; a future `bb_esp_mqtt` must meet the repeated-use rule above.
- Do not create a generic all-device base class.
- Do not add virtual-heavy command frameworks on ESP.
- Prefer plain structs, free functions, callbacks, and compile-time composition.
- Domain command handling stays in the firmware folder because safety rules differ by device.

## Required firmware folder shape

A standardized firmware folder should be thin:

```text
firmware/<firmware_family>/
  README.md
  config.json                              # committed defaults/schema reference, no secrets
  .env.<firmware_family>.example           # host-side provision/MQTT helper defaults
  main.cpp
  app/
    firmware_info.h                        # fills common FirmwareIdentity
    version_autogen.h
  config/
    domain_config.h                        # domain-only fields and validation
    domain_config.cpp
    runtime_config.h                       # composes common + domain config
    runtime_config.cpp                     # thin glue; common fields delegated to bb_esp_* libs
  mqtt/
    domain_topics.h                        # only firmware-specific collections/commands
    domain_bus.h
    domain_bus.cpp
  <domain>/
    <domain>_controller.h
    <domain>_controller.cpp
  variants/                                # optional hardware/relay profiles
  examples/
    provision.<firmware_family>.example.json

scripts/<firmware_family>/
  provision.py
  mqtt_command.py
```

Do **not** duplicate these full implementations under every firmware:

```text
wifi_manager.*
ota_manifest.*
http_ota_core.*
reboot_marker.*
topic_utils.*
common_runtime_config_store.*
relay_pin_utils.*
```

Those belong in the relevant `bb_esp_*` library and are assembled into firmware builds as needed.

A firmware may mark OTA as unsupported only if its README and MQTT/serial status make that explicit. Unsupported firmware should still surface stored OTA policy fields as policy-only data so operators do not mistake provisioned policy for an active OTA engine.
Otherwise it should use `bb_esp_ota` plus a firmware-specific `FirmwareIdentity` and safe-state callback.

## Runtime config composition

Every kept firmware should compose common and domain config:

```cpp
struct RuntimeConfig {
  battlebang::esp::config::CommonRuntimeConfig common;
  DomainConfig domain;
};
```

Common fields owned by `CommonRuntimeConfig`:

- `schema`
- `config_version`
- `configured`
- `device_id`
- `group`
- `stage_id` — Command Center routing scope, e.g. `boss_stage_v1`; persisted in NVS and reported in status.
- `location`
- `wifi.ssid`, `wifi.password`
- `network.auto_start`, `network.start_delay_ms`
- `mqtt.host`, `mqtt.port`, `mqtt.username`, `mqtt.password`, `mqtt.root`
- `ota.command_center_controlled`
- `ota.auto_check_enabled`
- `ota.channel`
- `ota.desired_build`
- `ota.public_manifest_url`
- `ota.local_mirror_url`
- `ota.check_interval_s`
- `ota.apply_only_in_safe_state`

Firmware folders add only domain fields, for example:

- Go2 hit/LED: `robot_id`, hit thresholds, HP bar LED settings, offline hit queue policy.
- Go2 Nixo: Go2 hit/LED fields plus `nixo_id`, validated fire timing and cooldown; relay pins/polarity/channel count remain build-profile data.
- Boss target: target count, HP/ring LED settings, piezo pins/thresholds, gameplay settings.
- Heavy blaster: station/slot config, relay unlock policy, matrix settings.
- Turret fleet: turret identity, pose/calibration/motion/fire safety envelope.

Rules:

- New standardized firmware should use `bb_esp_nvs::standardCommonRuntimeConfigKeys()` for common-field NVS keys unless it must preserve legacy key names.
- New standardized firmware should use `bb_esp_core/config/runtime_config_json.h` for common-field JSON parsing/output, then add only firmware-domain fields in its own `runtime_config.cpp`.
- `config_version` is mandatory for provision/config updates on newly standardized firmware.
- Reject stale versions unless a controlled migration tool explicitly clears/downgrades config.
- Persist secrets in NVS only when provisioned locally; never print secret values in status.
- `toJson(..., includeSecrets=false)` masks Wi-Fi/MQTT passwords.
- Store identity, network, MQTT, OTA policy, hardware tuning, and safety limits.
- Do not store ordinary live match progress by default.

Minimum local management surface for newly standardized firmware with a supported text management channel:

```text
show-status
show-config
provision {json}
config {json}
clear-config
```

These are management-channel commands, not a universal USB console. Production USB-framed Go2/Nixo reserves
USB for binary packets: use the supported management channel in its
[family guide](../go2_nixo_framed_packet_uart/README.md), never ASCII on the application stream.

`provision`/`config` payloads should share the common field shape above and include a positive `config_version`. Firmware may add domain fields such as `robot_id`, `hit_topic_prefix`, `nixo_id`, or relay variant names, but it must not expose unsafe live hardware actions as generic common config.

Host-side `scripts/<firmware_family>/provision.py` helpers should build this JSON shape from ignored `.env.<firmware_family>` files and delegate common field handling to shared script helpers where possible. They should not duplicate firmware parsing policy in every folder.

Go2/Nixo relay pins, polarity, and channel count remain build/variant data, not generic runtime config.
Its existing NVS-backed `nixo.prefire_delay_ms` and `nixo.relay_delay1_ms` are supported runtime tuning,
each validated at <= 60000 ms in `firmware/go2_nixo/config/runtime_config.cpp`. Preserve those bounds and sequencing.
Turret Fleet separately supports validated NVS relay profiles and per-channel polarity; do not remove that
product-specific contract.

## MQTT composition

MQTT is only partially common because topics and commands differ by firmware.

Common module ownership:

- `bb_esp_core/mqtt/topic_utils.h`: root normalization, segment validation, topic join helpers.
- `bb_esp_core/mqtt/device_topics.h`: device-level `status`, `config`, and `ota` topic construction.
- `bb_esp_mqtt`: proposed only, not available; extract PubSubClient JSON glue only after repeated product use justifies it.

Common topic model:

```text
{root}/devices/{device_type}/{device_id}/status
{root}/devices/{device_type}/{device_id}/config
{root}/devices/{device_type}/{device_id}/ota
{root}/{collection}/all/ota
```

Firmware domain code owns:

```text
{root}/{collection}/{entity_id}/status
{root}/{collection}/{entity_id}/config
{root}/{collection}/{entity_id}/command
{root}/{collection}/{entity_id}/ota
```

Rules:

- Common topic utilities normalize `mqtt.root`, reject empty path segments, and validate topic segment characters.
- A future shared bus may handle repeated device-level status/config/OTA glue; current product adapters own it.
- Domain command parsing and safety checks stay firmware-specific.
- No generic command executor should be shared across turret, target, blaster, and Go2 relay firmware.

## OTA composition

Each OTA-capable firmware defines only identity and safe-state policy:

```cpp
constexpr battlebang::esp::app::FirmwareIdentity kFirmwareIdentity{...};
bool canApplyOtaNow(const RuntimeConfig& config, const DomainState& state);
```

`bb_esp_ota` owns:

- manifest JSON parsing;
- `type == "firmware"` validation;
- required `app`, `hardware`, `version`, `build`, `url`, `sha256` checks;
- app/hardware/build/desired-build validation;
- SHA-256 verification;
- reboot marker / failed OTA recovery marker;
- HTTP(S) download core, timeout, and recovery status.

Firmware owns:

- app/hardware identity values;
- release channel URL;
- safe-state predicate;
- user-visible status fields that explain why OTA is blocked.

Production/default OTA should use CA-pinned HTTPS or signed artifacts. Local HTTP is acceptable only for explicit trusted bench/local mirror workflows.

## Relay and pin utilities

Relay GPIO attach/off behavior is shared hardware safety code and should live in:

```text
lib/bb_esp_hw/src/bb_esp_hw/relay_pin_utils.h
```

Relay profiles remain firmware-specific data, for example under:

```text
firmware/go2_nixo/variants/relay_1ch/config.json
firmware/go2_nixo/variants/relay_2ch/config.json
```

This keeps common code small while preserving product-specific relay order, polarity, and safety timing.

## PlatformIO integration checklist

For a firmware family:

1. Add or update `[env:esp32dev_<firmware_family>]` in `platformio.ini`.
2. Add only the needed private libs to `lib_deps` or include them so PlatformIO's LDF selects them.
3. Keep implemented `.cpp`-heavy capabilities split (`bb_esp_net`, `bb_esp_ota`, `bb_esp_nvs`) so unused subsystems are not compiled; do not depend on the unimplemented `bb_esp_mqtt`.
4. Do not include `firmware/_template/**` in any build.
5. Use an OTA-capable partition table only for firmware that actually supports OTA.
6. Add only external libraries the firmware uses.
7. Capture `./.venv-pio/bin/pio run -e <selected_env> -t size` for each affected env before/after shared-library extraction.

Current retention policy:

- Active families already live under `firmware/`; put new active applications there.
- Retain `src/` as legacy/reference/compatibility, including existing envs and hardware/calibration notes.
- Do not repeat completed moves or delete legacy sources/defaults as template cleanup; dependency review and separately scoped cleanup are required.

## Script and workflow checklist

Add or migrate:

- `scripts/<firmware_family>/provision.py`
- `scripts/<firmware_family>/mqtt_command.py`
- `.env.<firmware_family>.example`
- a family/variant matrix row and matching path filters in `.github/workflows/firmware-ota.yml`

Rules:

- Real `.env` files stay gitignored.
- Provision helpers emit common config plus domain config.
- MQTT helpers support at minimum `status`, `config`, `ota`, and `all-ota`; domain commands are firmware-specific.
- Use the unified `firmware-ota.yml` publishing matrix, not separate per-family publishing workflows. Each family/variant gets its own stable tag and versioned artifacts, not repo-wide latest URLs.

## Active families and legacy policy

Current active families (folder moves are complete, not pending work):

- `firmware/go2/` — already moved
- `firmware/go2_nixo/` — already moved
- `firmware/boss_target/` — already moved
- `firmware/heavy_blaster/` — already moved
- `firmware/turret_fleet/` — already moved
- `firmware/station/` — already moved
- `firmware/go2_nixo_framed_packet_uart/` — active framed UART/USB composition of Go2/Nixo

Retained legacy/reference/compatibility paths, not sources for new active firmware:

- `src/feeder/`
- `src/hit_target/` — unused; do not use as template source for new work
- `src/nIxo/` — standalone relay path replaced by Go2 Nixo direction unless explicitly revived
- `src/turret/`
- loose legacy sketches such as `src/turret_demo*.cpp` and `src/test.cpp`

Keep these paths and their hardware notes unless a separately scoped cleanup establishes that no active or
compatibility env depends on them. The root default still selects a legacy turret env; choose an explicit active
env rather than treating that default as a migration instruction.

## Composition decision record

The former migration-plan path is retained for existing links/tests; it records current boundaries, not an open migration:

```text
firmware/MIGRATION_PLAN.md
```
