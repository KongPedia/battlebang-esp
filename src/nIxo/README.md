# Nixo MQTT firmware path

This folder owns the Go2-mounted Nixo / game-blaster ESP32 firmware used by BTB-633.

## Current hardware contract

Bench debugging confirmed the real relay path matches the pre-MQTT Bluetooth baseline:

- Relay pin: `GPIO23`
- Relay polarity: active-HIGH (`HIGH` = on/fire, `LOW` = off)
- Second relay: disabled (`NIXO_RELAY2_PIN=-1`)
- Live Go2 mapping: `go2_03 -> nixo_go2_03`
- MQTT topic: `battlebang/nixo/nixo_go2_03/command`

Do not change this back to the old root-firmware relay pins (`GPIO22/21`, active-LOW) for Nixo. That was the cause of
MQTT logs saying “fire” while the physical relay did not move.

## Build and upload

From the `battlebang-esp` repo root:

```bash
pio run -e esp32dev_nixo
```

Upload only when the correct Nixo ESP32 is connected:

```bash
pio run -e esp32dev_nixo -t upload --upload-port /dev/cu.usbserial-1130
pio device monitor -p /dev/cu.usbserial-1130 -b 115200
```

Expected boot log includes:

```text
[PIN] ... RELAY1=23 RELAY2=-1 relay_on=1 relay_off=0
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
NIXO_MQTT_HOST=jetson-go2-02.local \
pio run -e esp32dev_nixo
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
  "duration_ms": 1500,
  "request_id": "nexus-nixo-command-hud-...",
  "ttl_ms": 1000
}
```

`enabled=false` stops an active fire sequence immediately. The ESP deduplicates repeated `request_id` values, clamps
`duration_ms` to `NIXO_FIRE_MIN_DURATION_MS..NIXO_FIRE_MAX_DURATION_MS`, and clears any stale retained command on connect
before subscribing. Command Center should publish with `retain=false`.

## Smoke tests

Direct MQTT smoke test:

```bash
mosquitto_pub -h jetson-go2-02.local -p 1883 \
  -t battlebang/nixo/nixo_go2_03/command \
  -m '{"schema_version":1,"command":"fire","nixo_id":"nixo_go2_03","parent_robot_id":"go2_03","enabled":true,"duration_ms":1000,"request_id":"direct-mqtt-smoke","ttl_ms":1000}' \
  -q 1
```

Expected ESP serial evidence:

```text
[MQTT] fire on request_id=direct-mqtt-smoke duration_ms=1000
[FIRE] start source=mqtt duration_ms=1000
[RELAY] CH1 ON pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=23 level=0 readback=0
[RELAY] ALL OFF / FIRE done
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
mosquitto_sub -h jetson-go2-02.local -p 1883 -q 1 -t 'battlebang/nixo/nixo_go2_03/command' -v
```

The Command Center response and subscriber output prove broker publish. Physical fire still requires ESP serial or the
relay itself as evidence because this firmware does not publish an ack/status topic yet.

## Bluetooth baseline note

`BluetoothSerial.cpp` is a legacy smoke/baseline sketch. It uses the same GPIO23 active-HIGH relay behavior, but it reads
Bluetooth SPP only (`SerialBT.available()`), not USB Serial. Sending `f` over USB Serial to that file will not fire; pair
with the ESP Bluetooth device and send `f` over SPP if you need to compare the pre-MQTT baseline.
