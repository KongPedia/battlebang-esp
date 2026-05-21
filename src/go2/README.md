# Go2 Hit ESP Firmware Setup Guide

Go2 등에 장착되는 ESP32 피격/LED 보드용 펌웨어 가이드입니다.

이 펌웨어는 ESP가 Command Center와 MQTT로 직접 통신하도록 빌드됩니다.

- ESP → Command Center
  - `battlebang/esp/{go2_id}/events`
  - `hit_candidate`, `heartbeat`
- Command Center → ESP
  - `battlebang/esp/{go2_id}/ring_display/command`
  - `ring_display`

예를 들어 `go2_01`용으로 업로드하면 topic은 자동으로 아래처럼 잡힙니다.

```text
battlebang/esp/go2_01/events
battlebang/esp/go2_01/ring_display/command
```

---

## 1. Repository clone

```bash
git clone git@github.com:KongPedia/battlebang-esp.git
cd battlebang-esp
```

HTTPS를 쓰는 경우:

```bash
git clone https://github.com/KongPedia/battlebang-esp.git
cd battlebang-esp
```

---

## 2. Local secrets 만들기

Wi-Fi / MQTT broker 주소는 git에 올리면 안 되므로 `local_secrets.h`에 따로 둡니다.

```bash
cp src/go2/local_secrets.example.h src/go2/local_secrets.h
```

그 다음 `src/go2/local_secrets.h`를 열어서 수정합니다.

```cpp
#define ESP_WIFI_SSID "YOUR_WIFI_SSID"
#define ESP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define ESP_MQTT_HOST "COMMAND_CENTER_OR_BROKER_HOST"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/esp"
```

예:

```cpp
#define ESP_WIFI_SSID "abcdefg"
#define ESP_WIFI_PASSWORD "********"
#define ESP_MQTT_HOST "192.168.123.1"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/esp"
```

주의:

- `src/go2/local_secrets.h`는 `.gitignore` 대상입니다.
- 실제 Wi-Fi password는 커밋하지 않습니다.
- 보통 `local_secrets.h`에는 `go2_01` 같은 robot id를 넣지 않습니다.
- robot id는 업로드 명령의 `--target go2_01=...`로 정합니다.

---

## 3. ESP 연결 후 USB 포트 확인

ESP32를 PC에 USB로 연결한 뒤 아래 명령을 실행합니다.

```bash
python3 scripts/go2_flash.py list-ports
```

예시 출력:

```text
/dev/cu.debug-console        n/a         n/a
/dev/cu.Bluetooth-Incoming-Port n/a      n/a
/dev/cu.usbserial-21130      USB Serial  USB VID:PID=1A86:7523 LOCATION=2-1.1.3
```

여기서 ESP 포트는 보통 이런 형태입니다.

```text
/dev/cu.usbserial-21130
```

Windows라면 보통 이런 형태입니다.

```text
COM3
COM4
```

---

## 4. Go2 ID와 ESP 보드 매칭

예를 들어 아래처럼 매칭했다고 가정합니다.

| ESP board | Robot |
|---|---|
| `esp_01` | `go2_01` |
| `esp_02` | `go2_02` |
| `esp_03` | `go2_03` |

이 경우 `esp_01`에 올릴 펌웨어는 `go2_01`용으로 빌드해야 합니다.

중요:

```text
esp_01 하드웨어에 go2_01용 firmware를 업로드한다
= --target go2_01=/dev/cu.usbserial-xxxx
```

---

## 5. Firmware upload

`esp_01`이 `/dev/cu.usbserial-21130`으로 잡혔고, 이 ESP가 `go2_01`에 붙는다면:

```bash
python3 scripts/go2_flash.py flash --target go2_01=/dev/cu.usbserial-21130
```

업로드만 안 하고 빌드만 확인하려면:

```bash
python3 scripts/go2_flash.py flash --target go2_01 --build-only
```

다른 Go2에 올릴 때는 target만 바꾸면 됩니다.

```bash
python3 scripts/go2_flash.py flash --target go2_02=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_03=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_04=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_05=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_06=/dev/cu.usbserial-21130
```

