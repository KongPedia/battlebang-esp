# BattleBang ESP32

ESP32 펌웨어 저장소입니다. Go2 피격 ESP, 터렛, Nixo 등 장치별 펌웨어를 PlatformIO env로 나누어 빌드합니다.

BTB-671부터 Go2 피격 ESP는 Command Center와 MQTT로 직접 통신할 수 있습니다.

- ESP → Command Center: `battlebang/hit/{robot_id}/events`
  - `hit_candidate`
  - `heartbeat`
- Command Center → ESP: `battlebang/hit/{robot_id}/ring_display/command`
  - `ring_display`
- Jetson UART HP 경로는 Command Center/MQTT 응답이 없을 때 로컬 fallback 보험으로 남깁니다.

Go2 피격 펌웨어 구조:

- `src/main.cpp`: setup/loop 오케스트레이션만 담당
- `src/go2/config.h`: 핀, HP, MQTT topic, 빌드 설정
- `src/go2/robots.json`: Go2별 non-secret 프로필. `robot_id`, 피격 임계값, LED/센서 핀 등
- `src/go2/local_secrets.h`: Wi-Fi/MQTT secret. **gitignore 대상**
- `src/go2/led/led_ring.*`: 로컬 HP LED / Command Center ring display 렌더링
- `src/go2/fire/fire_control.*`: 서보/릴레이 발사 시퀀스
- `src/go2/hit/hit_sensor.*`: 피에조 ISR, ADC peak capture, debounce/cooldown
- `src/go2/game/game_state.*`: 로컬 fallback HP/dead 상태와 Jetson UART HP 송신
- `src/go2/command_center/command_center_mqtt.*`: ESP↔Command Center MQTT, hit_candidate/heartbeat/ring command

---

## ESP32 펌웨어 빌드 & 업로드 방법

ESP32 MCU에 올리는 흐름은 **빌드 → 바이너리 생성 → USB로 업로드** 한 번입니다.

### 1. Arduino IDE (가장 일반적)

1. **Arduino IDE 설치**
   https://www.arduino.cc/en/software

2. **ESP32 보드 추가**
   - `파일 → 환경설정` → "추가 보드 매니저 URL"에 아래 한 줄 추가:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - `도구 → 보드 → 보드 매니저`에서 "esp32" 검색 → **esp32 by Espressif Systems** 설치.

3. **라이브러리 설치**
   - `스케치 → 라이브러리 포함하기 → 라이브러리 관리`
   - "FastLED", "PubSubClient", "ArduinoJson" 설치.

4. **스케치로 열기**
   - Go2 피격 ESP는 `src/main.cpp`가 `src/go2/**` 모듈을 include합니다.
   - 현재 구조는 여러 파일로 분리되어 있으므로 Arduino IDE 단독보다 **PlatformIO 사용을 권장**합니다.

5. **보드·포트 선택**
   - `도구 → 보드` → **ESP32 Dev Module** (또는 사용 중인 ESP32 보드)
   - `도구 → 포트` → ESP32가 연결된 COM 포트 선택 (USB 케이블 연결 후 표시됨).

6. **업로드**
   - 상단 **업로드 버튼**(→ 화살표) 클릭.
   - 컴파일이 끝나면 자동으로 MCU로 업로드됩니다.
   - 업로드 시 **BOOT 버튼** 눌러야 하는 보드도 있음 (실패하면 한 번 시도).

정리: **한 번에 빌드되고, 그 결과가 한 번에 플래시에 업로드**됩니다. (기능별로 나눠서 올리는 방식 아님.)

---

### 2. ESP-IDF (커맨드라인, 고급)

ESP32 공식 툴체인으로 C/C++ 프로젝트를 빌드·플래시하는 방법입니다.

1. **ESP-IDF 설치**
   - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/
   - Windows: ESP-IDF Tools Installer 사용 권장.
   - macOS/Linux: `install.sh` 실행 후 `export.sh`로 환경 변수 설정.

2. **프로젝트 구성**
   - 이 프로젝트를 ESP-IDF용으로 쓰려면 `main/main.c` 또는 `main/main.cpp`로 옮기고, `CMakeLists.txt`에서 Arduino 대신 ESP-IDF 컴포넌트로 빌드하도록 설정해야 합니다.
   - **지금 소스는 Arduino (Arduino.h, FastLED) 기준**이므로, 그대로 쓰려면 **Arduino IDE 또는 PlatformIO**가 더 적합합니다.

3. **빌드·업로드 (ESP-IDF 프로젝트일 때)**
   ```bash
   idf.py set-target esp32
   idf.py build
   idf.py -p /dev/cu.usbserial-xxx flash monitor   # macOS. Windows는 COM 포트
   ```

즉, **지금 코드 그대로** 쓰실 거면 Arduino IDE를 쓰는 게 가장 간단합니다.

---

### 3. PlatformIO (선택)

VS Code 등에서 쓰는 경우:

- `platformio.ini`가 있으면 이 프로젝트를 **Open Project**로 열고
- **Build**, **Upload** 버튼으로 빌드·업로드 가능.
- 내부적으로도 **한 번 빌드 → 한 번 업로드**입니다.

Go2 피격 ESP MQTT 설정은 기존 터렛과 같은 방식입니다.

- `src/go2/robots.json`: 커밋 가능한 장치별 non-secret 프로필
- `src/go2/local_secrets.h`: 커밋하면 안 되는 Wi-Fi/MQTT secret
- `scripts/go2_config.py`: PlatformIO 빌드 시 profile/env를 C++ 매크로로 주입
- `scripts/go2_flash.py`: 터렛의 `scripts/turret_flash.py`처럼 robot id와 USB port 기준으로 빌드/업로드

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
ESP_WIFI_SSID="kongstudios" \
ESP_WIFI_PASSWORD="********" \
ESP_MQTT_HOST="<command-center-or-broker-ip>" \
pio run -e esp32dev_go2
```

---

## 요약

| 방법        | 빌드                    | 업로드                    |
|------------|-------------------------|----------------------------|
| Arduino IDE | 스케치 컴파일 (자동)    | 업로드 버튼 한 번           |
| ESP-IDF     | `idf.py build`          | `idf.py flash`             |
| PlatformIO  | `pio run`               | `pio run -t upload`        |

- **펌웨어는 항상 전체가 한 번에 빌드되고, 그 이미지를 MCU 플래시에 한 번에 업로드**합니다.
- Go2 피격 펌웨어는 `src/main.cpp` + `src/go2/**` 모듈을 함께 사용합니다.

---

## 단위 테스트 (게임 로직)

HP/밴드/데미지/명령 파싱 등 **순수 로직**을 `lib/game_logic`으로 분리해 호스트 PC에서 테스트합니다.

- **테스트 실행**
  - 스크립트 (Google Test 필요: `brew install googletest` 또는 conda 등):
    ```bash
    chmod +x tests/run_tests.sh && ./tests/run_tests.sh
    ```
  - PlatformIO 사용 시:
    ```bash
    pio test -e native
    ```
- **구조**: `tests/test_game_logic.cpp` (Google Test) + `lib/game_logic/` (테스트용 로직).
