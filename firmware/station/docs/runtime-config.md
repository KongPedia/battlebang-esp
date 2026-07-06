# Station runtime config

Station uses the active firmware template: identity, `stage_id`, Wi-Fi, MQTT, OTA policy, sensor thresholds, LED behavior, and gameplay lock policy are stored in ESP32 NVS via `firmware/station/config/runtime_config.*`.

## Mutable NVS fields

- `station_id` — game-facing Station id, for example `station_01` through `station_06`.
- `device_id` — management id; provisioning defaults it to the same value as `station_id` for Fleet Dashboard `/demo` clarity.
- `display_name`, `group`, `stage_id`, `location` — operator metadata.
- `wifi.ssid`, `wifi.password`, `wifi.auto_start`, `wifi.start_delay_ms` — network settings.
- `mqtt.host`, `mqtt.port`, `mqtt.username`, `mqtt.password`, `mqtt.root` — broker settings.
- `ota.command_center_controlled`, `ota.auto_check_enabled`, `ota.desired_build`, `ota.public_manifest_url`, `ota.local_mirror_url`, `ota.check_interval_s`, `ota.apply_only_in_safe_state`.
- `sensor.hit_threshold`, `sensor.release_threshold`, `sensor.hit_cooldown_ms`, `sensor.sample_interval_ms`, `sensor.settle_us`.
- `led.num_leds`, `led.brightness`, `led.max_ma`, `led.waiting_color`, `led.captured_color`, `led.hit_flash_color`, `led.waiting_breath_bpm`, `led.waiting_breath_min`, `led.waiting_breath_max`.
- `gameplay.lock_after_hit`, `gameplay.auto_reset_ms`, `gameplay.heartbeat_interval_ms`.

## Not stored as runtime progress

Match progress is not stored in NVS. In other words, match progress is not stored in NVS. A reboot returns to `WAITING` unless Command Center publishes a fresh game state. This prevents stale `captured` state from carrying between demos.

Hardware pin identity (`piezo_pin`, `led_pin`, `led_type`, `color_order`) is validated against the compiled board profile and rejected when it does not match.

## Minimal provisioning payload

```json
{
  "type": "provision",
  "schema": 1,
  "config_version": 1,
  "configured": true,
  "device_id": "station_01",
  "station_id": "station_01",
  "display_name": "Station 01",
  "stage_id": "lit4f_260623",
  "wifi": { "ssid": "YOUR_WIFI_SSID", "password": "YOUR_WIFI_PASSWORD", "auto_start": true },
  "mqtt": { "host": "COMMAND_CENTER_IP_OR_DNS", "port": 1883, "root": "battlebang" }
}
```
