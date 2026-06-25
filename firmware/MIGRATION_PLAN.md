# Firmware Common-Module Template Standardization Plan (2026-06-23)

## Requirements Summary

`firmware/` should become the root for standardized ESP32 firmware families, but the goal is **not** to copy `config/`, `net/`, `mqtt/`, and `ota/` implementations into every firmware folder.

The target architecture is:

- repo-private `bb_esp_*` libraries under `lib/` own reusable NVS/runtime-config, Wi-Fi, OTA, MQTT topic utilities, and safe hardware helpers.
- `firmware/<family>/` owns only firmware identity, domain config, domain MQTT commands/status, hardware profiles, and controller behavior.
- `platformio.ini` assembles each image from one firmware folder plus only the common modules that firmware needs.
- `src/` remains compatibility/legacy until each kept firmware is migrated.

User direction for this plan:

- `src/hit_target/` is unused and must not be treated as an active standardization target.
- `firmware/go2/` and `firmware/go2_nixo/` must be standardized against the template; both have now been physically migrated.
- `src/common/relay_pin_utils.h` is related shared safety code and should move into `lib/bb_esp_hw/src/bb_esp_hw/`, not stay as an ad-hoc `src/common` include.
- `src/feeder/`, `src/nIxo/`, and `src/turret/` can be treated as disposable/legacy after useful notes are preserved.
- ESP constraints are first-class: avoid unused runtime code, generic base classes, and copy/paste drift.

## Current Evidence From Repo

- `platformio.ini` already isolates firmware images with `build_src_filter` per env, for example `go2`, `go2_nixo`, `boss_target`, `heavy_blaster`, and `turret_fleet`.
- `firmware/go2/` is the active Go2 hit/LED firmware selected by the generic `esp32dev_go2` env plus NVS runtime provisioning (`scripts/go2/provision.py`).
- `firmware/go2_nixo/` is an active integrated Go2 hit/LED + Nixo relay fallback selected by generic relay-variant envs (`esp32dev_go2_nixo`, `_1ch`, `_2ch`) plus NVS runtime provisioning.
- `firmware/go2/` and `firmware/go2_nixo/` keep build-time/local-secret fallback defaults but now have the NVS-backed common runtime config bridge.
- `firmware/boss_target/`, `firmware/heavy_blaster/`, and `firmware/turret_fleet/` are already closer to the runtime-configured NVS/MQTT/OTA direction.
- `src/common/relay_pin_utils.h` is used by `firmware/go2_nixo/nixo/nixo_fire_client.cpp` and `src/nIxo/main.cpp`, proving relay GPIO attach/off logic is shared hardware-safety code.

## Decision

Adopt a **common-module + thin firmware adapter** design.

Do this:

1. Put reusable code in capability-specific `bb_esp_*` private libraries under `lib/`.
2. Keep firmware folders thin and domain-specific.
3. Use composition and free functions, not a polymorphic ESP framework.
4. Let PlatformIO select only the common `.cpp` units required by each firmware.
5. Keep MQTT domain commands local to each firmware; share only topic utilities and device-level boilerplate.

Do not do this:

- Do not copy `wifi_manager.*`, `ota_manifest.*`, `http_ota.*`, or `reboot_marker.*` into every firmware folder.
- Do not build a mega-firmware with runtime device-type switches.
- Do not hide turret/blaster/Go2 relay safety commands behind one generic command executor.

## Target Workspace Layout

```text
firmware/
  README.md
  _template/README.md
  go2/
  go2_nixo/
  boss_target/
  heavy_blaster/
  turret_fleet/

lib/
  bb_esp_core/      # header-first foundations
    library.json
    src/bb_esp_core/{app,config,mqtt}/...
  bb_esp_nvs/       # Preferences/NVS glue
    library.json
    src/bb_esp_nvs/...
  bb_esp_net/       # Wi-Fi manager
    library.json
    src/bb_esp_net/...
  bb_esp_mqtt/      # optional PubSubClient JSON/device bus glue
    library.json
    src/bb_esp_mqtt/...
  bb_esp_ota/       # manifest/download/reboot-marker core
    library.json
    src/bb_esp_ota/...
  bb_esp_hw/        # safe GPIO/relay helpers
    library.json
    src/bb_esp_hw/relay_pin_utils.h

scripts/
  go2/
  go2_nixo/
  boss_target/
  heavy_blaster/
  turret_fleet/
```

