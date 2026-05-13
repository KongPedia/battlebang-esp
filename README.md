# BattleBang ESP32

BattleBang ESP32 firmware workspace. The repository now contains multiple firmware targets that are built as separate
PlatformIO environments:

| PlatformIO env | Source entrypoint | Purpose |
| --- | --- | --- |
| `esp32dev` | `src/main.cpp` | Legacy HP / Jetson UART2 / Bluetooth / LED / relay-servo firmware |
| `esp32dev_nixo` | `src/nIxo/main.cpp` | Go2-mounted Nixo/game blaster MQTT fire firmware (BTB-633) |
| `esp32dev_turret_*` | `src/turret/...` | Turret MQTT firmware variants |

ESP32 firmware uploads are full-flash images. Pick the correct PlatformIO environment before uploading; uploading
`esp32dev_nixo` replaces the currently flashed turret/root firmware on that board, and vice versa.

---

## Recommended build/upload: PlatformIO

From this repo root:

```bash
# Build the default legacy firmware.
pio run -e esp32dev

# Build the Go2-mounted Nixo firmware.
pio run -e esp32dev_nixo

# Upload to a specific connected board.
pio run -e esp32dev_nixo -t upload --upload-port /dev/cu.usbserial-1130

# Serial monitor after upload.
pio device monitor -p /dev/cu.usbserial-1130 -b 115200
```

For Nixo-specific secrets, hardware pins, MQTT topic, and smoke-test steps, see `src/nIxo/README.md`.

---

## Nixo / game blaster firmware summary (BTB-633)

The production Nixo path is `src/nIxo/main.cpp` built with `esp32dev_nixo`.

Current hardware invariant discovered during bench debugging:

- Real relay pin: `GPIO23`
- Relay polarity: active-HIGH (`HIGH` = fire/on, `LOW` = off)
- Second relay: disabled (`NIXO_RELAY2_PIN=-1`)
- Live mapping: `go2_03 -> nixo_go2_03`
- MQTT topic: `battlebang/nixo/nixo_go2_03/command`

The older `src/nIxo/BluetoothSerial.cpp` file is a Bluetooth-only baseline/smoke sketch. It does not read USB Serial
commands, so use Bluetooth SPP or the MQTT firmware's own local/debug inputs when comparing against it.

---

## Arduino IDE path (legacy root firmware only)

Arduino IDE is still useful for the legacy single-file `src/main.cpp` firmware, but it is no longer the recommended path
for the multi-environment workspace.

1. Install Arduino IDE: https://www.arduino.cc/en/software
2. Add ESP32 board support URL:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Install **esp32 by Espressif Systems** and **FastLED**.
4. Copy `src/main.cpp` into a sketch and upload to **ESP32 Dev Module**.

Do not use this Arduino-copy flow for `src/nIxo/main.cpp` unless you also port its PlatformIO build flags, local secrets,
and libraries manually.

---

## ESP-IDF note

The current sources are Arduino-style C++ (`Arduino.h`, FastLED, PubSubClient/ArduinoJson for MQTT paths). If you want a
pure ESP-IDF project, create a separate ESP-IDF component layout and port the Arduino dependencies deliberately. For this
repo as-is, PlatformIO is the supported command-line build/upload path.

---

## Unit tests (game logic)

`src/main.cpp`의 HP/밴드/데미지/명령 파싱 등 순수 로직은 `lib/game_logic`로 분리해 호스트 PC에서 테스트합니다.

```bash
chmod +x tests/run_tests.sh && ./tests/run_tests.sh
# or
pio test -e native
```

Structure: `tests/test_game_logic.cpp` + `lib/game_logic/`. Firmware uploads still flash one complete environment image
at a time.
