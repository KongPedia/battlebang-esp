# Wi-Fi-only brownout 조사 기록

작성일: 2026-04-09  
대상 포트: `/dev/cu.usbserial-1130`  
대상 터렛 프리셋: `turret_5` (`x=-170.0cm`, `y=190.0cm`, `z=134.5cm`, target z `70.0cm`)

> 민감정보 원칙: Wi-Fi SSID/비밀번호/MQTT host 같은 local secret 값은 이 문서에 기록하지 않는다. 테스트 시에는 `src/turret/local_secrets.h`의 로컬 값이 사용되었다.

## 결론

현재 확인된 핵심 결론은 아래와 같다.

1. **초기 확인 시점에는 `esp32dev_demo_v2_turret_5` 펌웨어가 업로드되어 안정 동작했다. 이후 추가 진단을 위해 마지막으로 `esp32dev_wifi_probe_idf_11g_split` probe 펌웨어를 업로드했다.**
   - demo-v2 시리얼 모니터에서는 `Mode=IDLE ...` 로그가 계속 출력되고, demo-v2의 IDLE sweep 동작이 확인되었다.
   - 추가 probe 시리얼 모니터에서는 Wi-Fi start 지점 brownout이 반복 재현되었다.
2. **최소 Wi-Fi-only 펌웨어에서도 brownout reset이 재현된다.**
   - MQTT, JSON 파싱, turret main 제어 로직, 서보/ESC attach 없이도 `esp_wifi_start()` 직후 brownout이 발생했다.
3. 따라서 이번 brownout의 직접 원인은 `src/turret/main.cpp`의 MQTT command 처리나 모터 제어 루프가 아니라, **ESP32 Wi-Fi RF stack이 켜지는 순간의 전원 rail 여유 부족**으로 보는 것이 맞다.
4. demo-v2가 정상 동작하는 것은 이상한 현상이 아니라, demo-v2가 **Wi-Fi/RF를 시작하지 않는 serial-only 터렛 펌웨어**이기 때문이다.
   - demo-v2는 서보/릴레이/ESC/ADC/Serial 중심이다.
   - MQTT 펌웨어 및 Wi-Fi probe는 ESP32의 Wi-Fi RF 송수신부를 켠다.
   - 현재 전원 구성에서는 “모터는 어느 정도 돌아가지만 Wi-Fi RF start 순간은 버티지 못하는” 상태로 판단된다.

## 테스트 1: 최소 Wi-Fi-only 펌웨어

업로드한 env:

```bash
./.venv-pio/bin/pio run -e esp32dev_wifi_probe_idf_split -t upload --upload-port /dev/cu.usbserial-1130
```

해당 env는 `src/turret_wifi_probe.cpp`만 빌드한다.

- `src/turret/main.cpp` 미포함
- MQTT client 미포함
- ArduinoJson 미포함
- 서보/ESC/릴레이 attach 없음
- 목적: Wi-Fi stack 자체가 brownout을 일으키는지 분리 확인

모니터에서 확인된 핵심 로그:

```text
[WIFI_PROBE] IDF split start
[WIFI_PROBE] esp_netif_init() -> ESP_OK
[WIFI_PROBE] esp_event_loop_create_default() -> ESP_OK
[WIFI_PROBE] esp_netif_create_default_wifi_sta()
[WIFI_PROBE] esp_wifi_init()
[WIFI_PROBE] esp_wifi_init() -> ESP_OK
[WIFI_PROBE] esp_wifi_set_storage(WIFI_STORAGE_RAM) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_mode(WIFI_MODE_STA) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_config(WIFI_IF_STA) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_country(US, 2dBm) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_max_tx_power(2dBm) -> ESP_ERR_WIFI_NOT_STARTED
[WIFI_PROBE] esp_wifi_start()

Brownout detector was triggered

rst:0xc (SW_CPU_RESET)
```

해석:

