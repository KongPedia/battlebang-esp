# BattleBang ESP32

ESP32 펌웨어 모노레포입니다. 활성 펌웨어는 `firmware/` 아래에 두고, 공통 NVS/MQTT/OTA/Wi-Fi 유틸은 `lib/bb_esp_*` PlatformIO 라이브러리로 조립합니다.

| PlatformIO env | Source entrypoint | Purpose | Runtime config |
| --- | --- | --- | --- |
| `esp32dev_go2` | `firmware/go2/main.cpp` | Go2-mounted hit/LED ESP: piezo AO ADC threshold + Command Center HP bar display | NVS: identity, stage/group/location, Wi-Fi, MQTT, OTA, hit tuning |
| `esp32dev_go2_nixo`, `esp32dev_go2_nixo_1ch`, `esp32dev_go2_nixo_2ch` | `firmware/go2_nixo/main.cpp` | Optional one-ESP fallback: hit/LED + Nixo relay | NVS: same as Go2 plus Nixo identity/topic and fire timing; relay pins/polarity/channel count stay build variant |
| `esp32dev_boss_target` | `firmware/boss_target/main.cpp` | Boss target firmware | NVS/MQTT/OTA standard template |
| `esp32dev_heavy_blaster` | `firmware/heavy_blaster/main.cpp` | Heavy blaster firmware | NVS/MQTT/OTA standard template |
| `esp32dev_turret_fleet` | `firmware/turret_fleet/main.cpp` | Generic runtime-configured turret fleet firmware | NVS: turret/device/stage identity, Wi-Fi, MQTT, motion/fire config, OTA |
| `esp32dev_nixo`, `esp32dev_nixo_1ch`, `esp32dev_nixo_2ch` | `src/nIxo/main.cpp` | Retired standalone Nixo relay ESP kept for compatibility | Build-time relay variant |
| `esp32dev_hit_target` | `src/hit_target/main.cpp` | Retired standalone circular hit-target firmware | Legacy/runtime config retained but not active path |
| `esp32dev_turret_*` | `src/turret/main.cpp` | Legacy turret variants | Legacy |

ESP32 uploads are full-flash images. Pick the correct PlatformIO environment before uploading; uploading one env replaces whatever firmware is currently flashed on that board.

## Active Go2/Nixo firmware layout

현재 Go2는 **runtime-provisioned generic image** 방식입니다. `go2_01`, `go2_02`, `go2_03` 같은 robot id는 PlatformIO env 이름이 아니라 ESP32 NVS에 들어가는 사용자 지정 runtime identity입니다. 같은 `esp32dev_go2` 이미지를 굽고, 이후 serial/MQTT config로 `robot_id`, `device_id`, `stage_id` 등을 바꿉니다.

- Go2 hit/LED ESP: `firmware/go2/`
  - piezo **AO ADC threshold** 기반 `hit_candidate` publish
  - Command Center `ring_display`/HP bar 렌더링
  - 빌드/업로드 env: `esp32dev_go2`
  - NVS 튜닝: `robot_id`, `hit_topic_prefix`, piezo threshold/rearm/capture/debug/rearm-stable, hit cooldown, offline queue, LED brightness
- Optional one-ESP fallback/reference: `firmware/go2_nixo/`
  - hit/LED/Nixo relay가 한 ESP에 통합된 경로
  - 빌드/업로드 env: `esp32dev_go2_nixo`(default 1ch), `esp32dev_go2_nixo_1ch`, `esp32dev_go2_nixo_2ch`
  - NVS 튜닝: Go2 hit 튜닝 + `nixo_id`, `nixo_command_topic_prefix`, ring brightness, Nixo fire duration/cooldown/prefire/relay delay
  - relay pin/polarity/channel count는 안전상 build variant/hardware profile에 남깁니다.
- Standalone Nixo `src/nIxo/`는 현재 active path가 아니며 compatibility env만 유지합니다.

Canonical upload/provision flow:

```bash
# Go2 hit/LED generic image
./.venv-pio/bin/pio run -e esp32dev_go2 -t upload --upload-port /dev/cu.usbserial-XXXX
cp firmware/go2/.env.go2.example firmware/go2/.env.go2
# edit GO2_ROBOT_ID=go2_03, GO2_STAGE_ID=stage_1, Wi-Fi/MQTT/OTA/tuning
./.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-XXXX

# Optional integrated Go2+Nixo 2ch fallback
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch -t upload --upload-port /dev/cu.usbserial-ZZZZ
cp firmware/go2_nixo/.env.go2_nixo.example firmware/go2_nixo/.env.go2_nixo
# edit GO2_NIXO_ROBOT_ID=go2_03, GO2_NIXO_NIXO_ID=nixo_go2_03, GO2_NIXO_STAGE_ID=stage_1
./.venv-pio/bin/python scripts/go2_nixo/provision.py --serial-port /dev/cu.usbserial-ZZZZ
```

