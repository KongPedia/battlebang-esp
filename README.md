# BattleBang ESP32

ESP32 펌웨어 모노레포입니다. 활성 펌웨어는 `firmware/` 아래에 두고, 공통 NVS/MQTT/OTA/Wi-Fi 유틸은 `lib/bb_esp_*` PlatformIO 라이브러리로 조립합니다.

- **펌웨어 선택·운영 문서:** [firmware index](firmware/README.md), [hardware assembly and power](BattleBang-HardwareKit-v1.0.md)
- **Codex/LLM 개발:** [AGENTS.md](AGENTS.md) → [.agents workflow and selected plugins](.agents/README.md) → [C++ engineering criteria](.agents/references/engineering.md)
- **기존 경로:** [legacy/compatibility index](src/README.md). 새 작업은 활성 family를 선택합니다.

`platformio.ini`의 기본 env는 아직 legacy `esp32dev_turret_5`입니다. 항상 `-e <env>`를 명시하세요.
문서·프롬프트만 바꾸는 작업에는 빌드/테스트/장치 접속이 필요하지 않습니다.
업로드·NVS provisioning·OTA/reboot와 실제 이동·발사는 별도 승인 범위입니다. 아래 운영 명령은 자동 실행 지시가 아닙니다.
업로드/재부팅/serial reset 자체로 boot probe/HOME이 동작할 수 있으므로, 해당 부팅 이동까지 승인받거나 물리 출력을 격리·확인해야 합니다.

| PlatformIO env | Source entrypoint | Purpose | Runtime config |
| --- | --- | --- | --- |
| `esp32dev_go2` | `firmware/go2/main.cpp` | Go2-mounted hit/LED ESP: local piezo hit/HP/down + ESP-owned HP bar display | NVS: identity, stage/group/location, Wi-Fi, MQTT, OTA, hit tuning |
| `esp32dev_go2_nixo`, `esp32dev_go2_nixo_1ch`, `esp32dev_go2_nixo_2ch` | `firmware/go2_nixo/main.cpp` | Go2/Nixo single-character forced-newline UART firmware | NVS: same as Go2 plus Nixo identity/topic and fire timing; relay pins/polarity/channel count stay build variant |
| `esp32dev_go2_nixo_framed_packet_uart_1ch`, `esp32dev_go2_nixo_framed_packet_uart_2ch` | `firmware/go2_nixo_framed_packet_uart/main.cpp` | Refactored Go2/Nixo framed packet UART firmware | Reuses Go2/Nixo NVS and relay hardware variants |
| `esp32dev_go2_nixo_framed_packet_usb_1ch`, `esp32dev_go2_nixo_framed_packet_usb_2ch` | `firmware/go2_nixo_framed_packet_uart/main.cpp` | Production Go2/Nixo framed packet over the board USB connector | USB is binary-only at runtime; Bluetooth/MQTT management remains available |
| `esp32dev_boss_target` | `firmware/boss_target/main.cpp` | Boss target firmware | NVS/MQTT/OTA standard template |
| `esp32dev_heavy_blaster` | `firmware/heavy_blaster/main.cpp` | Heavy blaster firmware | NVS/MQTT/OTA standard template |
| `esp32dev_station` | `firmware/station/main.cpp` | Target Station firmware | NVS: station/device/stage identity, network, capture tuning, OTA |
| `esp32dev_turret_fleet` | `firmware/turret_fleet/main.cpp` | Generic runtime-configured turret fleet firmware | NVS: turret/device/stage identity, Wi-Fi, MQTT, motion/fire config, OTA |
| `esp32dev_nixo`, `esp32dev_nixo_1ch`, `esp32dev_nixo_2ch` | `src/nIxo/main.cpp` | Retired standalone Nixo relay ESP kept for compatibility | Build-time relay variant |
| `esp32dev_hit_target` | `src/hit_target/main.cpp` | Retired standalone circular hit-target firmware | Legacy/runtime config retained but not active path |
| `esp32dev_turret_*` | `src/turret/main.cpp` | Legacy turret variants | Legacy |

Uploading a different env replaces the running application. Reconfirm board, env/variant, port, NVS compatibility and rollback before an approved upload; do not assume flashing preserves or erases all NVS/partitions.

## Active Go2/Nixo firmware layout

현재 Go2는 **runtime-provisioned generic image** 방식입니다. `go2_01`, `go2_02`, `go2_03` 같은 robot id는 PlatformIO env 이름이 아니라 ESP32 NVS에 들어가는 사용자 지정 runtime identity입니다. 같은 `esp32dev_go2` 이미지를 굽고, 이후 serial/MQTT config로 `robot_id`, `device_id`, `stage_id` 등을 바꿉니다.