- brownout 발생 지점이 `esp_wifi_start()` 바로 뒤로 좁혀졌다.
- 이 테스트에는 터렛 모터 제어 코드가 없으므로, “서보 attach 직후 전류가 커서 리셋된다”가 이번 테스트의 직접 원인은 아니다.
- `esp_wifi_set_country(..., 2dBm)`은 `ESP_OK`였지만 brownout을 막지 못했다.
- `esp_wifi_set_max_tx_power(2dBm)`은 Wi-Fi start 전에는 `ESP_ERR_WIFI_NOT_STARTED`를 반환했다. 즉, 현재 brownout이 나는 “start 순간”의 peak를 이 호출만으로 낮출 수 없었다.

## 테스트 2: demo-v2 serial-only 펌웨어

업로드한 env:

```bash
./.venv-pio/bin/pio run -e esp32dev_demo_v2_turret_5 -t upload --upload-port /dev/cu.usbserial-1130
```

시리얼 모니터에서 확인된 핵심 로그:

```text
=== 2-AXIS PID READY ===
Input:
  x,y  (example: 100,50)
  d    -> DEAD mode (immediate)
  r    -> return to IDLE (immediate)
Mode IDLE   : yaw sweep + pitch sweep
Mode TARGET : track target, fire, return idle
Mode DEAD   : hold current yaw, pitch up
Mode=IDLE | TargetXY=(0.0, 0.0) | YAW raw=... | PITCH raw=... | FIRE_STATE=IDLE
```

추가로 재확인한 현재 포트 상태:

```text
OPEN_OK /dev/cu.usbserial-1130
Mode=IDLE | TargetXY=(0.0, 0.0) | YAW raw=... | PITCH raw=... | FIRE_STATE=IDLE
```

해석:

- 같은 ESP32/터렛 연결 상태에서 demo-v2는 brownout 없이 계속 동작했다.
- demo-v2는 Wi-Fi stack을 시작하지 않으므로 RF start 순간의 전류 peak가 없다.
- demo-v2가 도는 것과 Wi-Fi-only probe가 brownout 나는 것은 서로 모순이 아니다.

## 왜 demo-v2는 되고 Wi-Fi-only는 안 되는가?

가장 중요한 차이는 **Wi-Fi RF start 여부**다.

| 항목 | demo-v2 serial-only | Wi-Fi-only probe / MQTT turret |
|---|---:|---:|
| Wi-Fi RF stack 시작 | 없음 | 있음 |
| MQTT 연결 | 없음 | 있음 또는 이후 예정 |
| JSON 파싱 | 없음 | MQTT turret만 있음 |
| 서보/릴레이/ESC 제어 | 있음 | probe에는 없음, MQTT turret에는 lazy attach |
| brownout 재현 | 안 됨 | `esp_wifi_start()`에서 재현 |

즉, 현재 증상은 “코드가 길어서” 또는 “MQTT payload 처리 때문에” 발생한다고 보기 어렵다. 최소 Wi-Fi-only 코드에서도 brownout이 나기 때문에, 전원 rail이 ESP32 Wi-Fi RF start 순간의 peak를 버티지 못하는 쪽이 더 강한 원인이다.

## 추가 검색 근거

온라인 사례와 공식 문서를 확인한 결과, ESP32에서 Wi-Fi 시작 시 brownout이 나는 사례는 흔하며 대부분 전원 공급/디커플링 문제로 수렴한다.

- Espressif ESP-IDF fatal error 문서: brownout detector는 공급 전압이 안전 레벨 아래로 내려갈 때 reset을 트리거한다.
  - https://docs.espressif.com/projects/esp-idf/en/v4.3.1/esp32/api-guides/fatal-errors.html#brownout