## Go2 hit/LED ESP firmware summary

Go2 hit/LED ESP는 Command Center와 MQTT로 직접 통신합니다. 이 펌웨어는 발사/릴레이/서보를 하지 않고, AO ADC threshold를 넘은 피에조 입력을 `hit_candidate`로 보낸 뒤 서버의 `ring_display` 명령만 렌더링합니다.

- ESP → Command Center: `battlebang/hit/{robot_id}/events`
  - `hit_candidate`
  - `heartbeat`
- Command Center → ESP: `battlebang/hit/{robot_id}/ring_display/command`
  - `ring_display`
- Device management: `battlebang/devices/{device_id}/status|config|ota`

Go2 hit/LED 펌웨어 구조:

- `firmware/go2/main.cpp`: Arduino `setup/loop` 진입점 및 Go2 hit/LED ESP runtime 오케스트레이션
- `firmware/go2/build_config.h`: hardware fallback defaults only; robot id is not selected at build time
- `firmware/go2/hardware_profile.json`: non-secret build-time hardware defaults (pins, LED count/capacity, factory fallback threshold)
- `firmware/go2/.env.go2.example`: serial provisioning defaults for NVS
- `firmware/go2/config/`: runtime config bridge to common NVS/MQTT/OTA schema
- `firmware/go2/display/`: Command Center `ring_display` 렌더링과 fallback LED 표시
- `firmware/go2/mqtt/`: MQTT hit_candidate/heartbeat publish, ring_display subscribe, device config/OTA subscribe
- `firmware/go2/docs/`: Go2 hit/LED 빌드/통신 문서

NVS로 바꾸는 값 기준:

- 자주 바뀌거나 현장 튜닝하는 값: `robot_id`, `device_id`, `group`, `stage_id`, `location`, Wi-Fi, MQTT, OTA policy, `hit_topic_prefix`, piezo threshold/rearm/capture/debug/rearm-stable, hit cooldown, offline queue capacity/flush interval, LED brightness
- build-time으로 남기는 값: 실제 GPIO pin, LED 물리 count/capacity, relay pin/polarity/channel count 같은 하드웨어 안전 envelope

기본 핀맵 (`firmware/go2/hardware_profile.json` defaults 기준):

| Part | Pin | Runtime? |
| --- | --- | --- |
| HP bar LED data | `GPIO18` | No, build hardware profile |
| Piezo AO | `GPIO34` | No, build hardware profile |
| Piezo D0 debug readback | `GPIO27` | No, build hardware profile |
| Piezo threshold/rearm | defaults `200`/`150` raw | Yes, NVS tuning |

### Recommended build/upload: PlatformIO

```bash
python3 -m venv .venv-pio
./.venv-pio/bin/python -m pip install -U platformio pyserial

# Build active firmware
./.venv-pio/bin/pio run -e esp32dev_go2
./.venv-pio/bin/pio run -e esp32dev_go2_nixo
./.venv-pio/bin/pio run -e esp32dev_boss_target
./.venv-pio/bin/pio run -e esp32dev_heavy_blaster
./.venv-pio/bin/pio run -e esp32dev_turret_fleet

# Upload + provision Go2 identity/config into NVS
./.venv-pio/bin/pio run -e esp32dev_go2 -t upload --upload-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-XXXX

# Serial monitor after upload.
./.venv-pio/bin/pio device monitor -p /dev/cu.usbserial-XXXX -b 115200
```

For `turret_fleet`, prefer the repo-local PlatformIO venv and helper:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
./bin/turret fleet-upload 2 /dev/cu.usbserial-120

