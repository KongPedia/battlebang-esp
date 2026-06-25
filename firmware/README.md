# BattleBang ESP firmware workspace

`firmware/` is the recommended root for standardized ESP32 firmware applications going forward.

Why `firmware/` instead of other common monorepo names:

- `services/` usually means long-running backend services. ESP32 binaries are deployed firmware images, not server services.
- `packages/` usually means publishable libraries/packages. The deployable folders here are hardware firmware applications.
- `apps/` is acceptable in a larger product monorepo, but this repo is ESP-focused; `firmware/` is more explicit and avoids web/mobile ambiguity.
- `src/` is PlatformIO's conventional single-project source directory, but in this repo it became a mixed multi-firmware root with active, legacy, shared, and loose sketch files.

The target split is:

```text
firmware/
  README.md
  _template/
  go2/
  go2_nixo/
  boss_target/
  heavy_blaster/
  turret_fleet/
  <future_firmware>/

lib/
  bb_esp_core/      # header-first foundations: identity, common structs, JSON config/topic helpers
  bb_esp_nvs/       # NVS/Preferences load-save glue for common runtime fields
  bb_esp_net/       # Wi-Fi connect/retry manager
  bb_esp_mqtt/      # optional PubSubClient JSON/device-topic glue
  bb_esp_ota/       # OTA manifest/download/reboot-marker core
  bb_esp_hw/        # GPIO/relay safety helpers

src/
  <temporary compatibility and legacy sources until migrated>

scripts/
  <host-side provisioning, MQTT, build/upload, release helpers>
```

Migration should be incremental. Do not move all existing firmware at once unless the corresponding PlatformIO envs, helper scripts, CI path filters, release workflows, and tests are updated in the same change.

Agent-facing rules live in `firmware/AGENTS.md`, `lib/AGENTS.md`, `.github/workflows/AGENTS.md`, and `tests/AGENTS.md`. README files remain the human/operator explanation layer; AGENTS files are the execution contract for coding agents.


## Current branch status

This branch is standardizing through common libraries first, then folder moves. `go2`, `go2_nixo`, `boss_target`, `heavy_blaster`, and `turret_fleet` have been physically moved to `firmware/`; active standardized firmware now lives under `firmware/` while legacy `src/` folders remain available for retirement work:

- `bb_esp_core`, `bb_esp_hw`, `bb_esp_net`, `bb_esp_ota`, and `bb_esp_nvs` exist under `lib/`.
- `boss_target`, `heavy_blaster`, and `turret_fleet` have started using common Wi-Fi/NVS/OTA/topic helpers through thin firmware adapters.
- `go2` and `go2_nixo` use common MQTT topic helpers, build-time `CommonRuntimeConfig` adapters, common JSON config apply/serialize helpers, and an NVS-backed runtime-config bridge that falls back to existing robot-profile/local-secret defaults when NVS is empty; `go2_nixo` uses the common relay pin helper.
- `go2` and `go2_nixo` now expose serial/BT line commands for the NVS bridge: `show-status`, `show-config`, `provision {json}`, `config {json}`, and `clear-config`. `go2_nixo` also accepts those commands from the Jetson UART.
- `scripts/go2/provision.py` and `scripts/go2_nixo/provision.py` generate the same standard runtime-config JSON shape from ignored `.env.*` files and send it over the firmware management command line; relay pin/timing values stay build/variant-time data.
- `go2` and `go2_nixo` now use real `bb_esp_ota` OTA execution from serial/BT/Jetson `check-ota`, MQTT device `/ota`, and configured automatic manifest polling; stored OTA policy is active policy, not status-only data.
- Go2 physical migration is complete: `platformio.ini`, `scripts/go2_config.py`, `scripts/go2_flash.py`, `scripts/go2/provision.py`, docs, and tests now point at `firmware/go2/`.
- Go2-Nixo physical migration is complete: `platformio.ini`, `scripts/go2_nixo_config.py`, `scripts/go2_nixo/provision.py`, docs, variants, and tests now point at `firmware/go2_nixo/`.
- Boss Target physical migration is complete: `platformio.ini`, `scripts/boss_target/provision.py`, `scripts/boss_target/mqtt_command.py`, CI workflow path filters, docs, and tests now point at `firmware/boss_target/`.
- Heavy Blaster physical migration is complete: `platformio.ini`, `scripts/heavy_blaster/provision.py`, `scripts/heavy_blaster/mqtt_command.py`, docs, and tests now point at `firmware/heavy_blaster/`.
- Turret Fleet physical migration is complete: `platformio.ini`, `scripts/turret_fleet/*`, CI workflow filters, docs, examples, profiles, and tests now point at `firmware/turret_fleet/`.