`src/` remains temporary compatibility until the migration is complete.

## Shared Library Naming Convention

Use `bb_esp_*` rather than `battlebang_esp_common`:

- `bb_esp_core` — header-first foundations: firmware identity, common runtime structs, JSON getters/apply/serialize helpers, topic utilities.
- `bb_esp_nvs` — NVS/Preferences load-save glue for common runtime fields; keeps existing firmware key names via a key map.
- `bb_esp_net` — Wi-Fi connect/retry manager.
- `bb_esp_mqtt` — optional PubSubClient JSON/device-topic glue.
- `bb_esp_ota` — OTA manifest/download/reboot-marker core.
- `bb_esp_hw` — safe GPIO/relay helpers.

Include paths mirror the library name, e.g. `#include <bb_esp_ota/ota_manifest.h>`. C++ namespace should stay explicit as `battlebang::esp::{config,net,mqtt,ota,hw}`.

This split matters because PlatformIO can compile all `.cpp` files in a selected private library. Keeping OTA, Wi-Fi, MQTT bus, and NVS in separate libraries prevents a firmware that only needs topic utilities from accidentally compiling OTA or Wi-Fi code.

## Active Firmware Scope

### Must standardize and migrate to `firmware/`

| Current folder | Target folder | Why included | Main refactor focus |
| --- | --- | --- | --- |
| `firmware/go2/` | `firmware/go2/` | Active Go2 hit/LED ESP path | Physical move complete; keep thin domain code plus common NVS/config/topic helpers, device MQTT management topics, and real bb_esp_ota OTA/auto-polling. |
| `firmware/go2_nixo/` | `firmware/go2_nixo/` | Active integrated hit/LED + Nixo relay path | Same as Go2 plus relay variant profiles and common relay pin helper. |
| `firmware/boss_target/` | `firmware/boss_target/` | Runtime-configured boss target firmware | Replace duplicated NVS/Wi-Fi/OTA/topic utility code with common modules while keeping gameplay local. |
| `firmware/heavy_blaster/` | `firmware/heavy_blaster/` | Runtime-configured heavy blaster firmware | Physical move complete; use common config/Wi-Fi/MQTT helpers and keep station/relay unlock domain code local. |
| `firmware/turret_fleet/` | `firmware/turret_fleet/` | Generic turret fleet firmware | Physical move complete; use common config/Wi-Fi/MQTT/OTA helpers and keep motion/fire safety local. |

### Shared utility migration

| Current path | Target path | Reason |
| --- | --- | --- |
| `src/common/relay_pin_utils.h` | `lib/bb_esp_hw/src/bb_esp_hw/relay_pin_utils.h` | Shared safe relay GPIO attach/off behavior used by Go2 Nixo and legacy Nixo; also useful for relay devices such as heavy blaster. |

### Retire or quarantine, not template-standardize

| Current folder/file | Policy |
| --- | --- |
| `src/hit_target/` | Unused. Preserve useful notes only if needed, remove from active docs/CI/release paths, then delete in a dedicated cleanup. |
| `src/feeder/` | Legacy/manual source. Archive notes then delete. |
| `src/nIxo/` | Legacy standalone relay path. Do not standardize unless product explicitly revives it; preserve relay notes before deletion. |
| `src/turret/` | Legacy/reference turret. Preserve useful hardware notes before deletion. |
| `src/turret_demo*.cpp`, `src/test.cpp` | Loose legacy sketches. Quarantine/archive or delete after confirming no active env uses them. |


## Current Implementation Checkpoint

As of this branch, standardization is being done in safe, reversible slices. Go2, Go2-Nixo, Boss Target, Heavy Blaster, and Turret Fleet physical migrations are complete; remaining `src` PlatformIO envs are legacy/retirement surfaces.

Completed common-module extractions:

- `lib/bb_esp_core/`: header-first identity, common runtime config structs, build-time compatibility adapter, common runtime-config JSON apply/serialize helper, fixed-buffer copy helper, JSON getters, MQTT topic normalization/join helpers, and device topic builders.
- `lib/bb_esp_hw/`: relay GPIO attach/off helper, with `src/common/relay_pin_utils.h` left as a compatibility include wrapper.
- `lib/bb_esp_net/`: reusable Wi-Fi connect/retry manager, used through thin wrappers in `boss_target`, `heavy-blaster`, and `turret_fleet`.
- `lib/bb_esp_ota/`: manifest parsing/decision helpers, shared HTTP OTA download/sha256/update transport, and reboot marker glue, used by active OTA-capable firmware.
- `lib/bb_esp_nvs/`: key-mapped `Preferences`/NVS load-save helper for common runtime fields, preserving existing persisted key names in `boss_target`, `heavy-blaster`, and `turret_fleet`, plus `standardCommonRuntimeConfigKeys()` for newly standardized firmware.

