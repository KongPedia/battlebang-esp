# BattleBang ESP source folders

`src/` is a multi-firmware workspace. Each production firmware folder owns its source, local env example, README, and operating contract. `platformio.ini` is the shared build index that selects one folder with `build_src_filter` per PlatformIO environment.

| Folder | PlatformIO env | Local config/env | Purpose |
| --- | --- | --- | --- |
| `go2/` | `esp32dev_go2_go2_03`, `esp32dev_go2_go2_05`, etc. | `src/go2/local_secrets.h` from example / `src/go2/robots.json` | Active Go2 hit/LED ESP path: piezo AO ADC threshold + Command Center ring display. |
| `nIxo/` | `esp32dev_nixo_go2_03`, `esp32dev_nixo_go2_05`, etc. | `src/nIxo/local_secrets.h` from example | Active standalone Nixo/game blaster relay ESP path. |
| `go2_nixo/` | `esp32dev_go2_nixo_go2_03`, `esp32dev_go2_nixo_go2_05`, etc. | shell env / profiles documented in folder README | Optional one-ESP integrated fallback path. |
| `hit_target/` | `esp32dev_hit_target` | `src/hit_target/.env.hit_target` from `.env.hit_target.example` | Legacy standalone generic piezo + circular LED hit target source; kept for reference, no GitHub Actions release workflow. |
| `boss_target/` | `esp32dev_boss_target` | `src/boss_target/.env.boss_target` from `.env.boss_target.example` | Boss-stage target: 4 random target rings, HP bar, MQTT status/commands, NVS config, and OTA. |
| `turret_fleet/` | `esp32dev_turret_fleet` | `src/turret_fleet/.env.turret_fleet` from `.env.turret_fleet.example` | Generic runtime-configured turret fleet firmware. |
| `turret/` | legacy envs | `src/turret/local_secrets.h` | Legacy/reference turret firmware. |

## Independence rules

- Do not put site-specific Wi-Fi, MQTT, Command Center IP, serial ports, or passwords in source files or GitHub Actions.
- Commit only example env/config files. Real env files are ignored, for example `src/hit_target/.env.hit_target`, `src/boss_target/.env.boss_target`, and `src/turret_fleet/.env.turret_fleet`.
- A folder-specific workflow should watch only its workflow file, its source folder, its helper scripts, and `platformio.ini` when that shared build index affects the env.
- Runtime-configured firmware stores provisioned values in ESP32 NVS, so changing Wi-Fi/MQTT/gameplay tuning should not require a firmware rebuild.

## OTA release channels

Use firmware-specific stable release tags, not GitHub's repo-wide `/releases/latest/download/...` URL:

| Firmware | Stable manifest URL | Versioned tag pattern |
| --- | --- | --- |
| `hit_target` | `https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json` | `hit-target-v{version}` |
| `boss_target` | `https://github.com/KongPedia/battlebang-esp/releases/download/boss-target-latest/boss-target-manifest.json` | `boss-target-v{version}` |
| `turret_fleet` | `https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json` | `turret-fleet-v{version}` |

The stable manifest points to the firmware binary for the specific version/build inside the manifest. This keeps hit-target releases from replacing or breaking turret-fleet polling, and vice versa.