# MQTT_BROKER_HOST is the Command Center/MQTT broker, not the ESP device IP.
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
./bin/turret fleet-mqtt turret_2 target 0 0 0.7 --host "$MQTT_BROKER_HOST"
./bin/turret fleet-e2e turret_2 --host "$MQTT_BROKER_HOST" --allow-live-fire
```

The fleet firmware is a single generic image. First provisioning over USB stores `turret_id`, `device_id`, `stage_id`, Wi-Fi, MQTT, pose, calibration, motion/fire, and OTA policy in ESP NVS. After that, Command Center can update config and command `target`, `idle`, `dead`, `home`, `recover`, and OTA jobs over MQTT without reflashing.

---

## Generic standalone hit target firmware

`src/hit_target/` is the generic circular hit-target firmware for a piezo sensor plus WS2812B-style LED ring. It is intentionally separate from the Go2-mounted `firmware/go2/` hit/LED firmware and the optional integrated `firmware/go2_nixo/` fallback:

- `firmware/go2/`: Go2-mounted hit/LED ESP, MQTT-controlled by Command Center, robot/device/stage identity comes from NVS provisioning.
- `firmware/go2_nixo/`: optional one-ESP integrated fallback, MQTT-controlled by Command Center.
- `src/hit_target/`: standalone/tutorial hit target, local HP/effect loop, `target_id` is derived at boot from the ESP32 eFuse MAC address (for example `hit_target_AABBCCDDEEFF`).

Implemented local UX:

- ready state starts as a full green circle
- HP uses large readable color phases: green 5 hits, yellow 5 hits, red 5 hits by default; each accepted hit removes 1/5 of the current color ring instead of a tiny `1 / max_hits` slice
- while green/yellow phases are partially damaged, the removed rear arc shows a dim next-phase color with black gap LEDs at both boundaries; phase transitions keep that existing dim next-phase rear arc visible while the last removed chunk wipes; the next color grows behind that shrinking chunk, then promotes into the next full ring
- compact feedback: no orbit, no full-ring hit flash, no remaining-HP pulse; accepted hits only decrement HP and run the faster red/orange recent-damage chip
- activation can be `always_on` for standalone/Go2-style targets or `linked_device`, which follows a linked device status topic; kind `turret` follows turret command/pattern status
- final HP0 publishes `destroyed=true` plus `linked_device_kind`/`linked_device_id`; Command Center can subscribe to that status and send `dead` when the linked device kind is `turret`
- lock-term input ignore, final HP chunk wipe, short blackout beat, longer rainbow defeat sweep, and blackout
- ESP32 BOOT/GPIO0 long-press initialize/reset in addition to USB serial reset
- JSON-line USB serial events with MAC-derived `target_id` / `device_mac` for bench debugging and future controller parsing
- DO-only piezo input uses configurable multi-edge capture/debounce (`digital_hit_min_edges`, `digital_isr_debounce_us`) to balance sensitivity against idle comparator noise
- NVS-backed runtime config: `show-config`, `config {json}`, `provision {json}`, and `clear-config` let HP/effect/sensor/network/OTA values change without rebuilding
- MQTT remote config/status/commands on `battlebang/devices/{device_id}/...` and `battlebang/hit_targets/{target_id}/...` topics
- hit-target-specific OTA manifests use app `battlebang-hit-target`, hardware `esp32dev-hit-target-ring-v1`, stable tag `hit-target-latest`, and asset `hit-target-manifest.json` so they do not collide with turret fleet `turret-fleet-latest/manifest.json`

Build/upload:

```bash
./.venv-pio/bin/pio run -e esp32dev_hit_target
./.venv-pio/bin/pio run -e esp32dev_hit_target -t upload --upload-port /dev/cu.usbserial-XXXX
./.venv-pio/bin/pio device monitor -p /dev/cu.usbserial-XXXX -b 115200
```

Local Wi-Fi / Command Center / MQTT settings are folder-owned, like `turret_fleet`:

```bash
cp src/hit_target/.env.hit_target.example src/hit_target/.env.hit_target
# Edit HIT_TARGET_WIFI_* and HIT_TARGET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS.
./.venv-pio/bin/python scripts/hit_target/provision.py --print-json --no-serial
./.venv-pio/bin/python scripts/hit_target/provision.py --serial-port /dev/cu.usbserial-XXXX
```

`src/hit_target/` is legacy/unused for the current active firmware plan. Active firmware OTA publishing is handled by the unified Firmware OTA Releases workflow with firmware-specific stable release tags instead of the repo-wide latest release URL.

Default pins are migrated from the wall-mounted target bench sketch and can be overridden while debugging hardware:

- LED data: GPIO18
- LED ring: 60 LEDs, WS2812B, GRB color order
- piezo DO: GPIO27
- piezo AO: disabled by default (`-1`; set an ADC1 GPIO such as GPIO34 if wired)
- reset/initialize button: ESP32 BOOT/GPIO0 long-press after firmware boot
- HP phases/hits-per-phase, activation mode, AO threshold, DO noise/sensitivity filtering, cooldown, damage-chip speed, and defeat timing are factory defaults in `src/hit_target/config.json`; after provisioning, NVS runtime config is the source of truth.
- FastLED hardware-profile fields (`led.pin`, `led.type`, `led.color_order`) remain build/profile-bound; remote config can tune gameplay/sensor/brightness/LED count up to the compiled capacity.

See `src/hit_target/README.md` for serial commands, MQTT topics, OTA manifest rules, and `BATTLEBANG_HIT_TARGET_*` factory-default overrides.

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
