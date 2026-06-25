# BattleBang ESP source folders

`src/` is the current compatibility workspace for firmware sources that have not yet moved. New standardized firmware should live under `firmware/` and use common modules from the `bb_esp_*` private libraries under `lib/` instead of copying NVS/Wi-Fi/MQTT/OTA code into each firmware folder.

The detailed migration plan is `firmware/MIGRATION_PLAN.md`; the composition template is `firmware/_template/README.md`.

| Folder | PlatformIO env | Local config/env | Current policy |
| --- | --- | --- | --- |
| `../firmware/go2/` | `esp32dev_go2` | `firmware/go2/.env.go2` from example; identity/tuning stored in NVS | Active. Generic image; `go2_01`/`go2_02`/`go2_03` are runtime `robot_id` values, not build envs. |
| `../firmware/go2_nixo/` | `esp32dev_go2_nixo`, `esp32dev_go2_nixo_1ch`, `esp32dev_go2_nixo_2ch` | `firmware/go2_nixo/.env.go2_nixo` from example; identity/tuning stored in NVS | Active integrated hit/LED + Nixo relay fallback. Relay helper uses common library; relay pins/polarity/channel count remain build variants. |
| `../firmware/boss_target/` | `esp32dev_boss_target` | `firmware/boss_target/.env.boss_target` from `.env.boss_target.example` | Active. Already moved to `firmware/boss_target/`; common helpers stay in `bb_esp_*` libraries and gameplay remains local. |
| `../firmware/heavy_blaster/` | `esp32dev_heavy_blaster` | `firmware/heavy_blaster/.env.heavy-blaster` from `.env.heavy-blaster.example` | Active. Already moved to `firmware/heavy_blaster/`; keep station/relay unlock domain code local. |
| `../firmware/turret_fleet/` | `esp32dev_turret_fleet` | `firmware/turret_fleet/.env.turret_fleet` from `.env.turret_fleet.example` | Active. Already moved to `firmware/turret_fleet/`; keep motion/fire safety local. |
| `hit_target/` | `esp32dev_hit_target` | `src/hit_target/.env.hit_target` from `.env.hit_target.example` | Unused/retire. Do not standardize or use as template source. Preserve notes only if needed. |
| `feeder/` | none / manual source | n/a | Legacy/disposable manual feeder sketch source. |
| `nIxo/` | legacy envs | `src/nIxo/local_secrets.h` from example | Legacy/disposable standalone Nixo/game blaster relay ESP path. Preserve useful relay notes only if needed. |
| `turret/` | legacy envs | `src/turret/local_secrets.h` | Legacy/disposable reference turret firmware. |


## Current compatibility status

Some active firmware still builds from `src/` while common modules are introduced under `lib/`. Treat this as an intermediate state, not the final layout:

- `go2`: physically moved to `firmware/go2/`; common MQTT topic helper, build-time `CommonRuntimeConfig` adapter, common runtime-config JSON helper, NVS-backed runtime-config bridge, device-level MQTT management topics, local/host provisioning helpers, real `bb_esp_ota` OTA/auto-polling, and NVS-tunable hit sensor/display values are adopted.
- `go2_nixo`: physically moved to `firmware/go2_nixo/`; common MQTT topic helper, build-time `CommonRuntimeConfig` adapter, common runtime-config JSON helper, NVS-backed runtime-config bridge, device-level MQTT management topics, local/host provisioning helpers, relay-safe OTA deferral, real `bb_esp_ota` OTA/auto-polling, and NVS-tunable hit sensor/display plus Nixo fire timing values are adopted.
- `go2_nixo`: common relay pin helper adopted.
- `boss_target`: physically moved to `firmware/boss_target/`; common NVS/Wi-Fi/topic/OTA manifest helpers are adopted through thin firmware adapters.
- `heavy_blaster`: physically moved to `firmware/heavy_blaster/`; common NVS/Wi-Fi/topic helpers and real `bb_esp_ota` MQTT/direct/auto-poll OTA execution are adopted while station/relay unlock behavior remains local.
- `turret_fleet`: physically moved to `firmware/turret_fleet/`; common NVS/Wi-Fi/topic/OTA helpers are adopted where applicable.

## Independence and common-module rules

- Do not put site-specific Wi-Fi, MQTT, Command Center IP, serial ports, or passwords in source files or GitHub Actions.
- Commit only example env/config files. Real env files are ignored, for example `.env.*` files and `local_secrets.h` files.
- The unified firmware OTA workflow should watch only its workflow file, active firmware folders, build-affecting helper scripts, required common modules, and `platformio.ini` when that shared build index affects the env.
- Runtime-configured firmware stores provisioned values in ESP32 NVS, so changing Wi-Fi/MQTT/gameplay tuning should not require a firmware rebuild.
- Do not duplicate common NVS/Wi-Fi/OTA/topic utility implementations inside every firmware folder. Put shared code under capability-specific `bb_esp_*` private libraries and include only what each env needs.
- MQTT domain commands are allowed to differ by firmware; keep command parsing and safety checks inside the firmware domain layer.


## Shared library naming convention

Use `bb_esp_*` for repo-private PlatformIO libraries:

- `bb_esp_core`: header-first foundations such as firmware identity, common runtime structs, JSON/topic helpers.
- `bb_esp_nvs`: NVS/Preferences load-save glue for common runtime fields, with firmware-specific key maps for backward compatibility.
- `bb_esp_net`: Wi-Fi connect/retry manager.
- `bb_esp_mqtt`: optional PubSubClient JSON/device-topic glue.
- `bb_esp_ota`: OTA manifest/download/reboot-marker core.
- `bb_esp_hw`: safe GPIO/relay helpers.

Keep `.cpp`-heavy capabilities split so including a topic helper does not compile OTA/Wi-Fi code. Include paths should mirror the library name, for example `#include <bb_esp_hw/relay_pin_utils.h>`.

## Active migration scope

Refactor these to the template/common-module model:

- `firmware/go2/` — already moved
- `firmware/go2_nixo/` — already moved
- `firmware/boss_target/` — already moved
- `firmware/heavy_blaster/` — already moved
- `firmware/turret_fleet/` — already moved

Move shared relay safety helper:

- `src/common/relay_pin_utils.h` -> `lib/bb_esp_hw/src/bb_esp_hw/relay_pin_utils.h`

## Legacy / retirement policy

Per current product direction, `src/feeder/`, `src/hit_target/`, `src/nIxo/`, `src/turret/`, and loose legacy sketches such as `src/turret_demo*.cpp` / `src/test.cpp` may be retired after useful hardware notes are preserved and active build/docs references are removed.

Do not delete them in the same change that introduces the template/common-library plan. Retire them in a dedicated cleanup after verification.

## OTA release channels

Use firmware-specific stable release tags, not GitHub's repo-wide `/releases/latest/download/...` URL.

Current active channels documented here:

| Firmware | Stable manifest URL | Versioned tag pattern |
| --- | --- | --- |
| `boss_target` | `https://github.com/KongPedia/battlebang-esp/releases/download/boss-target-latest/boss-target-manifest.json` | `boss-target-v{version}` |
| `turret_fleet` | `https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json` | `turret-fleet-v{version}` |

Active OTA channels now include Go2, Go2-Nixo, Boss Target, Heavy Blaster, and Turret Fleet. `hit_target` is unused and should not drive future OTA policy.
