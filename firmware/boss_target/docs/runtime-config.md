# Boss Target runtime config and NVS

This file documents the values that can change after flashing and therefore belong in ESP32 NVS.
It also separates the three config artifacts used by this firmware:

| Artifact | Tracked in git | Contains secrets | Role |
| --- | --- | --- | --- |
| `firmware/boss_target/config.json` | yes | no | Factory-default/schema reference for the current firmware build. It is documentation and test input; firmware does not auto-load it at runtime. |
| `firmware/boss_target/.env.boss_target.example` | yes | no | Operator template for local provisioning. Copy to ignored `.env.boss_target` and fill Wi-Fi/MQTT/serial values. |
| `firmware/boss_target/.env.boss_target` | no | yes | Local machine input for `scripts/boss_target/provision.py`; never commit this file. |
| `firmware/boss_target/examples/*.json` | yes | no | Command Center / serial payload examples for `provision {json}`, `config {json}`, and MQTT command/config topics. |
| ESP32 NVS namespace `boss_target` | no | yes, on-device | Actual runtime config persisted on the device after `provision {json}`, `config {json}`, or MQTT config updates. |

## How values enter NVS

Initial board setup should normally use USB serial:

```bash
./.venv-pio/bin/python scripts/boss_target/provision.py --serial-port /dev/cu.usbserial-XXXX
```

The helper reads the ignored `.env.boss_target`, generates a compact JSON payload, and sends:

```text
provision {json}
```

After a device is online, Command Center can publish partial updates to:

```text
battlebang/boss_targets/{boss_id}/config
```

or the same JSON can be sent over serial as:

```text
config {json}
```

Every provisioning/config update must include a positive `config_version`. The firmware rejects missing/zero versions and any version lower than the active config; equal versions are allowed so Command Center can safely retry the same payload.

`boss_id`, `target_id`, and `device_id` are MQTT topic segments, so they must use only the allowed character set. Non-empty `stage_id` follows the same safe topic-segment rule because Command Center may use it as a stage routing key: A-Z, a-z, 0-9, `_`, `-`, or `.`. `mqtt.root` is normalized as slash-separated MQTT topic segments with the same allowed characters per segment; leading/trailing slashes are trimmed and empty middle segments are rejected.

## Persisted NVS values

The firmware stores these fields in the `boss_target` NVS namespace. JSON paths are the public provisioning/config contract; NVS keys are implementation details and can be renamed in a migration.

