# BattleBang ESP32

ESP32 펌웨어: 단일 파일 `src/main.cpp`. Jetson UART2, LED, 릴레이/서보 발사, 피격 센서, HP 등 모두 포함.

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

3. **FastLED 라이브러리 설치**  
   - `스케치 → 라이브러리 포함하기 → 라이브러리 관리` → "FastLED" 검색 후 설치.

4. **스케치로 열기**  
   - `src/main.cpp` 내용을 **통째로 복사**해서 새 스케치에 붙여넣기  
   - 또는 이 프로젝트 루트에 `battlebang_esp.ino` 파일을 만들고, 그 안에 `main.cpp`와 동일한 내용 넣은 뒤 `battlebang_esp.ino` 더블클릭해서 열기.

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

---

## 요약

| 방법        | 빌드                    | 업로드                    |
|------------|-------------------------|----------------------------|
| Arduino IDE | 스케치 컴파일 (자동)    | 업로드 버튼 한 번           |
| ESP-IDF     | `idf.py build`          | `idf.py flash`             |
| PlatformIO  | `pio run`               | `pio run -t upload`        |

- **펌웨어는 항상 전체가 한 번에 빌드되고, 그 이미지를 MCU 플래시에 한 번에 업로드**합니다.  
- 소스는 `src/main.cpp` 한 파일만 사용합니다.

---

## 단위 테스트 (게임 로직)

`main.cpp`의 HP/밴드/데미지/명령 파싱 등 **순수 로직**을 `lib/game_logic`으로 분리해 호스트 PC에서 테스트합니다.

- **테스트 실행**
  - 스크립트 (Google Test 필요: `brew install googletest` 또는 conda 등):
    ```bash
    chmod +x tests/run_tests.sh && ./tests/run_tests.sh
    ```
  - PlatformIO 사용 시:
    ```bash
    pio test -e native
    ```
- **구조**: `tests/test_game_logic.cpp` (Google Test) + `lib/game_logic/` (테스트용 로직). 펌웨어 `src/main.cpp`는 그대로 단일 파일 유지.