- Go2 hit/LED ESP: `firmware/go2/`
  - piezo **AO ADC threshold** 기반 ESP 로컬 hit accept + HP/down/LED bar 처리
  - Command Center에는 `hit_event`/device `status`로 `accepted_hit_count`, `hp_remaining`, `max_hits`, `down` publish
  - 빌드/업로드 env: `esp32dev_go2`
  - NVS 튜닝: `robot_id`, `hit_topic_prefix`, piezo threshold/rearm/capture/debug/rearm-stable, hit cooldown, `max_hits`, `hit_flash_ms`, offline queue, LED brightness
- Optional one-ESP fallback/reference: `firmware/go2_nixo/`
  - hit/LED/Nixo relay가 한 ESP에 통합된 경로
  - 단일 문자 강제 개행 env: `esp32dev_go2_nixo`(default 2ch), `esp32dev_go2_nixo_1ch`, `esp32dev_go2_nixo_2ch`
  - production USB packet env: `esp32dev_go2_nixo_framed_packet_usb_1ch`, `esp32dev_go2_nixo_framed_packet_usb_2ch`
  - GPIO/debug env: `esp32dev_go2_nixo_framed_packet_uart_1ch`, `esp32dev_go2_nixo_framed_packet_uart_2ch` (`Serial2` packet link + USB logs/commands)
  - NVS 튜닝: Go2 hit 튜닝 + `nixo_id`, `nixo_command_topic_prefix`, ring brightness, Nixo fire duration/cooldown/prefire/relay delay
  - relay pin/polarity/channel count는 안전상 build variant/hardware profile에 남깁니다.
- Standalone Nixo `src/nIxo/`는 현재 active path가 아니며 compatibility env만 유지합니다.

### Separately approved upload/provision flow

다음은 장치별 승인 후 사용하는 운영 예시입니다. 실제 port/ID/variant를 다시 확인하고, 기존 NVS/보정값과 복구 경로를 보존하세요.
소프트웨어 빌드 승인만으로 업로드·NVS 변경·재부팅·구동이 승인되지는 않습니다.

```bash
# Go2 hit/LED generic image
./.venv-pio/bin/pio run -e esp32dev_go2 -t upload --upload-port /dev/cu.usbserial-XXXX
cp firmware/go2/.env.go2.example firmware/go2/.env.go2
# edit GO2_ROBOT_ID=go2_03, GO2_STAGE_ID=stage_1, Wi-Fi/MQTT/OTA/tuning
./.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-XXXX

# Optional integrated Go2+Nixo 2ch fallback
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch -t upload --upload-port /dev/cu.usbserial-ZZZZ
# Debug/service build: USB Serial Monitor + text commands, Jetson packet link on Serial2.
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_framed_packet_uart_2ch -t upload --upload-port /dev/cu.usbserial-ZZZZ
cp firmware/go2_nixo/.env.go2_nixo.example firmware/go2_nixo/.env.go2_nixo
# edit GO2_NIXO_ROBOT_ID=go2_03, GO2_NIXO_NIXO_ID=nixo_go2_03, GO2_NIXO_STAGE_ID=stage_1
./.venv-pio/bin/python scripts/go2_nixo/provision.py \
  --serial-port /dev/cu.usbserial-ZZZZ \
  --firmware framed_packet_uart \
  --jetson-transport usb_serial \
  --management-transport uart2_usb_debug \
  --relay-variant relay_2ch
# Production build last. After boot the USB port is binary-only.
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_framed_packet_usb_2ch -t upload --upload-port /dev/cu.usbserial-ZZZZ
```

Provision with the UART2 maintenance/debug image (`esp32dev_go2_nixo_framed_packet_uart_2ch`) already flashed; `--management-transport uart2_usb_debug` declares that prerequisite, not a production USB text mode. Then flash the production USB build. Both builds keep the ESP bootloader USB upload path, but the production build intentionally disables USB text commands and application logs so they cannot corrupt framed packets. Do not feed the ESP 5 V rail from both the expansion-board regulator and Jetson USB unless the hardware explicitly isolates those sources.

## Go2 hit/LED ESP firmware summary

Go2 hit/LED ESP는 Command Center와 MQTT로 직접 통신합니다. 이 펌웨어는 발사/릴레이/서보를 하지 않고, AO ADC threshold를 넘은 피에조 입력을 ESP 로컬 hit로 accept한 뒤 HP/down 상태와 HP bar LED를 직접 갱신합니다. Command Center는 ESP가 publish하는 `hit_event`/device `status`를 ingest합니다.

