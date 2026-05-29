# BattleBang ESP32

ESP32 펌웨어 저장소입니다. Go2 피격 ESP, Nixo/game blaster, 터렛 등 장치별 펌웨어를 PlatformIO env로 나누어 빌드합니다.

| PlatformIO env | Source entrypoint | Purpose |
| --- | --- | --- |
| `esp32dev`, `esp32dev_go2_*` | `src/go2/main.cpp` + `src/go2/**` | Go2-mounted hit sensor / ring LED firmware (BTB-671) |
| `esp32dev_nixo` | `src/nIxo/main.cpp` | Go2-mounted Nixo/game blaster MQTT fire firmware (BTB-633) |
| `esp32dev_turret_*` | `src/turret/main.cpp` | Turret MQTT firmware variants |
| `esp32dev_turret_fleet` | `src/turret_fleet/main.cpp` | Generic runtime-configured turret fleet firmware with MQTT config + OTA |

ESP32 firmware uploads are full-flash images. Pick the correct PlatformIO environment before uploading; uploading one env replaces whatever firmware is currently flashed on that board.

---

## Go2 피격 ESP firmware summary (BTB-671)

Go2 피격 ESP는 Command Center와 MQTT로 직접 통신합니다.

- ESP → Command Center: `battlebang/hit/{robot_id}/events`
  - `hit_candidate`
  - `heartbeat`
- Command Center → ESP: `battlebang/hit/{robot_id}/ring_display/command`
  - `ring_display`
- Jetson UART HP 경로는 Command Center/MQTT 응답이 없을 때 로컬 fallback 보험으로 남깁니다.

Go2 피격 펌웨어 구조:

- `src/go2/main.cpp`: Arduino `setup/loop` 진입점 및 Go2 피격 ESP runtime 오케스트레이션
- `src/go2/build_config.h`: 핀, HP, MQTT topic, 빌드 설정
- `src/go2/robots.json`: Go2별 non-secret 프로필. `robot_id`, 피격 임계값, LED/센서 핀 등
- `src/go2/local_secrets.h`: Wi-Fi/MQTT secret. **gitignore 대상**
- `src/go2/sensors/piezo_sensor.*`: 피에조 ISR, ADC peak capture, debounce/cooldown
- `src/go2/display/ring_display.*`: Command Center `ring_display` 렌더링과 fallback LED 표시
- `src/go2/mqtt/hit_mqtt_client.*`: MQTT hit_candidate/heartbeat publish, ring_display subscribe
- `src/go2/fallback/offline_hit_fallback.*`: Command Center/MQTT 미응답 시에만 쓰는 로컬 fallback HP/down 상태와 Jetson UART HP 송신
- 발사/릴레이/서보 제어는 Go2 피격 ESP가 아니라 `src/nIxo/` 펌웨어가 담당
- `src/go2/docs/`: 터렛 문서 구조와 맞춘 Go2 빌드/통신/fallback 문서

초기 설정:

```bash
cp src/go2/local_secrets.example.h src/go2/local_secrets.h
# src/go2/local_secrets.h 안의 Wi-Fi / MQTT broker 수정
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

---

## Nixo / game blaster firmware summary (BTB-633)

The production Nixo path is `src/nIxo/main.cpp` built with `esp32dev_nixo`.

Current hardware invariant discovered during bench debugging:

- Real relay pin: `GPIO23`
- Relay polarity: active-HIGH (`HIGH` = fire/on, `LOW` = off)
- Second relay: disabled (`NIXO_RELAY2_PIN=-1`)
- Live mapping: `go2_03 -> nixo_go2_03`
- MQTT topic: `battlebang/nixo/nixo_go2_03/command`

The older `src/nIxo/BluetoothSerial.cpp` file is a Bluetooth-only baseline/smoke sketch. It does not read USB Serial commands, so use Bluetooth SPP or the MQTT firmware's own local/debug inputs when comparing against it.

For Nixo-specific secrets, hardware pins, MQTT topic, and smoke-test steps, see `src/nIxo/README.md`.

---

## Recommended build/upload: PlatformIO

From this repo root:

```bash
# Build Go2 hit ESP firmware.
pio run -e esp32dev_go2_05

# Build the Go2-mounted Nixo firmware.
pio run -e esp32dev_nixo

# Upload to a specific connected board.
pio run -e esp32dev_nixo -t upload --upload-port /dev/cu.usbserial-1130

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
```

The fleet firmware is a single generic image. First provisioning over USB stores
`turret_id`, Wi-Fi, MQTT, pose, calibration, motion/fire, and OTA policy in ESP
NVS. After that, Command Center can update config and command `target`, `idle`,
`dead`, `home`, `recover`, and OTA jobs over MQTT without reflashing.

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
