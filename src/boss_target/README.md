# Boss Target firmware

`src/boss_target/` is the boss-stage ESP32 firmware for **4 target LED rings + 4 piezo DO inputs + 1 HP bar**. It is separate from `src/hit_target/` because this device owns the boss HP bar and publishes boss-level status for Command Center orchestration.

## Operating model

- Boot/reset: internal HP is full, but **all LEDs stay off** and hits are ignored.
- MQTT/serial `start`: HP bar turns on at full green and one random target ring becomes active.
- During ACTIVE: only the currently active ring accepts damage; wrong-ring hits are reported but do not reduce HP.
- After a correct hit: `damage_per_hit` is subtracted, status is published, and another random target is selected from `0..target.count-1`.
- HP reaches 0: state becomes `DEFEATED`/`dead`, status keeps publishing periodically with `hp_remaining=0` and `destroyed=true` so Command Center can send turret `dead`/inactive commands.
- `reset`: returns to READY with full internal HP and LEDs off. It does not start the game.

MVP commands are intentionally small:

```json
{"command":"start"}
{"command":"reset"}
{"command":"status"}
```

`{"command":"simulate_hit"}` exists only for bench debug and is rejected unless `debug_allow_simulate_hit=true` is provisioned. There is no production `disable`, `enable`, `pause`, or `stop` command in this firmware; Command Center can simply stop issuing `start` or issue `reset` after a round.

## Hardware profile

Current board profile is compiled as `kMaxTargets = 4`, while runtime config owns `target.count` (default 4):

| Function | GPIO / value |
| --- | --- |
| Ring 1 data | GPIO23 |
| Ring 2 data | GPIO21 |
| Ring 3 data | GPIO18 |
| Ring 4 data | GPIO17 |
| Piezo DO 1 | GPIO27 |
| Piezo DO 2 | GPIO32 |
| Piezo DO 3 | GPIO33 |
| Piezo DO 4 | GPIO25 |
| HP bar data | GPIO26 |
| LED type/order | WS2811 / RGB |
| Ring LEDs | 40 per target |
| HP bar LEDs | 92 |

Game logic iterates over `target.count`, not hard-coded `4`; if a future 6-target board appears, add a new hardware profile/env with a larger compiled capacity and pin map, then keep the random/HP/status logic mostly unchanged.

## Runtime config / NVS

NVS stores runtime configuration only. In this repo there are three different
config-looking artifacts:

- `config.json`: committed factory-default/schema reference for the current
  firmware. It is not automatically loaded by the ESP at runtime.
- `.env.boss_target.example`: committed local provisioning template; copy it to
  ignored `.env.boss_target` for real Wi-Fi/MQTT/serial values.
- `examples/*.json`: committed example payloads for serial `provision {json}` /
  `config {json}` and MQTT commands/config.

See [`docs/runtime-config.md`](docs/runtime-config.md) for the full NVS field
table, examples, validation ranges, and what is intentionally not persisted.

Persisted NVS config includes:

- identity/placement (`boss_id`, `display_name`, `group`, `location`)
- Wi-Fi and MQTT connection settings
- gameplay values (`hp_max`, `damage_per_hit`, target duration, cooldown)
- target count and LED counts/colors
- OTA policy
- debug simulation flag for bench testing

`hardware_profile` is accepted in provision/config payloads and validated
against the compiled board profile, but it is not a live pin-remapping setting.
GPIO pins are compiled into the firmware.

NVS intentionally does **not** store match progress (`hp_remaining`, `active_target_index`, `ACTIVE` state). A reboot always returns to safe READY/UNCONFIGURED with full internal HP and LEDs off until `start` is commanded.

Default gameplay is simple and production-readable:

```json
{
  "gameplay": {
    "hp_max": 10,
    "damage_per_hit": 1,
    "phase_count": 3,
    "target_duration_ms": 2500,
    "hit_cooldown_ms": 300
  },
  "target": { "count": 4 }
}
```


### Identity vs display name

`boss_id` is the stable unique MQTT/control identity. By default it is derived from the ESP32 MAC, for example `boss_target_E465B89B51E8`, so multiple bosses can run under one Command Center without colliding.

`display_name` is only for humans/UI, for example `Mini Boss Left` or `Final Boss HP`. Command Center should send commands by `boss_id`, then show `display_name` in lists and dashboards. The firmware also includes a `name` alias in status with the same value for UI clients that expect a generic name field.

## MQTT topics

With `mqtt.root=battlebang` and `boss_id=boss_target_AABBCCDDEEFF`:

```text
battlebang/devices/{device_id}/status
battlebang/devices/{device_id}/config
battlebang/devices/{device_id}/ota
battlebang/boss_targets/{boss_id}/status
battlebang/boss_targets/{boss_id}/config
battlebang/boss_targets/{boss_id}/command
battlebang/boss_targets/{boss_id}/ota
battlebang/boss_targets/all/ota
```

Status includes `boss_id`, `display_name`, `mode`, `command_state`, `hp_remaining`, `hp_max`, `hp_pct`, `destroyed`, `target_count`, `hardware_max_targets`, `active_target_index`, and a `targets[]` array. It publishes on state changes and on a 5-second heartbeat.

## Build / upload

```bash
./.venv-pio/bin/pio run -e esp32dev_boss_target
./.venv-pio/bin/pio run -e esp32dev_boss_target -t upload --upload-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/pio device monitor -p /dev/cu.usbserial-XXXX -b 115200
```

Serial commands:

- `start`: begin ACTIVE round with full HP and random target.
- `r` / `reset`: READY reset with full internal HP and LEDs off.
- `s` / `status` / `show-status`: print JSON status.
- `h` / `hit`: simulate a hit only when debug simulation is enabled.
- `provision {json}` / `config {json}`: write runtime config to NVS.
- `show-config`, `clear-config`, `start-network`, `check-ota [url]` for bench setup.

## Local provisioning helpers

```bash
cp src/boss_target/.env.boss_target.example src/boss_target/.env.boss_target
# edit the gitignored file, then:
./.venv-pio/bin/python scripts/boss_target/provision.py --print-json --no-serial
./.venv-pio/bin/python scripts/boss_target/provision.py --serial-port /dev/cu.usbserial-XXXX
```

The helper sends one `provision {json}` line over USB serial. Leave `BOSS_TARGET_BOSS_ID` empty to use the ESP32 MAC-derived boss id; set `BOSS_TARGET_DISPLAY_NAME` for Command Center UI text and `group`/`location` for placement metadata.

## OTA policy

Boss target uses its own release channel:

- stable polling tag: `boss-target-latest`
- stable manifest asset: `boss-target-manifest.json`
- versioned tags: `boss-target-v{version}`

Default manifest URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/boss-target-latest/boss-target-manifest.json
```

With `ota.command_center_controlled=true`, automatic polling applies only when manifest `build` exactly matches provisioned `ota.desired_build`. OTA is accepted only in safe states (`READY`, `DEFEATED`, `UNCONFIGURED`) unless policy is explicitly changed.