- ESP → Command Center: `battlebang/hit/{robot_id}/events`
  - `hit_event`
  - `heartbeat`
- ESP → Command Center: `battlebang/devices/{device_type}/{device_id}/status`
  - `hp_remaining`, `max_hits`, `down`, nested `combat` facet
- Command Center → ESP: `battlebang/devices/{device_type}/{device_id}/config|ota`
- Command Center → ESP compatibility: `battlebang/hit/{robot_id}/ring_display/command`
  - `reset_hit_state` or `debug_override` only

Go2 hit/LED 펌웨어 구조:

- `firmware/go2/main.cpp`: Arduino `setup/loop` 진입점 및 Go2 hit/LED ESP runtime 오케스트레이션
- `firmware/go2/build_config.h`: hardware fallback defaults only; robot id is not selected at build time
- `firmware/go2/hardware_profile.json`: non-secret build-time hardware defaults (pins, LED count/capacity, factory fallback threshold)
- `firmware/go2/.env.go2.example`: serial provisioning defaults for NVS
- `firmware/go2/config/`: runtime config bridge to common NVS/MQTT/OTA schema
- `firmware/go2/display/`: ESP-owned HP bar 렌더링과 debug override 표시
- `firmware/go2/mqtt/`: MQTT hit_event/heartbeat/status publish, config/OTA/reset/debug subscribe
- `firmware/go2/docs/`: Go2 hit/LED 빌드/통신 문서

NVS로 바꾸는 값 기준:

- 자주 바뀌거나 현장 튜닝하는 값: `robot_id`, `device_id`, `group`, `stage_id`, `location`, Wi-Fi, MQTT, OTA policy, `hit_topic_prefix`, piezo threshold/rearm/capture/debug/rearm-stable, hit cooldown, `max_hits`, `hit_flash_ms`, offline queue capacity/flush interval, LED brightness
- build-time으로 남기는 값: 실제 GPIO pin, LED 물리 count/capacity, relay pin/polarity/channel count 같은 하드웨어 안전 envelope

기본 핀맵 (`firmware/go2/hardware_profile.json` defaults 기준):

| Part | Pin | Runtime? |
| --- | --- | --- |
| HP bar LED data | `GPIO18` | No, build hardware profile |
| Piezo AO | `GPIO34` | No, build hardware profile |
| Piezo D0 debug readback | `GPIO27` | No, build hardware profile |
| Piezo threshold/rearm | defaults `200`/`150` raw | Yes, NVS tuning |

### Setup and software-only builds: PlatformIO

```bash
python3 -m venv .venv-pio
./.venv-pio/bin/python -m pip install -U platformio pyserial

# Build active firmware
./.venv-pio/bin/pio run -e esp32dev_go2
./.venv-pio/bin/pio run -e esp32dev_go2_nixo
./.venv-pio/bin/pio run -e esp32dev_boss_target
./.venv-pio/bin/pio run -e esp32dev_heavy_blaster
./.venv-pio/bin/pio run -e esp32dev_station
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
```

Turret Fleet setup, approved upload and operator commands live in [its usage guide](firmware/turret_fleet/docs/usage.md).
The MQTT host is the MQTT broker / Command Center host, **not** the ESP IP:

```bash
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
```

Use `"$MQTT_BROKER_HOST"` only for separately authorized live operations; a no-fire target can still move hardware.

The fleet firmware is a single generic image. First provisioning over USB stores `turret_id`, `device_id`, `stage_id`, Wi-Fi, MQTT, pose, calibration, motion/fire, and OTA policy in ESP NVS. After that, Command Center can update config and command `target`, `idle`, `dead`, `home`, `recover`, and OTA jobs over MQTT without reflashing.

---

## Generic standalone hit target firmware (legacy)

`src/hit_target/` is retained standalone circular-target compatibility code, not the active Go2-mounted path.
Its local HP/effect, pin/profile, serial/MQTT/NVS and prototype OTA details remain in
[the owning README](src/hit_target/README.md); see [legacy scope](src/README.md) before maintenance.
Do not use it as the template for new firmware or infer current hardware validation from historical records.

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

## Offline software checks (application changes only)

Select the owning tests; do not inherit `TURRET_RUN_HW_TESTS=1` or run live integration suites by accident.
Host tests, Python string contracts, target builds and physical validation prove different things.
See [agent verification policy](AGENTS.md#environments-and-verification).

- Framed production protocol/runtime: `./.venv-pio/bin/pio test -e native_go2_nixo_uart`
- Direct game-logic harness: `tests/run_tests.sh` (existing host GoogleTest required)
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
