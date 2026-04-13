# Turret firmware docs

현재 PlatformIO 기준 펌웨어 본체는 `src/turret/main.cpp`입니다.
`src/turret/main_turret.cpp`는 Arduino IDE에 붙여 넣는 단일 파일 export 용도로만 남겨두고, PIO 빌드에서는 제외합니다.

## 현재 기준 파일

- 펌웨어 entrypoint: `src/turret/main.cpp`
- 런타임 조각: `src/turret/runtime/*.inc`
  - `state.inc`: 전역 상태/상수/forward declaration
  - `support.inc`: 하드웨어, aiming, ADC, PID helper
  - `control.inc`: mode/fire/target/serial command
  - `network.inc`: Wi-Fi/MQTT/logging
- 빌드/로컬 설정 헤더: `src/turret/build_config.h`
- 터렛별 좌표/프리셋: `src/turret/turrets.json`
- 로컬 Wi-Fi/MQTT 시크릿: `src/turret/local_secrets.h` (`.gitignore`, 커밋 금지)
- PlatformIO 주입 스크립트: `scripts/turret_config.py`

`runtime/*.inc`는 독립 번역 단위가 아니라 `main.cpp`가 순서대로 include하는 구현 조각입니다. Arduino/PIO 전역 하드웨어 상태를 유지하면서 파일만 보기 좋게 나누기 위한 구조입니다.

## PlatformIO env

```text
esp32dev_turret_1
esp32dev_turret_2
esp32dev_turret_3
esp32dev_turret_4
esp32dev_turret_5
esp32dev_turret_6
```

현재 `turret_5`만 좌표가 설정되어 있습니다. 나머지는 `configured: false`라서 실수 빌드가 실패합니다.

## turret_5 빌드/업로드

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5
./.venv-pio/bin/pio run -e esp32dev_turret_5 -t upload --upload-port /dev/cu.usbserial-1130
```

가장 간단한 현장용 명령은:

```bash
./bin/turret ports
./bin/turret show 5
./bin/turret upload 5
./bin/turret upload 5 /dev/cu.usbserial-1130
```

여러 터렛을 USB 포트별로 순차 빌드/업로드하려면:

```text
src/turret/docs/build-upload-workflow.md
```

를 참고하고, 자동화 스크립트는 아래를 사용합니다.

```bash
python3 scripts/turret_flash.py list-ports
python3 scripts/turret_flash.py flash --target turret_5=/dev/cu.usbserial-1130
```

포트 확인:

```bash
./.venv-pio/bin/pio device list
```

## 설정 주입 우선순위

1. `scripts/turret_config.py`가 `src/turret/turrets.json`에서 터렛 ID/좌표를 build flag로 주입
2. `src/turret/build_config.h`가 `src/turret/local_secrets.h`를 include해서 Wi-Fi/MQTT 값을 가져옴
3. 필요하면 shell env로 Wi-Fi/MQTT 값을 빌드 시 직접 override 가능

예:

```bash
TURRET_WIFI_SSID='...' \
TURRET_WIFI_PASSWORD='...' \
TURRET_MQTT_HOST='...' \
./.venv-pio/bin/pio run -e esp32dev_turret_5
```

## 타겟이 너무 위를 볼 때

X/Y는 맞는데 전체적으로 위를 보면 우선 `../battlebang-demo/turrets.toml`의 해당 turret `target.z`를 낮춰 테스트합니다.
예: `turret_5.target.z = 0.5`에서 `0.4` 또는 `0.35`.

좌표계 자체가 맞고 pitch만 일정하게 높으면 `src/turret/turrets.json`의 해당 turret에 보정값을 추가할 수 있습니다.

```json
"pitch_offset_deg": -3.0
```