- ESP-IDF Wi-Fi API 문서: `esp_wifi_start()`는 현재 Wi-Fi config에 따라 STA/AP control block을 만들고 Wi-Fi를 시작하는 지점이다.
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html#_CPPv414esp_wifi_startv
- ESP-IDF Wi-Fi API 문서: `esp_wifi_config_11b_rate()`는 `esp_wifi_init()` 후, `esp_wifi_start()` 전에 호출 가능한 11b rate 비활성화 API다. 이번 추가 테스트에서 이 방법도 시도했다.
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html#_CPPv426esp_wifi_config_11b_rate16wifi_interface_tb
- ESPBoards troubleshooting 문서: Wi-Fi 초기화 시 전류 spike 때문에 brownout이 발생할 수 있고, 해결책으로 안정적인 5V 공급, 짧고 굵은 USB 케이블, 470µF~1000µF bulk capacitor, 주변장치 전원 분리를 제시한다.
  - https://www.espboards.dev/troubleshooting/issues/power/esp32-brownout-reset/
- Arduino Stack Exchange의 유사 사례: 최소 `WiFi.begin()` 코드에서 brownout이 재현되었고, 답변에서는 ESP32 모듈 전원핀 가까이에 decoupling capacitor를 추가하거나 USB cable/power path를 바꾸는 해결을 제시했다. Brownout detector를 끄는 것은 warning을 숨길 뿐 근본 해결이 아니라는 코멘트도 있다.
  - https://arduino.stackexchange.com/questions/76690/esp32-brownout-detector-was-triggered-upon-wifi-begin

## 테스트 3: Wi-Fi-only + 11b 비활성화 + CPU 80MHz + BT stop

온라인에서 찾은 소프트웨어 완화책 중 실제로 start 전에 넣을 수 있는 항목을 probe에 추가했다.

추가한 env:

```bash
./.venv-pio/bin/pio run -e esp32dev_wifi_probe_idf_11g_split -t upload --upload-port /dev/cu.usbserial-1130
```

추가 조건:

- `TURRET_WIFI_DISABLE_11B_RATE=1`
- `esp_wifi_config_11b_rate(WIFI_IF_STA, true)`를 `esp_wifi_start()` 전에 호출
- `TURRET_CPU_FREQ_MHZ=80`
- `TURRET_DISABLE_BT_AT_BOOT=1`

확인 로그:

```text
[WIFI_PROBE] setCpuFrequencyMhz(80)
[WIFI_PROBE] btStop()
[WIFI_PROBE] esp_wifi_config_11b_rate(WIFI_IF_STA, disable=true) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_country(US, 2dBm) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_max_tx_power(2dBm) -> ESP_ERR_WIFI_NOT_STARTED
[WIFI_PROBE] esp_wifi_start()

Brownout detector was triggered
```

결론:

- 11b rate 비활성화, CPU 80MHz, BT stop까지 적용해도 brownout은 계속 `esp_wifi_start()`에서 재현되었다.
- 따라서 현재 전원 조건에서는 소프트웨어 완화만으로 통과하기 어렵다.
- 이 테스트 이후 보드에는 마지막으로 `esp32dev_wifi_probe_idf_11g_split` 진단 펌웨어가 올라가 있다.

## 테스트 4: guest Wi-Fi 자격증명 변경 후 재테스트

신호/SSID/비밀번호 문제 가능성을 배제하기 위해 `src/turret/local_secrets.h`의 Wi-Fi 자격증명을 사용자가 제공한 guest network 값으로 바꾼 뒤 같은 probe를 다시 빌드/업로드했다.

> 이 문서에는 SSID/비밀번호 값을 기록하지 않는다.

실행한 명령:

```bash
./.venv-pio/bin/pio run -e esp32dev_wifi_probe_idf_11g_split -t upload --upload-port /dev/cu.usbserial-1130
```

결과:

```text
[WIFI_PROBE] esp_wifi_set_config(WIFI_IF_STA) -> ESP_OK
[WIFI_PROBE] esp_wifi_config_11b_rate(WIFI_IF_STA, disable=true) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_country(US, 2dBm) -> ESP_OK
[WIFI_PROBE] esp_wifi_set_max_tx_power(2dBm) -> ESP_ERR_WIFI_NOT_STARTED
[WIFI_PROBE] esp_wifi_start()

Brownout detector was triggered
```

