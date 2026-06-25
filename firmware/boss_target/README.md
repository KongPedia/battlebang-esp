# Boss Target firmware

`firmware/boss_target/` is the boss-stage ESP32 firmware for **4 target LED rings + 4 piezo AO inputs + 1 HP bar**. It is separate from `src/hit_target/` because this device owns the boss HP bar and publishes boss-level status for Command Center orchestration.

## Operating model

- Boot/reset: internal HP is full, target rings stay off, and the GPIO12 HP bar shows a dim white 30% idle fill; hits are ignored until `start`.
- MQTT/serial `start`: all target rings and the HP bar run a 5-second fast neon-rainbow orbit intro (`INTRO`), then the HP bar turns on at full green and one random target ring becomes active.
- During ACTIVE: only the currently active ring accepts damage; wrong-ring hits are reported but do not reduce HP.
- After a correct hit: `damage_per_hit` is subtracted, the hit ring blinks white for about 1 second, hits are temporarily ignored, status is published, and then another random target is selected from `0..target.count-1`.
- HP reaches 0: state becomes `DEFEATED`/`dead`, status keeps publishing periodically with `hp_remaining=0` and `destroyed=true` so Command Center can send turret `dead`/inactive commands.
- `reset`: returns to READY with full internal HP, target rings off, and the GPIO12 HP bar back to the dim white 30% idle fill. It does not start the game.

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
| Piezo AO 1 | GPIO34 |
| Piezo AO 2 | GPIO27 |
| Piezo AO 3 | GPIO32 |
| Piezo AO 4 | GPIO33 |
| HP bar data | GPIO12 |
| LED type/order | WS2811 / RGB |
| Ring LEDs | 120 per target |
| HP bar LEDs | 300 (100 vertical HP columns × 3 horizontal rows) |

Game logic iterates over `target.count`, not hard-coded `4`; if a future 6-target board appears, add a new hardware profile/env with a larger compiled capacity and pin map, then keep the random/HP/status logic mostly unchanged.

The HP bar is physically wired as three horizontal LED rows on one data line.
The renderer groups those rows by vertical column, so HP always drains by overall `hp_remaining / hp_max` ratio across all three rows together. Depleted columns are simply off; they do not blink or show a trailing next-color segment.

Latest live boss-stage wiring provided on 2026-06-19:

```cpp
#define RING1_PIN   23
#define RING2_PIN   21
#define RING3_PIN   18
#define RING4_PIN   17
#define HP_BAR_PIN  12
#define PIEZO_AO_1  34
#define PIEZO_AO_2  27
#define PIEZO_AO_3  32
#define PIEZO_AO_4  33
```

Bench GPIO check on 2026-06-19 with static-color diagnostic:

| GPIO | Observed hardware | Note |
| --- | --- | --- |
| GPIO23 | Ring 1 data | Initially open/disconnected during bench pass; keep as Ring 1 after wiring repair. |
| GPIO21 | Ring 2 data | Confirmed as a ring data line. |
| GPIO18 | Ring 3 data | Confirmed as a ring data line. |
| GPIO17 | Ring 4 data | Confirmed as a ring data line. |
| GPIO12 | HP bar data | Confirmed HP bar data line. |
| GPIO26 | Not used | Old HW branch HP pin; not current HP wiring. |

The original `HW_BossStage_Target` branch `main.cpp` differs from this live wiring:
its HP bar pin is GPIO26 and its piezo inputs are digital DO pins GPIO27/32/33/25.
The temporary bench Boss Stage firmware uploaded on 2026-06-19 keeps the original random-target/HP gameplay but uses ring pins 23/21/18/17, HP GPIO12, 120 LEDs per ring, 300 HP-bar LEDs, and analog AO polling on GPIO34/27/32/33.

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

`config_version` is mandatory for all `provision` and `config` payloads; the
firmware rejects missing/zero versions and versions older than the active NVS
config.

Persisted NVS config includes:

