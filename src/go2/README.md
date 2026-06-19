# Go2 Hit ESP Firmware Setup Guide

Go2 등에 장착되는 ESP32 피격/LED 보드용 펌웨어 가이드입니다.

이 펌웨어는 ESP가 Command Center와 MQTT로 직접 통신하도록 빌드됩니다. 발사/릴레이/서보 제어는 이 펌웨어에서 제거되었고, Go2의 발사는 `src/nIxo/` Nixo/game blaster 펌웨어가 담당합니다.

현재 Go2 ESP의 책임은 세 가지입니다.

1. 피에조 센서 AO ADC raw 값이 threshold 이상으로 올라오면 `hit_candidate(hit=true)` 이벤트를 Command Center에 publish
2. Command Center가 내려준 legacy `ring_display` 명령을 84개 HP bar LED에 렌더링
3. 별도 `src/nIxo/` ESP로 가는 Nixo fire MQTT command를 미러링해서 기존 ring LED에 cooldown을 표시

ESP는 타격 세기, 로컬 스코어, 로컬 down 상태를 계산하거나 저장하지 않습니다. 난이도/스코어/down 기준과 LED fill 비율은 Command Center 설정과 정책이 소유합니다. MQTT가 끊긴 동안의 hit는 RAM queue에 잠시 보관했다가 재연결 후 원래 `firmware_ts_ms`와 함께 재전송합니다.

```text
Piezo AO ADC threshold -> ESP hit_candidate(hit=true, peak, threshold) -> Command Center scoring/down policy
Command Center ring_display command -> ESP HP bar LED render
Command Center Nixo fire command -> ESP fire/cooldown ring render
```

현재 코드 구조:

```text
main.cpp   setup/loop runtime orchestration
display/   bar_display=HP bar renderer, ring_display=Nixo fire/cooldown ring renderer
mqtt/      hit_candidate/heartbeat/ring_display + Nixo fire mirror MQTT 통신
```

기본 핀맵 (`robots.json` defaults 기준):

| Part | Pin | Role |
| --- | --- | --- |
| HP bar LED data | `GPIO18` | HP 잔량 표시 |
| Ring LED data | `GPIO4` | Nixo fire/cooldown 표시 |
| Piezo AO ADC | `GPIO34` | hit 후보 감지 |
| Piezo DO debug readback | `GPIO27` | debug only |

- ESP → Command Center
  - `battlebang/hit/{go2_id}/events`
  - `hit_candidate`, `heartbeat`
- Command Center → ESP
  - `battlebang/hit/{go2_id}/ring_display/command`
  - legacy `ring_display` payload를 HP bar display로 렌더링
- Command Center → Nixo ESP (Go2 ESP는 cooldown ring 표시를 위해 mirror subscribe)
  - `battlebang/nixo/{nixo_id}/command`
  - `fire` payload의 `enabled`/`duration_ms`를 ring fire/cooldown 표시로 미러링

예를 들어 `go2_03`용으로 업로드하면 topic은 자동으로 아래처럼 잡힙니다.

```text
battlebang/hit/go2_03/events
battlebang/hit/go2_03/ring_display/command
battlebang/nixo/nixo_go2_03/command
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
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"
```

예:

```cpp
#define ESP_WIFI_SSID "abcdefg"
#define ESP_WIFI_PASSWORD "********"
#define ESP_MQTT_HOST "COMMAND_CENTER_IP_OR_DNS"
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"
```

주의:

- `src/go2/local_secrets.h`는 `.gitignore` 대상입니다.
- 실제 Wi-Fi password는 커밋하지 않습니다.
- 보통 `local_secrets.h`에는 `go2_03` 같은 robot id를 넣지 않습니다.
- robot id는 업로드 명령의 `--target go2_03=...`로 정합니다.

---

## 3. ESP 연결 후 USB 포트 확인

ESP32를 PC에 USB로 연결한 뒤 아래 명령을 실행합니다.

```bash
python3 scripts/go2_flash.py list-ports
```

ESP 포트 예:

```text
/dev/cu.usbserial-21130
COM3
```

---

## 4. Go2 ID와 ESP 보드 매칭

예를 들어 아래처럼 매칭했다고 가정합니다.

| ESP board | Robot |
|---|---|
| `esp_03` | `go2_03` |
| `esp_05` | `go2_05` |
| `esp_06` | `go2_06` |
| `esp_07` | `go2_07` |

이 경우 `esp_03`에 올릴 펌웨어는 `go2_03`용으로 빌드해야 합니다.

---

## 5. Firmware upload

`esp_03`이 `/dev/cu.usbserial-21130`으로 잡혔고, 이 ESP가 `go2_03`에 붙는다면:

```bash
python3 scripts/go2_flash.py flash --target go2_03=/dev/cu.usbserial-21130
```

업로드 없이 빌드만 확인하려면:

```bash
python3 scripts/go2_flash.py flash --target go2_03 --build-only
```

다른 Go2에 올릴 때는 target만 바꾸면 됩니다.

```bash
python3 scripts/go2_flash.py flash --target go2_03=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_05=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_06=/dev/cu.usbserial-21130
python3 scripts/go2_flash.py flash --target go2_07=/dev/cu.usbserial-21130
```

---

## 6. Upload 후 확인할 것

업로드가 완료되면 Serial Monitor에서 아래 로그를 확인합니다.