해석:

- SSID/password를 바꿔도 동일하게 `esp_wifi_start()` 직후 brownout이 발생했다.
- 이 probe는 아직 `esp_wifi_connect()`까지 가지 못한다. 따라서 현재 증상은 “특정 SSID 신호가 약해서 연결 실패”라기보다, SSID에 접속을 시도하기 전 Wi-Fi RF start 순간의 전원 문제가 더 강하다.

## 테스트 5: legacy `WiFi.h + PubSubClient + ESP32Servo` 코드 재현

예전에 동작했던 구조와 최대한 비슷하게 `src/turret_legacy_mqtt_probe.cpp`를 추가했다.

특징:

- `#include <WiFi.h>`
- `#include <PubSubClient.h>`
- `#include <ESP32Servo.h>`
- setup에서 relay off -> servo attach/stop -> `WiFi.mode(WIFI_STA)` -> `WiFi.begin(...)` -> MQTT connect 순서
- Wi-Fi/MQTT 값은 현재 `src/turret/local_secrets.h`를 그대로 사용

추가한 env:

```bash
./.venv-pio/bin/pio run -e esp32dev_legacy_mqtt_probe -t upload --upload-port /dev/cu.usbserial-1130
```

결과:

```text
=== LEGACY MQTT PROBE READY ===
[SYS] relays off
[SYS] servos attached and stopped
[WiFi] mode(WIFI_STA)

Brownout detector was triggered
```

해석:

- 예전과 같은 `WiFi.h + PubSubClient.h + ESP32Servo.h` 조합으로도 현재 보드/전원 상태에서는 brownout이 재현되었다.
- brownout은 `WiFi.begin(...)`/MQTT connect 이전, Arduino `WiFi.mode(WIFI_STA)`가 내부에서 Wi-Fi low-level start를 수행하는 구간에서 발생한다.
- 따라서 현재 현상은 `PubSubClient` 때문이라고 보기 어렵다.
- “예전 코드가 됐었다”는 사실은 중요하지만, 현재 조건에서는 전원 경로/케이블/보드 regulator/연결 상태 또는 코어/빌드 환경 차이를 먼저 비교해야 한다.
- 이 테스트 이후 보드에는 마지막으로 `esp32dev_legacy_mqtt_probe` 펌웨어가 올라가 있다.

## 테스트 6: PlatformIO Espressif32 / Arduino-ESP32 core 다운그레이드

Arduino IDE 2.3.7은 IDE 앱 버전이고, 실제 Wi-Fi stack 동작에 더 직접적인 것은 Boards Manager의 `esp32 by Espressif Systems` core 버전이다. PlatformIO 쪽에서는 같은 legacy probe를 유지하고 platform/core 버전만 낮춰서 테스트했다.

추가 env:

```text
esp32dev_legacy_mqtt_probe_pio640 -> espressif32@6.4.0 / Arduino-ESP32 2.0.11
esp32dev_legacy_mqtt_probe_pio540 -> espressif32@5.4.0 / Arduino-ESP32 2.0.6
esp32dev_legacy_mqtt_probe_pio440 -> espressif32@4.4.0 / Arduino-ESP32 2.0.3
```

실행 결과:

- `esp32dev_legacy_mqtt_probe_pio640`: 빌드/업로드 성공, `WiFi.mode(WIFI_STA)` 직후 brownout
- `esp32dev_legacy_mqtt_probe_pio540`: 빌드/업로드 성공, `WiFi.mode(WIFI_STA)` 직후 brownout
- `esp32dev_legacy_mqtt_probe_pio440`: 빌드/업로드 성공, `WiFi.mode(WIFI_STA)` 직후 brownout

공통 로그:

