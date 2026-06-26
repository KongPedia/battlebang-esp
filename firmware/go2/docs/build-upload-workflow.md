# Go2 ESP build / upload / provision flow

Go2 hit/LED firmware is now a **generic image + NVS runtime config** flow. Do not create per-robot PlatformIO envs. `go2_01`, `go2_02`, `go2_03` etc. are runtime `robot_id` values provisioned into ESP32 NVS.

## 1. Hardware profile vs runtime config

Commit-time hardware fallback lives in `firmware/go2/hardware_profile.json`:

- HP bar LED pin/count/capacity
- piezo AO/D0 pins
- factory fallback piezo threshold/rearm/capture/debug defaults
- MQTT topic prefix fallback

Runtime values are provisioned from `firmware/go2/.env.go2` via `scripts/go2/provision.py`:

- `GO2_ROBOT_ID`, `GO2_STAGE_ID`, `GO2_GROUP`, `GO2_LOCATION`
- Wi-Fi/MQTT/OTA policy
- `GO2_HIT_TOPIC_PREFIX`
- hit tuning: cooldown, offline queue, LED brightness, piezo threshold/rearm/capture/debug/rearm-stable

## 2. Build generic image

```bash
./.venv-pio/bin/pio run -e esp32dev_go2
```

## 3. Upload generic image

```bash
./.venv-pio/bin/pio run -e esp32dev_go2 -t upload --upload-port /dev/cu.usbserial-xxxx
```

## 4. Provision runtime config into NVS

```bash
cp firmware/go2/.env.go2.example firmware/go2/.env.go2
# Edit GO2_ROBOT_ID=go2_03, GO2_STAGE_ID=stage_1, Wi-Fi/MQTT/OTA/tuning.
./.venv-pio/bin/python scripts/go2/provision.py --no-serial --print-json
./.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-xxxx
```

## 5. Inspect/change config without reflashing

```bash
./.venv-pio/bin/python scripts/go2/provision.py --command show-config --serial-port /dev/cu.usbserial-xxxx
./.venv-pio/bin/python scripts/go2/provision.py --command show-status --serial-port /dev/cu.usbserial-xxxx
./.venv-pio/bin/python scripts/go2/provision.py --command config --serial-port /dev/cu.usbserial-xxxx
./.venv-pio/bin/python scripts/go2/provision.py --command clear-config --serial-port /dev/cu.usbserial-xxxx
```

## Structure notes

- Arduino entrypoint/runtime orchestration: `firmware/go2/main.cpp`
- Build fallback constants: `firmware/go2/build_config.h`
- NVS bridge: `firmware/go2/config/runtime_config.*`
- MQTT/device-management bridge: `firmware/go2/mqtt/`
- Host provisioning helper: `scripts/go2/provision.py`

Hit/down and HP bar display policy now runs locally on the ESP. The ESP publishes `hit_event` plus device status fields (`accepted_hit_count`, `hp_remaining`, `max_hits`, `down`) for Command Center ingestion; `ring_display` is retained only for reset/debug compatibility.
