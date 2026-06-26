# Go2-Nixo Integrated Fallback Firmware

`firmware/go2_nixo` is the optional one-ESP fallback for Go2 hit/HP bar + Nixo/game-blaster relay fire. The normal active path is the generic `firmware/go2/` hit/LED ESP; use this integrated firmware only when one ESP must own both hit sensing/display and relay fire.

This firmware is also **generic image + NVS runtime config**:

- Build envs select hardware/relay variant only: `esp32dev_go2_nixo`, `esp32dev_go2_nixo_1ch`, `esp32dev_go2_nixo_2ch`.
- `go2_01`, `go2_02`, `go2_03` etc. are not build envs. They are runtime `robot_id` values stored in ESP32 NVS.
- `nixo_id` and Nixo command topic are also NVS runtime config.
- Nixo fire timing/envelope (`default/min/max duration`, cooldown, prefire delay, relay inter-channel delay) is NVS runtime config.
- Relay pins, polarity, and channel count remain build-time hardware-safety variant data.

Responsibilities:

1. Accept piezo AO ADC threshold crossings locally as hits.
2. Render local HP/down state on the 84 LED HP bar.
3. Publish `hit_event` and device status with `accepted_hit_count`, `hp_remaining`, `max_hits`, and `down`.
4. Render local Nixo fire/cooldown state on the ring LED.
5. Subscribe to Nixo `fire` commands and execute the relay fire sequence.

## Topics

- ESP → Command Center: `battlebang/hit/{robot_id}/events`
- Command Center → ESP HP bar reset/debug only: `battlebang/hit/{robot_id}/ring_display/command`
- Command Center → Nixo relay: `battlebang/nixo/{nixo_id}/command`
- Device management: `{mqtt_root}/devices/{device_id}/status|config|ota`

Example after provisioning `robot_id=go2_03`, `nixo_id=nixo_go2_03`:

```text
battlebang/hit/go2_03/events
battlebang/hit/go2_03/ring_display/command
battlebang/nixo/nixo_go2_03/command
```

## Hardware defaults

Defaults live in `firmware/go2_nixo/hardware_profile.json` plus optional relay variant JSON files under `firmware/go2_nixo/variants/`.

| Part | Default | Runtime? |
| --- | --- | --- |
| HP bar LED data | GPIO18 / 84 LEDs | No, build hardware profile |
| Ring LED data | GPIO4 / 40 LEDs | No, build hardware profile |
| Piezo AO / D0 debug | GPIO34 / GPIO27 | No, build hardware profile |
| Piezo threshold/rearm | 200 / 150 raw | Yes, NVS tuning |
| Local HP model | 14 max hits, 900ms hit flash | Yes, NVS tuning |
| HP/ring brightness | 120 / 80 | Yes, NVS tuning |
| Relay 1ch | GPIO23 active-HIGH | No, build variant |
| Relay 2ch | GPIO22 flywheel + GPIO23 chain, active-LOW | No, build variant |
| Nixo fire timing | default 3000ms, cooldown 1500ms, prefire 600ms, relay delay1 800ms | Yes, NVS tuning |

## Build, upload, provision

```bash
# Build/upload default 1ch integrated image
./.venv-pio/bin/pio run -e esp32dev_go2_nixo -t upload --upload-port /dev/cu.usbserial-XXXX

# Or explicit variants
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_1ch
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch -t upload --upload-port /dev/cu.usbserial-XXXX

# Provision runtime identity, stage, network, MQTT, OTA, hit/display tuning including local HP count/flash, and Nixo fire timing
cp firmware/go2_nixo/.env.go2_nixo.example firmware/go2_nixo/.env.go2_nixo
# Edit GO2_NIXO_ROBOT_ID=go2_03, GO2_NIXO_NIXO_ID=nixo_go2_03, GO2_NIXO_STAGE_ID=stage_1.
./.venv-pio/bin/python scripts/go2_nixo/provision.py --no-serial --print-json
./.venv-pio/bin/python scripts/go2_nixo/provision.py --serial-port /dev/cu.usbserial-XXXX

# Inspect or clear runtime config
./.venv-pio/bin/python scripts/go2_nixo/provision.py --command show-config --serial-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/python scripts/go2_nixo/provision.py --command show-status --serial-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/python scripts/go2_nixo/provision.py --command clear-config --serial-port /dev/cu.usbserial-XXXX
```

