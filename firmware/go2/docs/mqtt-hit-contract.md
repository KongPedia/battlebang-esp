# Go2 ESP ↔ Command Center MQTT 계약

Go2 ESP는 piezo AO ADC threshold를 넘은 입력을 **ESP 로컬에서 즉시 hit로 accept**하고, ESP 내부 HP/down 상태와 84-LED HP bar 표시를 갱신합니다. Command Center는 hit 판정/LED bar per-hit 표시를 다시 계산하지 않고, ESP가 publish하는 `hit_event`/device `status`의 `accepted_hit_count`, `hp_remaining`, `max_hits`, `down` 값을 combat facet/source of truth로 ingest합니다.

## Topic

기본 hit prefix는 `battlebang/hit`, 기본 device root는 `battlebang`입니다.

```text
ESP -> Command Center
battlebang/hit/{robot_id}/events
battlebang/devices/{device_id}/status

Command Center -> ESP
battlebang/devices/{device_id}/config
battlebang/devices/{device_id}/ota
battlebang/hit/{robot_id}/ring_display/command   # reset/debug compatibility only
```

## ESP -> Command Center: hit_event

피에조 AO raw가 `piezo_ao_threshold_raw` 이상으로 올라오면 ESP가 capture window에서 peak를 잡고 hit를 accept합니다. accept 즉시:

1. `accepted_hit_count`를 증가시킵니다.
2. `hp_remaining`을 1 감소시킵니다.
3. `hp_remaining == 0`이면 `down=true`가 됩니다.
4. HP bar LED를 로컬에서 바로 갱신/flash/down 표시합니다.
5. 아래 `hit_event`를 MQTT로 publish합니다.

MQTT가 끊긴 동안의 hit는 HP/down 상태가 이미 로컬에 반영되고, event는 RAM queue에 보관했다가 재연결 후 원래 `firmware_ts_ms`와 당시 HP metadata로 재전송됩니다.

```json
{
  "schema_version": 2,
  "event": "hit_event",
  "robot_id": "go2_05",
  "sensor_id": "piezo_t1",
  "sequence": 7,
  "hit": true,
  "accepted": true,
  "accepted_hit_count": 3,
  "hp_remaining": 11,
  "max_hits": 14,
  "down": false,
  "ring_fill_ratio": 0.785714,
  "peak": 2140,
  "threshold": 400,
  "firmware_ts_ms": 12345,
  "firmware": "go2",
  "firmware_role": "hit_led",
  "mac_suffix": "948C",
  "client_id": "battlebang-hit-go2_05-go2-948C",
  "metadata": {
    "firmware": "go2",
    "firmware_role": "hit_led",
    "mac_suffix": "948C",
    "client_id": "battlebang-hit-go2_05-go2-948C",
    "hit_source": "piezo_ao_adc_threshold",
    "decision_owner": "esp_local",
    "display_owner": "esp_local",
    "hp_current": 11,
    "hp_max": 14,
    "adc_peak_raw": 2140,
    "adc_threshold_raw": 400
  }
}
```

재전송 event는 같은 schema에 `queued`, `queued_for_ms`, `queue_depth`, `queue_dropped` metadata를 추가합니다. 서버는 같은 `sequence`를 idempotency key로 취급해야 합니다.

`down=true`가 된 뒤에는 reset 전까지 추가 piezo trigger를 새 accepted hit로 publish하지 않습니다. ESP는 로컬 HP/down 상태를 유지하고, MQTT가 연결되어 있으면 device status reason `local_hit_ignored_down`만 publish합니다.

## ESP -> Command Center: device status / combat facet

ESP는 boot, reset, config 적용, local hit/down, 주기 heartbeat, MQTT reconnect 이후 status를 `{mqtt_root}/devices/{device_id}/status`에 publish합니다. Command Center는 이 payload의 root 필드 또는 nested `combat` 필드를 읽어 현재 HP/down 상태를 동기화합니다.