| JSON path | NVS key | Default | Example | Notes |
| --- | --- | --- | --- | --- |
| `config_version` | `cfg_ver` | `0` | `1700000000` | Mandatory positive version used to reject stale config. |
| `configured` / `type=provision` | `configured` | `false` | `true` | `type: "provision"` marks the device configured. |
| `device_id` | `device_id` | MAC-derived | `boss_target_dev_01` | Stable device-level identity for `battlebang/devices/boss_target/{device_id}/...` topics. Leave empty during provision to use the ESP32 MAC-derived id. Must be a safe MQTT topic segment. |
| `boss_id` | `boss_id` | MAC-derived | `boss_target_E465B89B51E8` | Stable unique control identity and MQTT route. Leave empty during provision to use the ESP32 MAC-derived id. Must be a safe MQTT topic segment. |
| `target_id` | `target_id` | same as `boss_id` | `boss_target_E465B89B51E8` | Compatibility alias for older hit-target consumers. Must be a safe MQTT topic segment. |
| `display_name` / `name` | `display` | `boss_id` | `Mini Boss Left` | Human-readable UI label. Commands should still route by `boss_id`. |
| `group` | `group` | empty | `boss-stage` | Role/product grouping metadata for Command Center. |
| `stage_id` | `stage_id` | empty | `boss_stage_v1` | Arena/stage routing key persisted in NVS so Command Center can target only devices assigned to one stage. |
| `location` | `location` | empty | `stage-left` | Human-readable placement metadata. |
| `debug_allow_simulate_hit` | `debug_sim` | `false` | `false` | Enables MQTT/serial simulated hits only for bench testing. Keep false in production. |
| `gameplay.hp_max` | `hp_max` | `10` | `10` | Full normalized HP at boot/reset/start. Set this with `damage_per_hit` to choose how many correct hits defeat the boss. Range: 1..999. |
| `gameplay.damage_per_hit` | `damage` | `1` | `1` | Damage from a correct active-target hit. With defaults, 10 hits defeat the boss. Range: 1..`hp_max`. |
| `gameplay.phase_count` | `phases` | `3` | `3` | Number of HP bar palette phases. Range: 1..8. |
| `gameplay.start_resets_hp` | `start_reset` | `true` | `true` | `start` restores HP to full before activating a target. |
| `gameplay.target_duration_ms` | `target_ms` | `2500` | `2500` | Time before the active target moves. Range: 250..60000. |
| `gameplay.hit_cooldown_ms` | `cooldown` | `300` | `300` | Per-target hit debounce/cooldown. Range: 20..5000. |
| `gameplay.digital_isr_debounce_us` | `isr_us` | `20000` | `20000` | Legacy debounce field retained for piezo compatibility. Range: 500..50000. |
| `target.count` | `tgt_count` | `4` | `4` | Runtime active target count. Must be 1..compiled `kMaxTargets` (currently 4). |
| `target.ring_num_leds` | `ring_leds` | `120` | `120` | LED count per target ring. Must fit compiled LED buffer. |
| `target.active_color` | `active_rgb` | `#FF0000` | `#FF0000` | Active ring color as RGB hex. |
| `target.hit_flash_color` | `flash_rgb` | `#FFFFFF` | `#FFFFFF` | Correct-hit flash color as RGB hex. |
| `hp_bar.num_leds` | `hp_leds` | `300` | `300` | HP bar LED count. Must fit compiled LED buffer and be divisible by 3 because the bar renders as 100 vertical HP columns × 3 horizontal rows. |
| `hp_bar.brightness` | `brightness` | `255` | `255` | FastLED brightness. Range: 1..255. |
| `hp_bar.max_ma` | `max_ma` | `12000` | `12000` | FastLED power cap. Range: 100..12000. |
| `hp_bar.dead_blink_ms` | `dead_blink` | `300` | `300` | HP bar blink period when defeated. |
| `hp_bar.palette[]` | `pal0`..`pal7` | green/yellow/red | `#00FF00` | Lit HP-bar color phases by remaining HP ratio. Depleted columns stay off. Only `phase_count` entries are reported. |
| `wifi.ssid` | `wifi_ssid` | empty | `BattleBangWiFi` | Provision over USB when possible. |
| `wifi.password` | `wifi_pass` | empty | secret | Secret; omitted from tracked files and redacted from `show-config` unless explicitly requested in code. |
| `wifi.auto_start` | `net_auto` | `false` in firmware, `true` in helper | `true` | Start Wi-Fi/MQTT automatically after boot when configured. |
| `wifi.start_delay_ms` | `net_delay` | `0` | `0` | Optional network start delay. |
| `mqtt.host` | `mqtt_host` | empty | `command-center.local` | Command Center / broker host. |
| `mqtt.port` | `mqtt_port` | `1883` | `1883` | MQTT broker port. |
| `mqtt.username` | `mqtt_user` | empty | `battlebang` | Optional MQTT auth. |
| `mqtt.password` | `mqtt_pass` | empty | secret | Secret; keep in ignored `.env.boss_target` or secure Command Center channel. |
| `mqtt.root` | `mqtt_root` | `battlebang` | `battlebang/boss-stage` | Topic root. Trimmed and validated as slash-separated safe MQTT topic segments. |
| `ota.command_center_controlled` | `ota_cc` | `true` | `true` | If true, automatic polling applies only to desired Command Center build. |
| `ota.auto_check_enabled` | `ota_auto` | `false` | `false` | Enables periodic manifest polling. |
| `ota.channel` | `ota_channel` | `boss-target` | `boss-target` | Firmware OTA channel. |
| `ota.desired_build` | `ota_build` | `0` | `0` | Command Center desired build when controlled. |
| `ota.public_manifest_url` | `ota_pub` | boss target stable URL | GitHub release URL | Public OTA manifest URL. |
| `ota.local_mirror_url` | `ota_mirror` | empty | local mirror URL | Optional LAN mirror. |
| `ota.check_interval_s` | `ota_secs` | `300` | `300` | Minimum is clamped to 30 seconds. |
| `ota.apply_only_in_safe_state` | `ota_safe` | `true` | `true` | OTA only while READY, DEFEATED, or UNCONFIGURED. |

## Accepted but not persisted as mutable NVS

`hardware_profile` appears in provision/config payloads and status/config output, but it is a compatibility guard, not a field for live pin remapping. The current firmware validates it against the compiled board profile:

```json
{
  "max_targets": 4,
  "ring_pins": [23, 21, 18, 17],
  "piezo_do_pins": [34, 27, 32, 33],
  "hp_bar_pin": 12,
  "led_type": "WS2811",
  "color_order": "RGB"
}
```

If a future 6-target board exists, add a new compiled hardware profile / PlatformIO environment and then set `target.count <= kMaxTargets` for that profile. Do not expect MQTT or NVS to move GPIO pins on an already compiled firmware.

If an OTA boot finds an older stored `hardware_profile` or LED shape that no longer validates against the compiled firmware, the firmware keeps the safe compiled hardware defaults but attempts to preserve identity, Wi-Fi, MQTT, debug, and OTA fields so the device can still reconnect and receive a corrected config.

## Intentionally not persisted

Match progress is runtime state and is deliberately not written to NVS:

- `hp_remaining`
- `hp_pct`
- `active_target_index`
- `targets[].active`
- `mode` / `command_state` INTRO/ACTIVE progress
- `sequence`, last hit information, target timers, debounce timestamps

A reboot or `reset` always returns to safe READY/UNCONFIGURED with internal HP full, target rings off, and the GPIO12 HP bar in dim white 30% idle fill. The game starts only after a `start` command, which first runs a 5-second fast neon-rainbow orbit INTRO before ACTIVE target play.

## Example files

- `firmware/boss_target/examples/provision.boss-target.example.json` — full first-provision payload without real secrets.
- `firmware/boss_target/examples/config.display-name.example.json` — small UI/placement rename update.
- `firmware/boss_target/examples/config.gameplay-target.example.json` — gameplay and target-count update.
- `firmware/boss_target/examples/command.start.example.json` — MQTT command payload.
- `firmware/boss_target/examples/command.reset.example.json` — MQTT command payload.
- `firmware/boss_target/examples/command.status.example.json` — MQTT command payload.
