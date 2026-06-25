# Nixo MQTT firmware path

This folder owns the standalone Go2-mounted Nixo / game-blaster ESP32 firmware used by BTB-633 and BTB-766.

## Relay variants

Relay wiring lives under `src/nIxo/variants/<variant>/config.json` and is selected by the PlatformIO env
`custom_nixo_variant` or the `NIXO_RELAY_VARIANT` shell override.

### `relay_1ch` — legacy/default

Bench debugging confirmed the real one-channel relay path matches the pre-MQTT Bluetooth baseline:

- Channel 1: `GPIO23`
- Channel 2: disabled (`NIXO_RELAY2_PIN=-1`)
- Relay polarity: active-HIGH (`HIGH` = on/fire, `LOW` = off)
- Live Go2 mapping example: `go2_03 -> nixo_go2_03`
- MQTT topic example: `battlebang/nixo/nixo_go2_03/command`

Do not change this back to the old root-firmware relay pins (`GPIO22/21`, active-LOW) for the 1ch Nixo build. That was
the cause of MQTT logs saying “fire” while the physical relay did not move.

### `relay_2ch` — BTB-766 two-channel build

The two-channel build is separated because the hardware now uses a flywheel relay and a chain relay:

- Channel 1 / first on: `GPIO22` — flywheel
- Channel 2 / second on: `GPIO23` — chain
- Relay polarity: active-LOW (`LOW` = on/fire, `HIGH` = off)
- Inter-channel delay: `150ms`

Field observation showed the physical order was reversed with the previous mapping, so the firmware maps GPIO22 as the first flywheel channel and GPIO23 as the second chain channel. It starts the flywheel first, waits `relay_delay1_ms` (`150ms`), then starts the chain. On normal completion it turns the chain off first, then the flywheel off.

## Build and upload

From the `battlebang-esp` repo root:

```bash
pio run -e esp32dev_nixo_1ch
pio run -e esp32dev_nixo_2ch
```

`go2_03` is not part of the build env name. For this retired standalone path, set `NIXO_ID=nixo_go2_03`
through `src/nIxo/local_secrets.h` or shell env if you need compatibility testing.

Upload only when the correct Nixo ESP32 is connected:

```bash
pio run -e esp32dev_nixo_2ch -t upload --upload-port /dev/cu.usbserial-1130
pio device monitor -p /dev/cu.usbserial-1130 -b 115200
```

Expected 1ch boot log includes:

```text
[PIN] variant=relay_1ch channels=1 RELAY1=23 role=single_fire_gpio23 RELAY2=-1 role=disabled relay_on=1 relay_off=0 delay1_ms=800 servo_gpio18=unused
[MQTT] subscribed topic=battlebang/nixo/nixo_go2_03/command qos=1
```

Expected 2ch probe boot log includes:

```text
[PIN] variant=relay_2ch channels=2 RELAY1=22 role=flywheel_gpio22 RELAY2=23 role=chain_gpio23 relay_on=0 relay_off=1 delay1_ms=150 servo_gpio18=unused
[MQTT] subscribed topic=battlebang/nixo/nixo_go2_03/command qos=1
```

## Local secrets

Copy `src/nIxo/local_secrets.example.h` to `src/nIxo/local_secrets.h` and set Wi-Fi/MQTT values. The local file is
gitignored. In local bench tests, copy the Wi-Fi and broker values from the turret local secrets so Nixo uses the same
stage broker as Command Center/turrets.

Shell environment values can override the local file at build time:

```bash
NIXO_ID=nixo_go2_03 \
NIXO_WIFI_SSID='...' \
NIXO_WIFI_PASSWORD='...' \
NIXO_MQTT_HOST=<BROKER_IP> \
pio run -e esp32dev_nixo_2ch
```

Relay variant and pin overrides are also available for bench probing:

```bash
NIXO_RELAY_VARIANT=relay_2ch \
NIXO_RELAY_DELAY1_MS=200 \
pio run -e esp32dev_nixo_2ch
```

## MQTT command

Command Center publishes to:

```text
battlebang/nixo/<NIXO_ID>/command
```

Payload:

```json
{
  "schema_version": 1,
  "command": "fire",
  "nixo_id": "nixo_go2_03",
  "parent_robot_id": "go2_03",
  "enabled": true,
  "duration_ms": 3000,
  "request_id": "nexus-nixo-command-hud-...",
  "ttl_ms": 1000
}
```

`enabled=false` stops an active fire sequence immediately. The ESP deduplicates repeated `request_id` values, clamps
`duration_ms` to `NIXO_FIRE_MIN_DURATION_MS..NIXO_FIRE_MAX_DURATION_MS`, keeps the local fallback cooldown at 1500ms, and
clears any stale retained command on connect before subscribing. Command Center should publish with `retain=false`.

## Smoke tests

Direct MQTT smoke test:

```bash
mosquitto_pub -h <BROKER_IP> -p 1883 \
  -t battlebang/nixo/nixo_go2_03/command \
  -m '{"schema_version":1,"command":"fire","nixo_id":"nixo_go2_03","parent_robot_id":"go2_03","enabled":true,"duration_ms":1000,"request_id":"direct-mqtt-smoke","ttl_ms":1000}' \
  -q 1
```

Expected 1ch ESP serial evidence:

```text
[MQTT] fire on request_id=direct-mqtt-smoke duration_ms=1000
[FIRE] start source=mqtt duration_ms=1000
[RELAY] CH1 ON pin=23 role=single_fire_gpio23 level=1 readback=1
[RELAY] CH1 OFF pin=23 role=single_fire_gpio23 level=0 readback=0
[RELAY] ALL OFF / FIRE done
```

Expected 2ch probe evidence includes CH1 followed by CH2:

```text
[RELAY] CH1 ON pin=22 role=flywheel_gpio22 level=0 readback=0
[RELAY] CH2 ON pin=23 role=chain_gpio23 level=0 readback=0
[RELAY] CH2 OFF pin=23 role=chain_gpio23 level=1 readback=1
[RELAY] CH1 OFF pin=22 role=flywheel_gpio22 level=1 readback=1
```

Command Center API smoke test:

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "X-Api-Key: btb_dev" \
  -d '{"enabled":true,"duration_ms":1000,"request_id":"cc-api-smoke"}' \
  "http://127.0.0.1:8000/api/robots/go2_03/blaster/fire"
```

Use an MQTT subscriber to confirm the backend publish:

```bash
mosquitto_sub -h <BROKER_IP> -p 1883 -q 1 -t 'battlebang/nixo/nixo_go2_03/command' -v
```

The Command Center response and subscriber output prove broker publish. Physical fire still requires ESP serial or the
relay itself as evidence because this firmware does not publish an ack/status topic yet.

## Bluetooth baseline note

`BluetoothSerial.cpp` is a legacy smoke/baseline sketch. It uses the same GPIO23 active-HIGH relay behavior, but it reads
Bluetooth SPP only (`SerialBT.available()`), not USB Serial. Sending `f` over USB Serial to that file will not fire; pair
with the ESP Bluetooth device and send `f` over SPP if you need to compare the pre-MQTT baseline.
