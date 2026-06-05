# BattleBang ESP32

ESP32 펌웨어 저장소입니다. Go2-mounted ESP, 터렛 등 장치별 펌웨어를 PlatformIO env로 나누어 빌드합니다.

| PlatformIO env | Source entrypoint | Purpose |
| --- | --- | --- |
| `esp32dev`, `esp32dev_go2_*` | `src/go2_nixo/main.cpp` + `src/go2_nixo/**` | Go2-mounted hit sensor / ring LED + Nixo/game blaster fire firmware |
| `esp32dev_turret_*` | `src/turret/main.cpp` | Turret MQTT firmware variants |
| `esp32dev_turret_fleet` | `src/turret_fleet/main.cpp` | Generic runtime-configured turret fleet firmware with MQTT config + OTA |

ESP32 firmware uploads are full-flash images. Pick the correct PlatformIO environment before uploading; uploading one env replaces whatever firmware is currently flashed on that board.

---

## Go2-mounted ESP firmware summary

Go2-mounted ESP는 Command Center와 MQTT로 직접 통신합니다. Hit sensor/ring LED와 Nixo/game blaster fire가
`src/go2_nixo` firmware 하나로 통합되어 있습니다.

- ESP → Command Center: `battlebang/hit/{robot_id}/events`
  - `hit_candidate`
  - `heartbeat`
- Command Center → ESP: `battlebang/hit/{robot_id}/ring_display/command`
  - `ring_display`
- Command Center → ESP: `battlebang/nixo/{nixo_id}/command`
  - `fire`

Go2 피격 펌웨어 구조:

- `src/go2_nixo/main.cpp`: Arduino `setup/loop` 진입점 및 Go2 피격 ESP runtime 오케스트레이션
- `src/go2_nixo/build_config.h`: 핀, MQTT topic, 빌드 설정
- `src/go2_nixo/robots.json`: Go2별 non-secret 프로필. `robot_id`, LED/센서/Nixo relay 핀 등
- `src/go2_nixo/local_secrets.h`: Wi-Fi/MQTT secret. **gitignore 대상**
- `src/go2_nixo/piezo/piezo_sensor.*`: 피에조 ISR, ADC peak capture, debounce/cooldown
- `src/go2_nixo/ring_led/ring_display.*`: Command Center `ring_display` 렌더링과 fallback LED 표시
- `src/go2_nixo/mqtt/hit_mqtt_client.*`: MQTT hit_candidate/heartbeat publish, ring_display subscribe
- `src/go2_nixo/nixo/nixo_fire_client.*`: MQTT Nixo fire command subscribe, servo/relay fire sequence
- `src/go2_nixo/docs/`: 터렛 문서 구조와 맞춘 Go2 빌드/통신/fallback 문서

초기 설정:

```bash
cp src/go2_nixo/local_secrets.example.h src/go2_nixo/local_secrets.h
# src/go2_nixo/local_secrets.h 안의 Wi-Fi / MQTT broker 수정
```

Go2 5번 빌드/업로드:

```bash
pio run -e esp32dev_go2_05
pio run -e esp32dev_go2_05 -t upload --upload-port /dev/cu.usbserial-21130
```

터렛 flash 스크립트와 같은 방식으로도 실행할 수 있습니다.

```bash
python scripts/go2_flash.py show-config
python scripts/go2_flash.py flash --target go2_05=/dev/cu.usbserial-21130
```

`local_secrets.h`를 쓰지 않고 shell env로도 주입할 수 있습니다.

```bash
GO2_ID=go2_05 \
ESP_WIFI_SSID="YOUR_WIFI_SSID" \
ESP_WIFI_PASSWORD="YOUR_WIFI_PASSWORD" \
ESP_MQTT_HOST="<command-center-or-broker-ip>" \
pio run -e esp32dev_go2
```

Nixo/game blaster fire uses the same Go2 firmware and broker. Defaults:

- relay pin: `GPIO23`
- relay polarity: active-HIGH (`HIGH` = fire/on, `LOW` = off)
- second relay: disabled (`NIXO_RELAY2_PIN=-1`)
- live mapping: `go2_03 -> nixo_go2_03`
- MQTT topic: `battlebang/nixo/nixo_go2_03/command`

---

## Recommended build/upload: PlatformIO

From this repo root:

```bash
# Build combined Go2 hit/ring/Nixo fire ESP firmware.
pio run -e esp32dev_go2_05

# Upload to a specific connected board.
pio run -e esp32dev_go2_03 -t upload --upload-port /dev/cu.usbserial-1130

# Serial monitor after upload.
pio device monitor -p /dev/cu.usbserial-1130 -b 115200
```

For `turret_fleet`, prefer the repo-local PlatformIO venv and helper:

```bash
python3 -m venv .venv-pio
./.venv-pio/bin/python -m pip install -U platformio pyserial

./.venv-pio/bin/pio run -e esp32dev_turret_fleet
./bin/turret fleet-upload 2 /dev/cu.usbserial-120

# MQTT_BROKER_HOST is the Command Center/MQTT broker, not the ESP device IP.
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
./bin/turret fleet-mqtt turret_2 target 0 0 0.7 --host "$MQTT_BROKER_HOST"
./bin/turret fleet-e2e turret_2 --host "$MQTT_BROKER_HOST" --allow-live-fire
```

The fleet firmware is a single generic image. First provisioning over USB stores
`turret_id`, Wi-Fi, MQTT, pose, calibration, motion/fire, and OTA policy in ESP
NVS. After that, Command Center can update config and command `target`, `idle`,
`dead`, `home`, `recover`, and OTA jobs over MQTT without reflashing.
For `turret_1` through `turret_4`, `fleet-upload` automatically overlays the
matching `src/turret_fleet/profiles/<turret>.json` and
`src/turret_fleet/pattern_presets/<turret>.json` files before writing NVS.

---

## Arduino IDE path

Arduino IDE is useful for simple single-sketch experiments, but this repository is now a multi-environment PlatformIO workspace. PlatformIO is the recommended path for Go2, Nixo, and turret firmware.

1. Install Arduino IDE: https://www.arduino.cc/en/software
2. Add ESP32 board support URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Install **esp32 by Espressif Systems** and required libraries such as **FastLED**, **PubSubClient**, and **ArduinoJson**.
4. If copying files into Arduino IDE manually, also port the relevant PlatformIO build flags, local secrets, and libraries.

---

## ESP-IDF note

The current sources are Arduino-style C++ (`Arduino.h`, FastLED, PubSubClient/ArduinoJson for MQTT paths). If you want a pure ESP-IDF project, create a separate ESP-IDF component layout and port the Arduino dependencies deliberately. For this repo as-is, PlatformIO is the supported command-line build/upload path.

---

## Unit tests

- Native C++ tests:
  ```bash
  pio test -e native
  ```
- Python tests:
  ```bash
  python3 -m venv .venv-turret-tests
  ./.venv-turret-tests/bin/python -m pip install -r tests/python/requirements.txt
  ./.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py -q
  ```