## Naming convention

Use the `bb_esp_*` prefix for repo-private PlatformIO libraries:

- folder names: lower_snake_case, e.g. `lib/bb_esp_ota/`;
- include prefix mirrors the library, e.g. `#include <bb_esp_hw/relay_pin_utils.h>`;
- C++ namespace stays explicit: `battlebang::esp::{config,net,mqtt,ota,hw}`;
- avoid vague package names such as `common`, `shared`, or `utils` at the library root;
- keep capability libraries split so using a topic helper does not pull OTA/Wi-Fi `.cpp` files into the image.

## Standardization rule

The template is **not** a request to duplicate `config/`, `net/`, `mqtt/`, and `ota/` implementations in every firmware folder.

The standard is:

- shared NVS/runtime-config, Wi-Fi, OTA, MQTT topic utilities, and relay/pin helpers live in the `bb_esp_*` private libraries under `lib/`;
- common runtime config includes `stage_id` so Command Center can inventory devices by arena/stage and dispatch only to the devices in that stage;
- each firmware folder owns only identity, domain config, domain MQTT commands/status, hardware profiles, and controller behavior;
- PlatformIO includes only the capability libraries needed by that firmware;
- domain safety logic stays local to the firmware folder.

See `firmware/_template/README.md` for the exact composition contract.

## Active migration targets

These firmware folders should be refactored to, or kept on, the template/common-module model:

| Current folder | Target folder | Notes |
| --- | --- | --- |
| `firmware/go2/` | `firmware/go2/` | Active Go2 hit/LED ESP path; physical move complete. Uses common runtime config/NVS/topic helpers, device MQTT management topics, host provisioning script, and real `bb_esp_ota` OTA/auto-polling. |
| `firmware/go2_nixo/` | `firmware/go2_nixo/` | Active integrated Go2 hit/LED + Nixo relay path; physical move complete. Relay variants remain firmware-specific data and relay GPIO helper uses `bb_esp_hw`. |
| `firmware/boss_target/` | `firmware/boss_target/` | Active boss-stage target path; physical move complete. Uses common NVS/Wi-Fi/OTA/topic helpers through thin firmware adapters while gameplay remains local. |
| `firmware/heavy_blaster/` | `firmware/heavy_blaster/` | Active heavy blaster station path; physical move complete. Product MQTT topics/OTA tags keep `heavy-blaster` while folder/script paths use snake_case. |
| `firmware/turret_fleet/` | `firmware/turret_fleet/` | Active generic turret fleet path; physical move complete. Motion/fire safety remains local while common NVS/Wi-Fi/OTA/topic helpers are composed from `bb_esp_*`. |

Shared utility migration:

| Current path | Target path |
| --- | --- |
| `src/common/relay_pin_utils.h` | `lib/bb_esp_hw/src/bb_esp_hw/relay_pin_utils.h` |

## Legacy policy for `src/`

`src/` remains the current compatibility root until migration work is done. Per current direction, these folders/files are not active template targets and may be retired after useful hardware notes are preserved and active references are removed:

- `src/feeder/`
- `src/hit_target/` — unused; do not use as the source for new template work
- `src/nIxo/` — standalone relay path; preserve relay notes only if needed
- `src/turret/`
- loose legacy sketches such as `src/turret_demo*.cpp` and `src/test.cpp`

Do not delete legacy folders in the same step as introducing the new root. First document, then migrate kept firmware, then remove obsolete build/docs references, then delete in a dedicated cleanup.

## OTA release channels

Use firmware-specific stable release tags, not GitHub's repo-wide `/releases/latest/download/...` URL.

Active standardized OTA channels should be owned by the corresponding firmware family. Existing known channels:

| Firmware | Stable manifest URL | Versioned tag pattern |
| --- | --- | --- |
| `go2` | `https://github.com/KongPedia/battlebang-esp/releases/download/go2-latest/go2-manifest.json` | `go2-v{version}` |
| `go2_nixo` relay 1ch | `https://github.com/KongPedia/battlebang-esp/releases/download/go2-nixo-1ch-latest/go2-nixo-1ch-manifest.json` | `go2-nixo-1ch-v{version}` |
| `go2_nixo` relay 2ch | `https://github.com/KongPedia/battlebang-esp/releases/download/go2-nixo-2ch-latest/go2-nixo-2ch-manifest.json` | `go2-nixo-2ch-v{version}` |
| `boss_target` | `https://github.com/KongPedia/battlebang-esp/releases/download/boss-target-latest/boss-target-manifest.json` | `boss-target-v{version}` |
| `heavy_blaster` | `https://github.com/KongPedia/battlebang-esp/releases/download/heavy-blaster-latest/heavy-blaster-manifest.json` | `heavy-blaster-v{version}` |
| `turret_fleet` | `https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json` | `turret-fleet-v{version}` |