```json
{
  "type": "status",
  "reason": "local_hit",
  "firmware_app": "go2",
  "configured": true,
  "device_id": "go2_05",
  "robot_id": "go2_05",
  "mqtt_connected": true,
  "accepted_hit_count": 3,
  "hp_remaining": 11,
  "max_hits": 14,
  "down": false,
  "ring_fill_ratio": 0.785714,
  "last_hit_sequence": 7,
  "combat": {
    "accepted_hit_count": 3,
    "hp": 11,
    "hp_current": 11,
    "max_hp": 14,
    "hp_max": 14,
    "down": false,
    "ring_fill_ratio": 0.785714,
    "last_hit_sequence": 7
  }
}
```

## ESP -> Command Center: heartbeat

Hit event topic heartbeat는 연결/queue/display ownership 관측용입니다.

```json
{
  "schema_version": 1,
  "event": "heartbeat",
  "robot_id": "go2_05",
  "sensor_id": "hit_ring",
  "sequence": 10,
  "firmware_ts_ms": 20000,
  "mode": "mqtt_connected",
  "display_owner": "esp_local",
  "metadata": {
    "offline_queue_count": 0,
    "offline_queue_capacity": 32,
    "offline_queue_dropped": 0
  }
}
```

- `display_owner=esp_local`: 정상. ESP 로컬 HP bar 상태가 화면 source of truth입니다.
- `display_owner=debug_override`, `mode=direct`: `debug_override`/`maintenance_override` ring command가 TTL 동안 display를 덮어쓴 상태입니다.
- `mode=mqtt_connected`: MQTT 연결 정상.
- `mode=mqtt_disconnected`: MQTT 연결 없음. ESP는 그래도 local hit/LED 처리를 계속합니다.

## Command Center -> ESP: config / reset / debug display

### Runtime config

Threshold, HP cap, flash 시간 등 현장 튜닝값은 device config topic으로 보냅니다.

```bash
./.venv-pio/bin/python scripts/go2/provision.py \
  --env-file firmware/go2/.env.go2 \
  --command config \
  --no-serial \
  --print-json-secrets \
| mosquitto_pub -h <MQTT_HOST> -p 1883 -t "battlebang/devices/go2_05/config" -s
```

주요 hit tuning 필드:

- `piezo_ao_threshold_raw`: hit trigger threshold
- `piezo_ao_rearm_raw`: 재arm을 허용하는 low/raw 기준
- `hit_cooldown_ms`: hit 후 재arm 전 최소 cooldown
- `max_hits` / `hits_to_down`: full HP에서 down까지 필요한 accepted hit 수
- `hit_flash_ms`: hit 직후 HP bar flash 지속 시간
- `led_brightness`: HP bar brightness

### Reset

Reset/recovery는 legacy topic 이름을 유지하되 `reset_hit_state=true`만 정상 운영 명령으로 사용합니다. 이 명령은 ADC latch, offline queue, local hit count/HP/down, display 상태를 초기화합니다.

Power cycle도 같은 HP 초기화 정책을 따릅니다. ESP는 `accepted_hit_count`/`hp_remaining`/`down`을 NVS에 저장하지 않으며, 부팅할 때 NVS의 `max_hits` 룰만 읽고 full HP 상태로 시작합니다.

```json
{
  "schema_version": 1,
  "command": "ring_display",
  "robot_id": "go2_05",
  "reset_hit_state": true
}
```

### Debug/maintenance display override

정상 hit마다 Command Center가 `ring_display`를 보내면 ESP는 무시합니다. 벤치/정비용으로 LED bar를 강제로 덮어쓰려면 `debug_override=true` 또는 `maintenance_override=true`를 명시해야 합니다.

```json
{
  "schema_version": 1,
  "command": "ring_display",
  "robot_id": "go2_05",
  "debug_override": true,
  "ring_fill_ratio": 0.5,
  "down": false,
  "ring_display_mode": "active",
  "ttl_ms": 1000
}
```
