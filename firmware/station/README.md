# BattleBang Station firmware

`firmware/station` is the Target Station firmware for six Station boards used by Fleet Dashboard `/demo` tab `2`. Each board has one piezo impact sensor and one local Station LED strip/ring. The first detected hit sets `"active": true`, changes the LED to captured color, and locks further shocks until `reset`/`unlock` arrives.

This is separate from **Heavy Blaster**. Station status does not reuse Heavy Blaster topics, commands, or matrix-slot semantics.

## Hardware profile

- ESP32 dev board (`esp32dev_station`).
- Piezo analog input: GPIO 32 (`PIEZO_AO_PIN`).
- Station LED data: GPIO 33 (`LED_PIN`).
- LED profile: WS2812B, RGB order, default 60 LEDs, max buffer 180 LEDs.

Pin identity is compiled into `build_config.h`; NVS can tune thresholds/counts/colors but cannot silently move the safety-relevant pin profile.

## MQTT contract for Fleet Dashboard `/demo`

Station publishes status to the generic ESP status path:

```text
battlebang/devices/station/{station_id}/status
```

Command/config/OTA topics:

```text
battlebang/devices/station/{station_id}/command
battlebang/devices/station/{station_id}/config
battlebang/devices/station/{station_id}/ota
battlebang/devices/station/all/ota
```

Example status shape:

```json
{
  "schema_version": 1,
  "type": "status",
  "device_type": "station",
  "firmware_app": "battlebang-station",
  "firmware_hardware": "esp32dev-target-station-v1",
  "device_id": "station_01",
  "station_id": "station_01",
  "stage_id": "lit4f_260623",
  "active": true,
  "station": {
    "station_id": "station_01",
    "active": true
  }
}
```

`active=false` means waiting/not captured. `active=true` means captured by the first valid piezo hit. Command Center’s generic ESP status subscriber normalizes this into `esp_status`, and Fleet Dashboard `/demo` uses the Station ids registered in tab `2`.

## First USB upload and six-board provisioning

Blank ESP32 boards need one USB serial upload. After upload, all board identity and broker/Wi-Fi settings are mutable in NVS.

1. Copy and edit local settings (ignored by git):

```bash
cp firmware/station/.env.station.example firmware/station/.env.station
$EDITOR firmware/station/.env.station
```

2. Upload and provision six Station boards with the same firmware image:

```bash
./.venv-pio/bin/python scripts/station/flash_and_provision.py \
  --station station_01=/dev/cu.usbserial-110 \
  --station station_02=/dev/cu.usbserial-120 \
  --station station_03=/dev/cu.usbserial-130 \
  --station station_04=/dev/cu.usbserial-140 \
  --station station_05=/dev/cu.usbserial-150 \
  --station station_06=/dev/cu.usbserial-160
```

The helper runs `pio run -e esp32dev_station -t upload` once per `station_id=serial_port`, then sends `provision {json}` over serial. Each board receives its own `station_id`; Wi-Fi/MQTT/OTA fields come from `.env.station` and can be changed later with another provisioning/config push.

3. Single-board provisioning after firmware is already flashed:

```bash
./.venv-pio/bin/python scripts/station/provision.py \
  --station-id station_01 \
  --serial-port /dev/cu.usbserial-110
```

## Serial commands

- `show-status` / `status` — print current JSON status.
- `show-config` — print NVS-backed runtime config with secrets masked.
- `reset` / `unlock` — clear captured state and allow the next hit.
- `simulate-hit` — only when `debug_allow_simulate_hit=true`.
- `provision {json}` / `config {json}` — apply and persist runtime config.
- `start-network`, `stop-network`, `check-ota [url]`, `clear-config`.

## OTA

- PlatformIO env: `esp32dev_station`.
- App: `battlebang-station`.
- Hardware: `esp32dev-target-station-v1`.
- Stable polling manifest: `station-latest/station-manifest.json`.
- Versioned audit tags: `station-v{version}`.

## Validation

```bash
.venv-turret-tests/bin/python -m pytest tests/python/test_station_contract.py tests/python/test_firmware_template_contract.py -q
python3 -m py_compile scripts/station/*.py tests/python/test_station_contract.py tests/python/test_firmware_template_contract.py
./.venv-pio/bin/pio run -e esp32dev_station
git diff --check
```

Hardware validation still requires uploading to the actual six Station boards and capturing serial/MQTT evidence for each configured `station_id`.
