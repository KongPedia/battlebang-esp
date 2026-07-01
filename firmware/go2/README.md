# Go2 Hit ESP Firmware Setup Guide

Go2 등에 장착되는 ESP32 피격/LED 보드용 펌웨어 가이드입니다.

이 펌웨어는 ESP가 Command Center와 MQTT로 직접 통신하도록 빌드됩니다. 발사/릴레이/서보 제어와 쿨다운 링 표시는 이 펌웨어에서 제거되었습니다.

현재 Go2 ESP의 책임은 세 가지입니다.

1. left/right/front 피에조 센서 AO ADC 중 최대 raw 값이 threshold 이상이면 ESP가 로컬에서 즉시 hit를 accept
2. ESP 내부 `accepted_hit_count` / `hp_remaining` / `down` 상태를 갱신하고 84개 HP bar LED를 바로 표시
3. Command Center에는 `hit_event`와 device `status`로 현재 HP/down 상태만 publish

즉 정상 hit마다 Command Center가 `ring_display`를 내려주지 않아도 됩니다. MQTT가 끊겨도 ESP는 hit 판정과 LED 표시를 계속하고, event만 RAM queue에 보관했다가 재연결 후 원래 `firmware_ts_ms`와 당시 HP metadata로 재전송합니다.

```text
3ch Piezo AO max ADC threshold -> ESP local hit accept -> ESP HP/down state -> ESP HP bar render
                                      └── MQTT hit_event/status -> Command Center combat/status ingest
```

현재 코드 구조:

```text
main.cpp   setup/loop runtime orchestration + local hit/HP state
display/   bar_display=ESP-owned HP bar renderer + debug override renderer
mqtt/      hit_event/heartbeat/status publish, config/ota/reset/debug subscribe
```

기본 핀맵 (`hardware_profile.json` defaults 기준):

| Part | Pin | Role |
| --- | --- | --- |
| HP bar LED data | `GPIO18` | HP 잔량 표시 |
| Piezo AO ADC | left `GPIO34`, right `GPIO35`, front `GPIO32` | ESP 로컬 3ch hit 감지 |
| Piezo DO debug readback | `GPIO27` | debug only |

- ESP → Command Center
  - `battlebang/hit/{robot_id}/events`: `hit_event`, `heartbeat`
  - `battlebang/devices/go2/{device_id}/status`: `hp_remaining`, `max_hits`, `down`, `combat` facet
- Command Center → ESP
  - `battlebang/devices/go2/{device_id}/config`: threshold/max_hits/brightness 등 NVS config 갱신
  - `battlebang/hit/{robot_id}/ring_display/command`: `reset_hit_state` 또는 `debug_override` 호환 명령만 사용

예를 들어 `go2_03`용으로 업로드하면 topic은 자동으로 아래처럼 잡힙니다.

```text
battlebang/hit/go2_03/events
battlebang/devices/go2/go2_03/status
battlebang/devices/go2/go2_03/config
battlebang/hit/go2_03/ring_display/command   # reset/debug compatibility only
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

## 2. Runtime `.env.go2` 만들기

Wi-Fi / MQTT broker 주소와 runtime device identity는 git에 올리면 안 되므로
ignored 파일인 `firmware/go2/.env.go2`에 두고, serial provisioning으로 ESP32
NVS에 저장합니다. `local_secrets.h`는 표준 경로에서 필요하지 않습니다.

```bash
cp firmware/go2/.env.go2.example firmware/go2/.env.go2
```

그 다음 `firmware/go2/.env.go2`를 열어서 수정합니다.

```dotenv
GO2_ROBOT_ID=go2_dev_01
GO2_STAGE_ID=dev_stage_01
GO2_WIFI_SSID=YOUR_WIFI_SSID
GO2_WIFI_PASSWORD=YOUR_WIFI_PASSWORD
GO2_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS
GO2_MQTT_PORT=1883
GO2_MQTT_TOPIC_PREFIX=battlebang/hit
```

주의:

- `firmware/go2/.env.go2`는 `.gitignore` 대상입니다.
- 실제 Wi-Fi password는 커밋하지 않습니다.
- robot id는 빌드 env가 아니라 NVS runtime config(`GO2_ROBOT_ID` / `robot_id`)로 정합니다.
- `local_secrets.h`는 기본 빌드에서 읽지 않습니다. 정말 필요한 factory/legacy fallback만
  `BATTLEBANG_ENABLE_LOCAL_SECRETS` 또는 `scripts/go2_flash.py flash --use-local-secrets`
  로 명시적으로 켭니다.

### NVS runtime provisioning

표준화 경로에서는 Wi-Fi/MQTT/identity와 센서/표시 튜닝값을 펌웨어에 다시 빌드하지 않고 ESP32 NVS에 저장합니다. `hardware_profile.json` 값은 물리 배선/기본 튜닝 fallback으로만 사용하고, Wi-Fi/MQTT 값은 `.env.go2`에서 provision합니다.

```bash
# 전송 전 JSON 확인. password는 기본 출력에서 마스킹됩니다.
.venv-pio/bin/python scripts/go2/provision.py --no-serial --print-json