Go2, Go2-Nixo, Boss Target, Heavy Blaster, and Turret Fleet all use firmware-specific OTA manifest channels. `hit_target` is unused and should not be used as the reference OTA channel for new work.
The unified `Firmware OTA Releases` workflow covers all active firmware families and publishes these channels without using repo-wide `latest`. On `main` pushes it builds only the affected firmware matrix entries; firmware-specific folders select one family, `firmware/go2_nixo/**` selects both relay variants, and common `bb_esp_*` library changes select only firmware that actually depends on that library. Manual dispatch builds the full matrix for branch smoke/release validation.
Host-only provisioning/MQTT helper changes under `scripts/boss_target/`, `scripts/go2/`, `scripts/go2_runtime/`, `scripts/go2_nixo/`, `scripts/heavy_blaster/`, or `scripts/turret_fleet/` do not trigger OTA release builds. Build-affecting extra scripts (`scripts/go2_config.py`, `scripts/go2_nixo_config.py`) and the shared manifest generator still trigger the relevant release job.

## Bench hardware validation IDs

Bench validation can be done with a single ESP32 by reflashing it sequentially:
flash one firmware family, provision its runtime NVS identity, run serial/MQTT
smoke checks, then flash the next firmware family. The board can only run the
last flashed firmware; this proves the shared NVS/MQTT/OTA plumbing and topic
routing, not simultaneous multi-device gameplay. Hardware-specific relays,
servos, piezos, and LED strips still need a matching fixture before declaring
physical I/O complete.

The bench/dev IDs are runtime NVS identities, not PlatformIO environment names.
Do not create `esp32dev_go2_01` / `esp32dev_go2_02` style build environments
for per-device IDs; use one reusable firmware image and change IDs through
provisioning/MQTT config.

Use the ignored `.env.<firmware>` files as the canonical local input for Wi-Fi,
MQTT, identity, `stage_id`, OTA policy, and runtime tuning. Active standardized
firmware must not require `local_secrets.h`; Go2/Go2-Nixo keep it only as an
explicit legacy/factory opt-in so stale build-time secrets cannot silently
override NVS provisioning.

Current convention:

| Firmware | Runtime ID convention | Notes |
| --- | --- | --- |
| `go2` | `device_id=go2_dev_01`, `robot_id=go2_dev_01` | Flash `esp32dev_go2`, then provision with `scripts/go2/provision.py --robot-id go2_dev_01`. |
| `go2_nixo` | `device_id=go2_dev_01`, `robot_id=go2_dev_01`, `nixo_id=nixo_go2_dev_01` | Flash the matching relay variant (`esp32dev_go2_nixo`, `_1ch`, or `_2ch`), then provision with `scripts/go2_nixo/provision.py --robot-id go2_dev_01 --nixo-id nixo_go2_dev_01`. |
| `boss_target` | `device_id=boss_target_dev_01`, `boss_id=boss_target_dev_01`, `target_id=boss_target_dev_01` | Current bench board was validated with this ID on `/dev/cu.usbserial-110`. |
| `heavy_blaster` | `device_id=heavy_blaster_dev_01`, `blaster_id=heavy_blaster_dev_01` | Flash `esp32dev_heavy_blaster`, provision from `firmware/heavy_blaster/.env.heavy-blaster`, then smoke `battlebang/heavy-blasters/heavy_blaster_dev_01/command` with safe `status`/`reset` commands only. |
| `turret_fleet` | `device_id=turret_dev_01`, `turret_id=turret_dev_01` | Reuse a physical pose/motion profile with `scripts/turret_fleet/provision.py build-config turret_dev_01 --profile-id turret_1 ...` (or the correct physical profile id for the connected turret). |

For stage-scoped Command Center routing, set `stage_id` during provisioning
(bench default: `dev_stage_01`). The device should then publish status on both
its device management topic and firmware-domain topic, for example:

```text
battlebang/devices/go2/go2_dev_01/status
battlebang/hit/go2_dev_01/events
battlebang/devices/heavy_blaster/heavy_blaster_dev_01/status
battlebang/heavy-blasters/heavy_blaster_dev_01/status
battlebang/devices/turret/turret_dev_01/status
battlebang/turrets/turret_dev_01/status
```