`firmware/go2_nixo/.env.go2_nixo` is the canonical local secret/config input for
standardized builds. `local_secrets.h` is not read by default; only enable it for
explicit legacy/factory fallback with `BATTLEBANG_ENABLE_LOCAL_SECRETS`.

## NVS-configured fields

`provision`/`config` payloads include common `wifi`, `mqtt`, `ota` fields plus these domain fields:

- `robot_id`, `hit_topic_prefix`
- `nixo_id`, `nixo_command_topic_prefix`
- hit/display tuning: `hit_cooldown_ms`, `offline_queue_capacity`, `offline_queue_flush_interval_ms`, `led_brightness`, `ring_brightness`, `piezo_ao_threshold_raw`, `piezo_ao_rearm_raw`, `piezo_ao_capture_window_ms`, `piezo_ao_debug_period_ms`, `piezo_ao_rearm_stable_ms`, `max_hits`, `hit_flash_ms`
- Nixo fire timing/envelope: `nixo_fire_default_duration_ms`, `nixo_fire_min_duration_ms`, `nixo_fire_max_duration_ms`, `nixo_fire_cooldown_ms`, `nixo_prefire_delay_ms`, `nixo_relay_delay1_ms` (or the same fields nested under `nixo` without the `nixo_` prefix)

Power cycling the ESP resets local HP state: `accepted_hit_count`, `hp_remaining`, and `down` are not persisted to NVS. On boot the ESP reads the configured `max_hits` rule and starts from full HP.

Do not put relay pins, relay polarity, relay channel count, or physical LED capacity into runtime config; those stay in hardware/variant profiles so Command Center cannot accidentally remap hardware wiring.

## OTA

Go2-Nixo uses the common `bb_esp_ota` HTTP OTA engine. `show-status` and MQTT device status include `ota_supported=true`, `ota_manifest_url`, `ota_channel`, `ota_desired_build`, and `post_ota_reboot`. Serial/BT/Jetson `check-ota [manifest-url]`, MQTT `{mqtt_root}/devices/{device_id}/ota`, and automatic polling all pass through manifest app/hardware/build/hash/safe-state checks. If `ota.apply_only_in_safe_state=true`, OTA is deferred while the Nixo relay is firing.

OTA is split by relay hardware variant because 1ch and 2ch use different relay pins/polarity:

| Build env | Relay variant | OTA channel | Stable manifest | Firmware hardware id |
| --- | --- | --- | --- | --- |
| `esp32dev_go2_nixo_1ch` | `relay_1ch` | `go2-nixo-1ch` | `https://github.com/KongPedia/battlebang-esp/releases/download/go2-nixo-1ch-latest/go2-nixo-1ch-manifest.json` | `esp32dev-go2-nixo-relay-1ch-v1` |
| `esp32dev_go2_nixo_2ch` | `relay_2ch` | `go2-nixo-2ch` | `https://github.com/KongPedia/battlebang-esp/releases/download/go2-nixo-2ch-latest/go2-nixo-2ch-manifest.json` | `esp32dev-go2-nixo-relay-2ch-v1` |

Provision NVS with the matching `GO2_NIXO_RELAY_VARIANT` or `scripts/go2_nixo/provision.py --relay-variant ...`; otherwise OTA channel and manifest URL will not match the flashed relay hardware.

## Runtime verification logs

After provisioning `go2_03`:

```text
[CC] robot_id=go2_03 mqtt=enabled broker=<MQTT_BROKER_IP>:1883 event_topic=battlebang/hit/go2_03/events ring_topic=battlebang/hit/go2_03/ring_display/command
[NIXO] mqtt=enabled nixo_id=nixo_go2_03 command_topic=battlebang/nixo/nixo_go2_03/command relay1=23 relay2=-1 relay_on=1 relay_off=0 delay1_ms=800 fire_default_ms=3000 fire_min_ms=100 fire_max_ms=10000 cooldown_ms=1500 prefire_ms=600
```

Nixo fire command example:

```json
{
  "schema_version": 1,
  "command": "fire",
  "nixo_id": "nixo_go2_03",
  "parent_robot_id": "go2_03",
  "enabled": true,
  "duration_ms": 1000,
  "request_id": "manual-fire-001"
}
```