```text
[CC] robot_id=go2_03 mqtt=enabled broker=<MQTT_BROKER_IP>:1883 event_topic=battlebang/hit/go2_03/events hp_bar_topic=battlebang/hit/go2_03/ring_display/command nixo_cooldown_topic=battlebang/nixo/nixo_go2_03/command
[WIFI] connecting ssid=...
[MQTT] connecting host=... port=1883 client_id=battlebang-hit-go2_03-go2-948C
[MQTT] subscribed battlebang/hit/go2_03/ring_display/command
[MQTT] subscribed nixo cooldown battlebang/nixo/nixo_go2_03/command
```

피에조 센서 AO ADC raw 값이 threshold 이상으로 올라오면 ESP가 `hit_candidate`를 publish합니다.

```json
{
  "schema_version": 1,
  "event": "hit_candidate",
  "robot_id": "go2_03",
  "sensor_id": "piezo_t1",
  "sequence": 1,
  "hit": true,
  "peak": 2140,
  "threshold": 400,
  "firmware_ts_ms": 12345,
  "firmware": "go2",
  "firmware_role": "hit_led",
  "mac_suffix": "948C",
  "client_id": "battlebang-hit-go2_03-go2-948C",
  "metadata": {
    "firmware": "go2",
    "firmware_role": "hit_led",
    "mac_suffix": "948C",
    "client_id": "battlebang-hit-go2_03-go2-948C",
    "hit_source": "piezo_ao_adc_threshold",
    "adc_peak_raw": 2140,
    "adc_threshold_raw": 400
  }
}
```

---

## 7. 설정 파일 구조

### `src/go2/robots.json`

Go2별 non-secret profile입니다.

```json
{
  "defaults": {
    "hit_cooldown_ms": 0,
    "offline_hit_queue_capacity": 32,
    "offline_hit_queue_flush_interval_ms": 50,
    "led_pin": 18,
    "num_leds": 84,
    "led_brightness": 120,
    "ring_led_pin": 4,
    "ring_num_leds": 40,
    "ring_led_brightness": 80,
    "t1_do_pin": 27,
    "piezo_ao_pin": 34,
    "piezo_ao_threshold_raw": 400,
    "piezo_ao_rearm_raw": 100,
    "piezo_ao_capture_window_ms": 30,
    "piezo_ao_debug_period_ms": 100,
    "mqtt_topic_prefix": "battlebang/hit",
    "nixo_mqtt_topic_prefix": "battlebang/nixo",
    "nixo_fire_default_duration_ms": 1500,
    "nixo_fire_min_duration_ms": 100,
    "nixo_fire_max_duration_ms": 10000,
    "nixo_fire_cooldown_ms": 10000
  },
  "robots": {
    "go2_03": { "configured": true },
    "go2_05": { "configured": true },
    "go2_06": { "configured": true },
    "go2_07": { "configured": true }
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
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"
```

이 파일은 커밋하지 않습니다.

---

## 8. Hit scoring rule

Go2 ESP는 `hit=true`만 보냅니다. Command Center는 accepted hit 개수를 원천 scoring unit으로 누적하고, 몇 번 맞으면 down인지와 LED fill 비율을 서버 config/policy로 계산합니다.

## 9. 통신 단절 시 hit queue

MQTT 연결이 없거나 publish가 실패하면 ESP는 valid hit event를 RAM queue에 넣습니다. 재연결되면 queue 앞에서부터 다시 publish합니다. 이때 payload에는 원래 hit가 발생한 `firmware_ts_ms`와 `metadata.queued=true`, `metadata.queued_for_ms`가 포함됩니다.

중요:

- queue는 로컬 판정이 아니라 transport 보관용입니다.
- ESP는 queue를 flush할 뿐, score/down/LED fill을 계산하지 않습니다.
- queue가 꽉 차면 오래된 hit부터 버리고 최신 hit를 보관합니다.
- 리셋 명령(`reset_hit_state=true` 또는 로컬 `2`)은 센서 latch와 queue를 같이 비웁니다.
- Command Center/MQTT가 내려주는 `ring_display`가 없으면 ESP는 로컬 점수 계산 없이 full idle HP bar를 표시합니다.
- 기존 ring LED는 HP/hit/down 표시를 하지 않고 Nixo cooldown 전용입니다. Ready는 green full, firing은 red full, fire 종료 후 10초 cooldown 동안 off에서 시작해 1초마다 ring의 1/10씩 green으로 채웁니다.

주의:

- ESP hit cooldown 기본값은 0ms이며, 센서 rearm gate와 Command Center 최종 중복 판정으로 처리합니다.
- Command Center `hit_accept_cooldown_ms`가 중복 score 방지의 최종 기준입니다.

---

## 10. 자주 나는 에러

### `zsh: command not found: python`

Mac에서는 보통 `python` 대신 `python3`를 씁니다.

```bash
python3 scripts/go2_flash.py list-ports
```

### `FileNotFoundError: No such file or directory: 'pio'`

`pio`가 PATH에 없을 때 발생합니다. 현재 `scripts/go2_flash.py`는 `pio`가 없으면 자동으로 `uvx platformio`를 사용합니다.

### 업로드 중 connecting에서 멈춤

일부 ESP32 보드는 업로드할 때 BOOT 버튼을 눌러야 합니다.

1. 업로드 명령 실행
2. `Connecting...`가 뜨면 ESP32의 BOOT 버튼 누름
3. 업로드가 시작되면 버튼에서 손 뗌