- identity/placement (`device_id`, `boss_id`, `display_name`, `group`, `stage_id`, `location`)
- Wi-Fi and MQTT connection settings
- gameplay values (`hp_max`, `damage_per_hit`, target duration, cooldown). Defaults are normalized as 10 HP / 1 damage, and provisioning can change how many hits defeat the boss.
- target count and LED counts/colors
- OTA policy
- debug simulation flag for bench testing

`hardware_profile` is accepted in provision/config payloads and validated
against the compiled board profile, but it is not a live pin-remapping setting.
GPIO pins are compiled into the firmware.

NVS intentionally does **not** store match progress (`hp_remaining`, `active_target_index`, `ACTIVE` state). A reboot always returns to safe READY/UNCONFIGURED with full internal HP, target rings off, and the GPIO12 HP bar in dim white 30% idle fill until `start` is commanded.

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

`boss_id`, `target_id`, and `device_id` are restricted to MQTT-safe topic segment characters. Non-empty `stage_id` follows the same MQTT-safe topic segment rule (`A-Z`, `a-z`, `0-9`, `_`, `-`, `.`). `mqtt.root` may contain `/`, but only as separators between safe non-empty segments.

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

Status includes `boss_id`, `display_name`, `group`, `stage_id`, `location`, `mode`, `command_state`, `hp_remaining`, `hp_max`, `hp_pct`, `destroyed`, `target_count`, `hardware_max_targets`, HP bar LED diagnostics (`hp_bar_pin`, `hp_bar_num_leds`, `hp_bar_brightness`, `led_type`, `color_order`, `led_test_active`), `start_intro_active`, `target_transition_pending`, `active_target_index`, and a `targets[]` array. It publishes on state changes and on a 5-second heartbeat.

Bench LED diagnostics:

```json
{"command":"led_test","color":"#FFFFFF","duration_ms":30000,"hp_only":true}
{"command":"led_test_off"}
```

`led_test` bypasses gameplay HP layout and writes the first `hp_bar.num_leds` pixels linearly on GPIO12, which is useful for separating firmware/MQTT health from HP bar wiring, data direction, level shifting, and common-ground issues.

## Build / upload

```bash
./.venv-pio/bin/pio run -e esp32dev_boss_target
./.venv-pio/bin/pio run -e esp32dev_boss_target -t upload --upload-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/pio device monitor -p /dev/cu.usbserial-XXXX -b 115200
```

Serial commands:

- `start`: begin the 5-second fast neon-rainbow orbit intro, then enter ACTIVE with full HP and a random target.
- `r` / `reset`: READY reset with full internal HP, target rings off, and the GPIO12 HP bar in dim white 30% idle fill.
- `s` / `status` / `show-status`: print JSON status.
- `led-test [#RRGGBB] [duration_ms]`, `led-test-off`: force HP bar diagnostic output on GPIO12.
- `h` / `hit`: simulate a hit only when debug simulation is enabled.
- `provision {json}` / `config {json}`: write runtime config to NVS.
- `show-config`, `clear-config`, `start-network`, `check-ota [url]` for bench setup.

## Local provisioning helpers

```bash
cp firmware/boss_target/.env.boss_target.example firmware/boss_target/.env.boss_target
# edit the gitignored file, then:
./.venv-pio/bin/python scripts/boss_target/provision.py --print-json --no-serial
./.venv-pio/bin/python scripts/boss_target/provision.py --serial-port /dev/cu.usbserial-XXXX
```

The helper sends one `provision {json}` line over USB serial. Leave `BOSS_TARGET_BOSS_ID` empty to use the ESP32 MAC-derived boss id; set `BOSS_TARGET_DISPLAY_NAME` for Command Center UI text, `BOSS_TARGET_STAGE_ID` for stage-scoped routing, and `group`/`location` for placement metadata.

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

HTTPS OTA syncs the ESP32 clock, then uses a pinned GitHub Release root CA rather than disabling TLS verification. If a firmware download stalls or fails after entering OTA-prepared mode, the controller clears OTA-prepared state and restores normal target rendering instead of staying dark with hits disabled.