Completed active-firmware bridge work and first folder move:

- `firmware/go2/` and `firmware/go2_nixo/` now use `bb_esp_core/mqtt/topic_utils.h` for MQTT topic assembly while preserving existing topic shapes.
- `firmware/go2/` and `firmware/go2_nixo/` now create a thin `RuntimeConfig` from build-time macros through `bb_esp_core/config/build_time_config.h`; this preserves local-secret/profile behavior while preparing for NVS-backed config.
- `firmware/go2/` and `firmware/go2_nixo/` now load/save common runtime fields from NVS through `bb_esp_nvs` when provisioned, while falling back to existing build/local-secret defaults when NVS is empty.
- `firmware/go2/` and `firmware/go2_nixo/` now use `bb_esp_core/config/runtime_config_json.h` for common provision/config JSON parsing, stale `config_version` rejection, MQTT root validation, and secret-masked config output.
- `firmware/go2/` and `firmware/go2_nixo/` now expose local management commands for the NVS bridge: `show-status`, `show-config`, `provision {json}`, `config {json}`, and `clear-config`; `firmware/go2_nixo/` also accepts these over Jetson UART.
- `firmware/go2_nixo/` and legacy `src/nIxo/` use `bb_esp_hw/relay_pin_utils.h` for relay-safe boot/off behavior.
- `scripts/go2/provision.py` and `scripts/go2_nixo/provision.py` now generate standard runtime-config JSON from ignored `.env.*` files and can send the management command over serial; shared host-side parsing/building lives in `scripts/go2_runtime/`.
- `firmware/go2/` and `firmware/go2_nixo/` now run real `bb_esp_ota` OTA from serial/BT/Jetson `check-ota`, MQTT device `/ota`, and configured automatic manifest polling; Go2-Nixo defers OTA while the relay is firing when safe-state policy is enabled.
- `firmware/go2/` is physically moved and `platformio.ini`, `scripts/go2_config.py`, `scripts/go2_flash.py`, `scripts/go2/provision.py`, docs, and tests now point at the new path.
- `firmware/go2_nixo/` is physically moved and `platformio.ini`, `scripts/go2_nixo_config.py`, `scripts/go2_nixo/provision.py`, docs, variants, and tests now point at the new path.
- `firmware/boss_target/` is physically moved and `platformio.ini`, `scripts/boss_target/provision.py`, `scripts/boss_target/mqtt_command.py`, CI workflow filters, docs, and tests now point at the new path.
- `firmware/heavy_blaster/` is physically moved and `platformio.ini`, `scripts/heavy_blaster/provision.py`, `scripts/heavy_blaster/mqtt_command.py`, docs, and tests now point at the new path while product MQTT/OTA surfaces stay hyphenated as `heavy-blaster` / `heavy-blasters`.
- `firmware/turret_fleet/` is physically moved and `platformio.ini`, `scripts/turret_fleet/*`, CI workflow filters, docs, examples, profiles, pattern presets, and tests now point at the new path.

Remaining work after this standardization slice:

- No active firmware folder remains to move; the kept firmware families were migrated one firmware at a time into `firmware/<family>/`.
- Release automation/channel publishing is aligned for Go2, Go2-Nixo, Heavy Blaster, Boss Target, and Turret Fleet.
- Single-ESP bench upload/serial/MQTT validation has been completed by reflashing the same board sequentially for Go2, Go2-Nixo, Boss Target, Heavy Blaster, and Turret Fleet. This proves runtime NVS identity, stage routing, MQTT topics, and OTA plumbing; hardware-specific I/O still needs the matching fixture before declaring relays, servos, piezos, or LED strips physically validated.
- Add `bb_esp_mqtt` only when a repeated PubSubClient JSON/device bus pattern is ready; do not add it as an empty abstraction.
- Retire `hit_target`, `feeder`, `nIxo`, `turret`, and loose sketches in a dedicated cleanup after active references are removed.

## Common Module Boundaries

### NVS/runtime config

Common owns:

- common fields: schema, config version, configured flag, device ID, group, `stage_id`, location, Wi-Fi, network startup, MQTT, OTA policy;
- JSON typed getters, common provision/config application, stale `config_version` rejection, and secret masking;
- key-mapped NVS load/save for common fields;
- standard key-map helper for new firmware NVS namespaces;
- stale config-version rejection helpers.

Firmware owns:

- domain config struct;
- domain field validation;
- domain JSON/NVS load/save glue;
- safe defaults for hardware profiles.

Recommended shape:

```cpp
struct RuntimeConfig {
  battlebang::esp::config::CommonRuntimeConfig common;
  DomainConfig domain;
};
```

Use X-macro/generated straight-line field lists for repeated common fields if hand-written glue starts drifting. Do not add a runtime schema interpreter.

### Wi-Fi/network

Common owns:

- connect/retry state machine;
- network auto-start delay;
- status summaries without secrets.

Firmware owns:

- when to start networking;
- what local safe state is required before config/OTA/network transitions.

### OTA

Common owns:

- manifest parser;
- app/hardware/build/desired-build validation;
- SHA-256 validation;
- HTTP(S) download core;
- reboot marker and failed-update recovery marker;
- timeout/no-progress handling.

Firmware owns:

- `FirmwareIdentity` values;
- release URL/channel;
- safe-state predicate;
- domain status explaining OTA blocked reasons.

### MQTT

Common owns:

- `stage_id` as persisted routing metadata for Command Center inventory/stage-scoped dispatch; this does not imply firmware-side wildcard subscriptions yet;
- topic root normalization;
- topic segment validation;
- device-level topic builder: `devices/{device_type}/{device_id}/status|config|ota`;
- optional JSON publish/subscribe glue.

Firmware owns:

- collection naming;
- entity IDs;
- domain command topics;
- command parsing and safety checks;
- domain status payloads.

### Relay/pin utilities

Common owns only safe pin attach/off helpers and active-level helpers.

Firmware owns relay sequence policy:

- one-channel vs two-channel profile;
- flywheel/chain order;
- active-low/high polarity;
- prefire, inter-channel delay, cooldown, and inhibit rules.

## Plan Review Corrections

The previous single-library wording was too ambiguous. Corrections applied:

1. **Single `common` library rejected** — a monolithic private library can pull unrelated `.cpp` files into the build. Split by capability with the `bb_esp_*` naming family.
2. **Go2 transition needs a compatibility bridge** — `go2` and `go2_nixo` currently rely on build-time profiles/local secrets. During migration, keep build macros as fallback defaults until NVS provisioning is in place and tested.
3. **OTA support must be explicit** — active firmware now either composes `bb_esp_ota` for real OTA execution or is marked retired/legacy; do not leave policy-only OTA fields on an active firmware without an execution path.
4. **MQTT sharing is intentionally narrow** — share topic utilities/device-level glue only; domain command handling remains local.
5. **Test updates are required** — existing tests that assert `src/common/relay_pin_utils.h` and `#include "common/relay_pin_utils.h"` must be updated when relay utilities move to `bb_esp_hw`.
6. **Folder naming must be normalized** — new firmware folders use snake_case (`heavy_blaster`) even if legacy folders used kebab-case (`heavy-blaster`); OTA app/tag names may remain kebab-case.

## Migration Phases

### Phase 0 — Correct the plan and scope

- Document that common subsystems are assembled from capability-specific `bb_esp_*` private libraries, not copied into every firmware.
- Mark `go2` and `go2_nixo` as active standardization targets.
- Mark `hit_target` as unused/retirement, not a template source.
- Add relay pin utilities to the shared-library plan.

Acceptance:

- Docs clearly answer which folders will be refactored and which will be retired.
- Docs clearly say `config/net/ota` are common modules, not per-firmware copies.

### Phase 1 — Add common library skeleton with no behavior change

Create the first `bb_esp_*` private libraries and move/extract the safest pieces first:

1. `hardware/relay_pin_utils.h` from `src/common/relay_pin_utils.h`.
2. `mqtt/topic_utils.h` for root/segment normalization.
3. `config/json_getters.h` for typed ArduinoJson accessors.
4. `app/firmware_identity.h` as a shared identity struct.

Acceptance:

- Existing active builds still pass.
- Relay utility include paths are updated only where used.
- No OTA/NVS behavior changes yet.
- `pio run -t size` shows no material size increase for touched envs.