# ESP에 NVS provision payload 전송
.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-XXXX

# 저장된 config/status 확인 또는 초기화
.venv-pio/bin/python scripts/go2/provision.py --command show-config --serial-port /dev/cu.usbserial-XXXX
.venv-pio/bin/python scripts/go2/provision.py --command show-status --serial-port /dev/cu.usbserial-XXXX
.venv-pio/bin/python scripts/go2/provision.py --command clear-config --serial-port /dev/cu.usbserial-XXXX
```

`provision`/`config` payload는 공통 `wifi`, `mqtt`, `ota` 필드와 Go2 domain 필드(`robot_id`, `hit_topic_prefix`, hit cooldown, offline queue, LED brightness, piezo threshold/rearm/capture/debug/rearm-stable, `max_hits`, `hit_flash_ms`)를 포함합니다.

Go2 펌웨어는 이제 공통 `bb_esp_ota` HTTP OTA 엔진을 사용합니다. `show-status`와 MQTT device status에는 `ota_supported=true`, `ota_manifest_url`, `ota_channel`, `ota_desired_build`, `post_ota_reboot`가 포함됩니다. Serial/BT `check-ota [manifest-url]`, MQTT `{mqtt_root}/devices/go2/{device_id}/ota`, 그리고 `ota.auto_check_enabled=true`일 때 자동 polling 경로가 모두 같은 manifest 검증/sha256/rollback-marker 흐름을 사용합니다.

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

이 경우에도 `esp_03`에는 generic `esp32dev_go2` 이미지를 빌드/업로드하고, `GO2_ROBOT_ID=go2_03`으로 NVS provision합니다.

---

## 5. Firmware upload

`esp_03`이 `/dev/cu.usbserial-21130`으로 잡혔고, 이 ESP가 `go2_03`에 붙는다면:

```bash
./.venv-pio/bin/pio run -e esp32dev_go2 -t upload --upload-port /dev/cu.usbserial-21130
GO2_ROBOT_ID=go2_03 ./.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-21130
```

업로드 없이 generic build만 확인하려면:

```bash
./.venv-pio/bin/pio run -e esp32dev_go2
```

다른 Go2에 올릴 때도 같은 generic image를 사용하고 provision payload의 `GO2_ROBOT_ID`/`GO2_STAGE_ID`만 바꿉니다. `--target go2_03=...`는 build env가 아니라 편의상 출력되는 runtime label입니다.

```bash
./.venv-pio/bin/pio run -e esp32dev_go2 -t upload --upload-port /dev/cu.usbserial-21130
GO2_ROBOT_ID=go2_03 ./.venv-pio/bin/python scripts/go2/provision.py --serial-port /dev/cu.usbserial-21130
# For another robot, reuse the same image and change GO2_ROBOT_ID/GO2_STAGE_ID
# in firmware/go2/.env.go2 before re-running scripts/go2/provision.py.
```

---

## 6. Upload 후 확인할 것

업로드가 완료되면 Serial Monitor에서 아래 로그를 확인합니다.

```text
[CC] robot_id=go2_03 mqtt=enabled broker=<MQTT_BROKER_IP>:1883 event_topic=battlebang/hit/go2_03/events hp_bar_topic=battlebang/hit/go2_03/ring_display/command
[WIFI] connecting ssid=...
[MQTT] connecting host=... port=1883 client_id=battlebang-hit-go2_03-go2-948C
[MQTT] subscribed battlebang/hit/go2_03/ring_display/command
```

left/right/front 피에조 센서 AO ADC 중 최대 raw 값이 threshold 이상으로 올라오면 ESP가 로컬 HP를 먼저 깎고 LED bar를 갱신한 뒤 `hit_event`를 publish합니다.

```json
{
  "schema_version": 2,
  "event": "hit_event",
  "robot_id": "go2_03",
  "sensor_id": "piezo:left",
  "sequence": 1,
  "hit": true,
  "accepted": true,
  "accepted_hit_count": 1,
  "hp_remaining": 13,
  "max_hits": 14,
  "down": false,
  "ring_fill_ratio": 0.928571,
  "peak": 2140,
  "threshold": 2400,
  "firmware_ts_ms": 12345,
  "metadata": {
    "hit_source": "piezo_ao_adc_threshold",
    "decision_owner": "esp_local",
    "display_owner": "esp_local",
    "hp_current": 13,
    "hp_max": 14,
    "adc_peak_raw": 2140,
    "adc_threshold_raw": 2400
  }
}
```

---

## 7. 설정 파일 구조

### `firmware/go2/hardware_profile.json`

커밋 가능한 non-secret hardware fallback profile입니다. Robot별 ID나 stage는 여기에 두지 않습니다.

```json
{
  "defaults": {
    "hit_cooldown_ms": 0,
    "offline_hit_queue_capacity": 32,
    "offline_hit_queue_flush_interval_ms": 50,
    "led_pin": 18,
    "num_leds": 84,
    "led_brightness": 120,
    "t1_do_pin": 27,
    "piezo_left_pin": 34,
    "piezo_right_pin": 35,
    "piezo_front_pin": 32,
    "piezo_ao_threshold_raw": 2400,
    "piezo_ao_rearm_raw": 1800,
    "piezo_ao_capture_window_ms": 30,
    "piezo_ao_debug_period_ms": 1000,
    "mqtt_topic_prefix": "battlebang/hit"
  },
  "notes": "identity/stage/tuning overrides are provisioned into ESP32 NVS"
}
```

### `firmware/go2/.env.go2` and legacy `local_secrets.h`

`.env.go2`가 표준 serial provisioning 입력이며 gitignore 대상입니다. Active
firmware는 기본적으로 `local_secrets.h`를 읽지 않습니다. 오래된 bench/factory
workflow에서 build-time fallback이 꼭 필요할 때만 `BATTLEBANG_ENABLE_LOCAL_SECRETS`
또는 `scripts/go2_flash.py flash --use-local-secrets`를 명시적으로 사용합니다.

---

## 8. Local hit/HP rule

Go2 ESP는 `max_hits`를 full HP로 보고 accepted hit마다 `hp_remaining`을 1씩 줄입니다. `hp_remaining == 0`이면 `down=true`가 되고 HP bar는 down 표시로 전환됩니다. Command Center는 이 값을 받아 dashboard/combat 상태를 맞추고, 정상 hit마다 LED bar 명령을 다시 내리지 않습니다.

- 기본값: `GO2_MAX_HITS=14`, `GO2_HIT_FLASH_MS=900`
- NVS config로 `max_hits`/`hits_to_down`을 바꾸면 다음 config 적용부터 ESP local HP 모델에 반영됩니다.
- ESP 전원을 뺐다 꽂거나 재부팅되면 `accepted_hit_count`/`hp_remaining`/`down`은 저장하지 않고 full HP로 초기화됩니다. NVS에는 `max_hits` 같은 룰만 남습니다.
- reset/recovery는 `reset_hit_state=true`, USB/BT `r`, 또는 `reset` 명령으로 local hit count/HP/down/queue/display를 초기화합니다.

## 9. 통신 단절 시 hit queue

MQTT 연결이 없거나 publish가 실패해도 ESP는 local hit 판정과 HP bar 표시를 즉시 수행합니다. event publish만 RAM queue에 넣고, 재연결되면 queue 앞에서부터 다시 publish합니다. 이때 payload에는 원래 hit가 발생한 `firmware_ts_ms`와 당시 `hp_remaining`/`down`, `metadata.queued=true`, `metadata.queued_for_ms`가 포함됩니다.

중요:

- queue는 transport 보관용입니다. HP/down은 event 발생 시점에 이미 로컬 반영됩니다.
- queue가 꽉 차면 오래된 hit event부터 버리고 최신 hit event를 보관합니다.
- 리셋 명령(`reset_hit_state=true` 또는 로컬 `r`)은 센서 latch, local HP/down, queue, display를 같이 비웁니다.
- 정상 운영에서 `ring_display` payload는 무시됩니다. LED 강제 테스트는 `debug_override=true` 또는 `maintenance_override=true`일 때만 TTL 동안 적용됩니다.

주의:

- ESP hit cooldown 기본값은 0ms이며, 센서 rearm gate로 중복 trigger를 제한합니다.
- Command Center 쪽 기존 hit accept/down/LED 계산은 Go2 local ESP 모델과 충돌하지 않게 ingest/status 동기화 역할로 바꿔야 합니다.

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