```text
=== LEGACY MQTT PROBE READY ===
[SYS] relays off
[SYS] servos attached and stopped
[WiFi] mode(WIFI_STA)

Brownout detector was triggered
```

해석:

- PlatformIO/Arduino-ESP32 2.0.17, 2.0.11, 2.0.6, 2.0.3 계열에서 모두 같은 위치에서 brownout이 재현되었다.
- 따라서 “최근 PlatformIO core만의 regression” 가능성은 낮아졌다.
- 단, 사용자가 Arduino IDE에서 실제로 사용했던 Boards Manager의 `esp32 by Espressif Systems` 버전이 1.0.x 또는 3.x라면 아직 그 exact 버전은 별도로 확인해야 한다.
- 이 테스트 이후 보드에는 마지막으로 `esp32dev_legacy_mqtt_probe_pio440` 펌웨어가 올라가 있다.

## 현재 소프트웨어 쪽에서 이미 반영/시도한 방어책

MQTT turret 쪽에는 이미 아래 방향의 소프트웨어 방어책이 들어가 있다.

- 부팅 직후 자동 발사/자동 target 금지
- 기본 상태를 `HOLD`로 두고 `idle`, `target`, `fire`, `dead` 명령 기반으로 동작
- 서보/ESC/릴레이를 부팅 즉시 attach하지 않고 필요한 순간에 lazy attach
- Wi-Fi 시작 전 delay (`TURRET_WIFI_BOOT_DELAY_MS`)
- Wi-Fi 저출력 시도 (`TURRET_WIFI_REDUCED_TX_POWER`)
- 수동 Wi-Fi 시작 env 제공 (`esp32dev_turret_5_manual_wifi_test`)

하지만 이번 최소 Wi-Fi-only probe 결과상, **Wi-Fi start 자체에서 brownout이 발생하면 위의 모터 지연/비활성화 최적화만으로는 해결이 제한적**이다.

## 재현/복구용 명령

### Wi-Fi-only brownout 재현

```bash
./.venv-pio/bin/pio run -e esp32dev_wifi_probe_idf_split -t upload --upload-port /dev/cu.usbserial-1130
```

### demo-v2 serial-only 안정 동작 확인

```bash
./.venv-pio/bin/pio run -e esp32dev_demo_v2_turret_5 -t upload --upload-port /dev/cu.usbserial-1130
```

### MQTT turret 자동 Wi-Fi 시작 테스트

현재 전원 상태에서는 brownout이 재현될 가능성이 높다.

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5_safe_low_wifi_test -t upload --upload-port /dev/cu.usbserial-1130
```

### MQTT turret 수동 Wi-Fi 시작 테스트

부팅은 안정적으로 시킨 뒤, 시리얼에서 `w`를 입력했을 때 Wi-Fi start를 별도로 확인하기 위한 env다.

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5_manual_wifi_test -t upload --upload-port /dev/cu.usbserial-1130
```

## 다음 판단 기준

1. demo-v2만 필요하면 아래 명령으로 `esp32dev_demo_v2_turret_5`를 다시 업로드하면 된다.
2. MQTT 원격 제어가 필요하면 Wi-Fi RF start를 반드시 통과해야 한다.
3. 소프트웨어만으로 추가로 확인할 수 있는 것은 다음 정도다.
   - Wi-Fi start 시점을 더 늦추기
   - 첫 사용자 명령 전까지 Wi-Fi를 시작하지 않기
   - BLE/BT 비활성화 유지
   - CPU frequency/print/servo attach 지연 유지
   - Wi-Fi start 후에만 tx power를 다시 낮추는 코드 추가
4. 다만 최소 Wi-Fi-only probe가 brownout 나는 현재 조건에서는, 최종적으로는 ESP32 입력 전원/보드 3.3V regulator/USB 케이블/보조배터리 순간전류/모터와 ESP 전원 분리 여부를 확인해야 한다.