### Phase 2 — Add contract tests and inventory before moving folders

Add `tests/python/test_firmware_template_contract.py` with an explicit kept-firmware list:

```python
KEPT_FIRMWARE = ["go2", "go2_nixo", "boss_target", "heavy_blaster", "turret_fleet"]
RETIRED_FIRMWARE = ["hit_target", "feeder", "nIxo", "turret"]
```

Verify:

- each kept firmware has a README, identity, runtime config composition, MQTT docs, and build env;
- retired firmware is not listed as an active template target;
- common modules are referenced from `bb_esp_*` libraries, not copied into every firmware;
- `hit_target` release/docs are not used as the model for new OTA work.

Acceptance:

- The test fails if someone adds a new active firmware under `src/` without a template/migration decision.
- The test fails if `hit_target` is reintroduced as an active target without explicit policy update.

### Phase 3 — Migrate `go2` and `go2_nixo` to the template model

These are now explicit active targets. Do this as two sub-phases so the topic/relay behavior is locked before path churn.

Go2/Go2-Nixo bridge work:

- [x] use `bb_esp_core/mqtt/topic_utils.h` for Go2 hit/ring topic assembly while preserving existing topic strings;
- [x] use `bb_esp_hw/relay_pin_utils.h` for Go2 Nixo relay boot/off safety;
- [x] introduce common runtime config defaults behind existing build macros/local secrets so current robot profiles keep working;
- [x] add NVS-backed common runtime config bridge for Go2/Go2-Nixo with build/local-secret fallback when NVS is empty.
- [x] add local management commands for the Go2/Go2-Nixo NVS bridge (`show-status`, `show-config`, `provision {json}`, `config {json}`, `clear-config`) while preserving immediate reset/fire commands.
- [x] add host-side provisioning/status scripts for Go2/Go2-Nixo that generate the standard common+domain JSON shape from ignored env files; keep relay pins/polarity/channel count out of runtime config while allowing Nixo fire timing/envelope NVS tuning.
- [x] implement real `bb_esp_ota` OTA for Go2/Go2-Nixo, including device MQTT `/ota`, serial/BT/Jetson `check-ota`, auto polling, and safe-state deferral for Go2-Nixo relay firing.

For `go2` physical migration (completed):

- [x] move to `firmware/go2/`;
- [x] keep Go2 hit/LED domain behavior local;
- [x] move/update existing host-side provisioning/status tooling for the NVS-backed Wi-Fi/MQTT/device identity bridge;
- [x] decide OTA support explicitly by implementing real `bb_esp_ota` OTA/auto-polling;
- [x] migrate `scripts/go2_config.py`, `scripts/go2_flash.py`, `scripts/go2/provision.py`, PlatformIO filters, docs, and tests to `firmware/go2/`.

For `go2_nixo` physical migration (completed):

- [x] move to `firmware/go2_nixo/`;
- [x] keep integrated hit/LED + Nixo relay behavior local;
- [x] move relay variants to `firmware/go2_nixo/variants/`;
- [x] keep using `#include <bb_esp_hw/relay_pin_utils.h>`;
- [x] move/update existing host-side provisioning/status tooling for the NVS-backed Go2 and Nixo identity/network/MQTT bridge;
- [x] keep relay sequence and fire safety in `firmware/go2_nixo/nixo/`.

Acceptance:

- Representative builds pass: `esp32dev_go2`, `esp32dev_go2_nixo`, and `esp32dev_go2_nixo_2ch`; robot/stage identity is verified through NVS provisioning JSON, not build env names.
- Existing MQTT hit candidate and ring display behavior is unchanged.
- Existing Nixo fire command behavior is unchanged.
- Secrets are not stored in source files.

### Phase 4 — Migrate already runtime-configured firmware

Migrate one firmware at a time and replace duplicate internals with common modules:

1. `firmware/boss_target/` -> `firmware/boss_target/` (completed)
2. `firmware/heavy_blaster/` — already moved
3. `firmware/turret_fleet/` -> `firmware/turret_fleet/` (completed)

For each firmware:

- move folder and update PlatformIO filters/scripts/workflows;
- replace duplicated common NVS/Wi-Fi/OTA/topic helper code with the relevant `bb_esp_*` libraries;
- keep domain controller, command parsing, and safety state local;
- record binary size before/after.

Acceptance:

- Each env builds after its migration.
- Python contract tests pass.
- OTA manifests remain firmware-family-specific and cannot overwrite another firmware family.
- Size does not materially increase; if it does, document why.

### Phase 5 — Retire unused/legacy folders

After active firmware is migrated and tests prove no references remain:

1. Remove active docs/CI/release references to `hit_target`, `feeder`, `nIxo`, `turret`, and loose legacy sketches.
2. Preserve useful hardware notes in `docs/archive/` or firmware docs.
3. Remove PlatformIO envs for retired firmware.
4. Delete retired folders in a separate cleanup commit.

Acceptance:

- `platformio.ini` no longer exposes retired envs.
- Docs no longer tell users to provision/flash retired firmware.
- Contract tests distinguish kept vs retired firmware.

## Verification Plan

### Documentation and contract tests

```bash
.venv-turret-tests/bin/python -m pytest tests/python/test_firmware_template_contract.py -q
```

### Active firmware builds

Run representative builds after each migration/extraction:

```bash
./.venv-pio/bin/pio run -e esp32dev_go2
./.venv-pio/bin/pio run -e esp32dev_go2_nixo
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch
./.venv-pio/bin/pio run -e esp32dev_boss_target
./.venv-pio/bin/pio run -e esp32dev_heavy_blaster
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
```

Run all robot/profile variants in CI or before hardware rollout.

### Existing contract tests

```bash
.venv-turret-tests/bin/python -m pytest \
  tests/python/test_turret_fleet_contract.py \
  tests/python/test_boss_target_contract.py \
  tests/python/test_heavy_blaster_contract.py \
  -q
```

Add Go2/Go2 Nixo contract tests during Phase 3.

### Size gates

For each active env touched by common extraction:

```bash
./.venv-pio/bin/pio run -e <env> -t size
```

Record before/after in the PR or Jira comment.

### Hardware gates

For hardware-affecting changes:

- upload to a representative board;
- capture serial boot/status evidence;
- verify Wi-Fi/MQTT reconnect;
- verify relay outputs boot OFF and return OFF after fire/OTA/reboot failure;
- verify OTA safe-state blocking.

## Risks and Mitigations

| Risk | Mitigation |
| --- | --- |
| Common modules accidentally pull unused code into ESP images | Keep common `.cpp` units small and selected per env; record `pio run -t size` before/after. |
| A generic MQTT layer hides safety-critical command behavior | Share topic utilities and device-level glue only; keep domain command handling local. |
| Go2 migration changes active hit behavior | Add Go2 contract tests and build representative robot envs before moving folder paths. |
| Go2 Nixo relay behavior regresses | Keep relay sequence local; move only GPIO attach/off helper to common; verify 1ch and 2ch builds. |
| OTA policy drifts between firmware families | Use common manifest/validation/download core plus per-firmware identity/safe-state callbacks. |
| Legacy folders are deleted before notes are preserved | Retire in a separate cleanup commit after active docs/build refs are removed. |

## ADR

### Decision

Use `firmware/` for firmware applications and capability-specific `bb_esp_*` private libraries for reusable ESP modules. Standardize active firmware by composing common modules instead of copying NVS/Wi-Fi/MQTT/OTA folders into every firmware.

### Drivers

- Reduce human error from duplicated OTA/NVS/MQTT code.
- Preserve ESP binary-size control.
- Keep device-specific safety logic visible in each firmware.
- Make active Go2 and Go2 Nixo firmware first-class template-compliant targets.

### Alternatives Considered

- Per-firmware copied template folders: rejected because it recreates the copy/paste drift problem.
- One runtime-polymorphic mega-firmware: rejected because it risks unused code, larger binaries, and hidden safety behavior.
- Keep everything under `src/`: rejected because `src/` already mixes active, legacy, shared, and loose sketches.

### Consequences

- Initial migration needs build/script path updates.
- Common-module boundaries must be enforced by tests and review.
- `go2` and `go2_nixo` need deeper config migration than the already runtime-configured firmware.

### Follow-ups

- Add `bb_esp_*` library skeletons, starting with `bb_esp_core` and `bb_esp_hw`.
- Add template contract tests with explicit kept/retired lists.
- Migrate Go2 and Go2 Nixo first.
- Move relay pin utility to common hardware module.
- Then migrate heavy blaster and turret fleet.
- Retire `hit_target`, `feeder`, `nIxo`, `turret`, and loose sketches only after active references are removed.