---

## 6. Upload 후 확인할 것

업로드가 완료되면 Serial Monitor에서 아래 로그를 확인합니다.

```text
[CC] robot_id=go2_01 mqtt=enabled broker=10.2.80.80:1883 event_topic=battlebang/esp/go2_01/events ring_topic=battlebang/esp/go2_01/ring_display/command
[WIFI] connecting ssid=...
[MQTT] connecting host=... port=1883 client_id=battlebang-hit-go2_01
[MQTT] subscribed battlebang/esp/go2_01/ring_display/command
```

MQTT broker에서 확인할 topic:

```text
battlebang/esp/go2_01/events
battlebang/esp/go2_01/ring_display/command
```

피에조 센서를 치면 ESP가 `hit_candidate`를 publish합니다.

예:

```json
{
  "schema_version": 1,
  "event": "hit_candidate",
  "robot_id": "go2_01",
  "sensor_id": "piezo_t1",
  "sequence": 1,
  "peak": 4095,
  "threshold": 3000,
  "firmware_ts_ms": 12345
}
```

---

## 7. 설정 파일 구조

### `src/go2/robots.json`

Go2별 non-secret profile입니다.

예:

```json
{
  "defaults": {
    "hp_max": 100,
    "piezo_damage_divisor": 100,
    "hit_threshold": 3000,
    "mqtt_topic_prefix": "battlebang/esp"
  },
  "robots": {
    "go2_01": { "configured": true },
    "go2_02": { "configured": true }
  }
}
```

### `src/go2/local_secrets.h`

Wi-Fi / MQTT broker secret입니다.

```cpp
#define ESP_WIFI_SSID "..."
#define ESP_WIFI_PASSWORD "..."
#define ESP_MQTT_HOST "..."
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/esp"
```

이 파일은 커밋하지 않습니다.

---

## 8. Damage rule

현재 피격 데미지는 임시 룰입니다.

```text
damage = piezo_peak / piezo_damage_divisor
```

기본값:

```text
piezo_damage_divisor = 100
```

즉 현재는 피에조 센서값 앞 두 자리만 데미지처럼 씁니다.

```text
peak=4000 -> damage=40
peak=3500 -> damage=35
```

주의:

- 피에조 센서는 압력 센서가 아니므로 실제 타격 강도를 정확히 의미하지 않습니다.
- 이 룰은 임시 게임룰입니다.
- 최종 게임룰이 정해지면 `piezo_damage_divisor` 또는 데미지 계산식을 수정해야 합니다.

---

## 9. 자주 나는 에러

### `zsh: command not found: python`

Mac에서는 보통 `python` 대신 `python3`를 씁니다.

```bash
python3 scripts/go2_flash.py list-ports
```

### `FileNotFoundError: No such file or directory: 'pio'`

`pio`가 PATH에 없을 때 발생합니다.

현재 `scripts/go2_flash.py`는 `pio`가 없으면 자동으로 `uvx platformio`를 사용합니다.

그래도 안 되면 직접 PlatformIO를 설치합니다.

```bash
brew install platformio
```

또는:

```bash
pipx install platformio
```

### 업로드 중 connecting에서 멈춤

일부 ESP32 보드는 업로드할 때 BOOT 버튼을 눌러야 합니다.

1. 업로드 명령 실행
2. `Connecting...`가 뜨면 ESP32의 BOOT 버튼 누름
3. 업로드가 시작되면 버튼에서 손 뗌

---

## 10. 요약 명령어

처음 받은 뒤:

```bash
git clone git@github.com:KongPedia/battlebang-esp.git
cd battlebang-esp
cp src/go2/local_secrets.example.h src/go2/local_secrets.h
# src/go2/local_secrets.h 수정
```

ESP 포트 확인:

```bash
python3 scripts/go2_flash.py list-ports
```

`esp_01`에 `go2_01`용 펌웨어 업로드:

```bash
python3 scripts/go2_flash.py flash --target go2_01=/dev/cu.usbserial-21130
```

빌드만 확인:

```bash
python3 scripts/go2_flash.py flash --target go2_01 --build-only
```
