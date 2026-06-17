# Integrated Go2/Nixo ESP ↔ Command Center MQTT 계약

Integrated Go2/Nixo ESP는 Command Center와 직접 MQTT로 통신합니다. ESP는 piezo AO ADC raw 값이 설정 threshold를 넘는 입력만 `hit_candidate`로 보내고, 최종 accept/reject, scoring/down/LED 표시 정책은 Command Center가 소유합니다.

## Topic

기본 prefix는 `battlebang/hit`입니다.

```text
ESP -> Command Center
battlebang/hit/{robot_id}/events

Command Center -> ESP
battlebang/hit/{robot_id}/ring_display/command
```

## ESP -> Command Center: hit_candidate

피에조 센서 AO ADC raw 값이 firmware threshold 이상으로 올라오면 ESP가 후보 이벤트를 보냅니다. D0 디지털 출력은 hit 판정에 사용하지 않고 debug readback으로만 남깁니다. ESP는 점수/HP/down을 계산하지 않고 `hit=true` 후보와 관측된 `peak`/`threshold`를 보냅니다. 최종 hit 수락/거절, 플레이어 난이도별 score/down/display 계산은 Command Center가 담당합니다. MQTT publish가 실패한 hit는 RAM queue에 보관되고, 재연결 후 같은 event topic으로 다시 publish됩니다.

```json
{
  "schema_version": 1,
  "event": "hit_candidate",
  "robot_id": "go2_05",
  "sensor_id": "piezo_t1",
  "sequence": 1,
  "hit": true,
  "peak": 2140,
  "threshold": 1800,
  "firmware_ts_ms": 12345,
  "firmware": "go2_nixo",
  "firmware_role": "integrated_hit_led_nixo",
  "mac_suffix": "948C",
  "client_id": "battlebang-hit-go2_05-go2_nixo-948C",
  "metadata": {
    "firmware": "go2_nixo",
    "firmware_role": "integrated_hit_led_nixo",
    "mac_suffix": "948C",
    "client_id": "battlebang-hit-go2_05-go2_nixo-948C",
    "hit_source": "piezo_ao_adc_threshold",
    "adc_peak_raw": 2140,
    "adc_threshold_raw": 1800
  }
}
```

재전송된 hit는 원래 발생 시각을 유지하고 queue metadata를 추가합니다. 서버는 이 metadata를 관측/디버그에 사용할 수 있지만, 최종 accept/drop은 기존 Command Center hit policy가 결정합니다.

```json
{
  "schema_version": 1,
  "event": "hit_candidate",
  "robot_id": "go2_05",
  "sensor_id": "piezo_t1",
  "sequence": 7,
  "hit": true,
  "peak": 2188,
  "threshold": 1800,
  "firmware_ts_ms": 45678,
  "firmware": "go2_nixo",
  "firmware_role": "integrated_hit_led_nixo",
  "mac_suffix": "948C",
  "client_id": "battlebang-hit-go2_05-go2_nixo-948C",
  "queued": true,
  "queued_for_ms": 1200,
  "metadata": {
    "firmware": "go2_nixo",
    "firmware_role": "integrated_hit_led_nixo",
    "mac_suffix": "948C",
    "client_id": "battlebang-hit-go2_05-go2_nixo-948C",
    "hit_source": "piezo_ao_adc_threshold",
    "adc_peak_raw": 2188,
    "adc_threshold_raw": 1800,
    "queued": true,
    "queued_for_ms": 1200,
    "queue_depth": 3,
    "queue_dropped": 0
  }
}
```

## ESP -> Command Center: heartbeat

ESP 온라인 여부와 표시 경로 상태를 Command Center가 판단할 수 있게 주기적으로 보냅니다.

```json
{
  "schema_version": 1,
  "event": "heartbeat",
  "robot_id": "go2_05",
  "sensor_id": "hit_ring",
  "sequence": 10,
  "firmware_ts_ms": 20000,
  "mode": "mqtt_connected",
  "firmware": "go2_nixo",
  "firmware_role": "integrated_hit_led_nixo",
  "mac_suffix": "948C",
  "client_id": "battlebang-hit-go2_05-go2_nixo-948C",
  "metadata": {
    "firmware": "go2_nixo",
    "firmware_role": "integrated_hit_led_nixo",
    "mac_suffix": "948C",
    "client_id": "battlebang-hit-go2_05-go2_nixo-948C",
    "offline_queue_count": 0,
    "offline_queue_capacity": 32,
    "offline_queue_dropped": 0
  }
}
```

- `mode=direct`: Command Center가 내려준 `ring_display`를 렌더링 중
- `mode=mqtt_connected`: MQTT는 연결되어 있지만 현재 유효한 remote display가 없음
- `mode=mqtt_disconnected`: MQTT 연결 없음

## Command Center -> ESP: ring_display

Command Center는 LED로 렌더링하는 데 필요한 semantic display state만 보냅니다. MQTT topic/field 이름은 호환성을 위해 `ring_display`/`ring_*`를 유지하지만, 현재 ESP는 이를 HP bar LED layout으로 렌더링합니다. 기존 ring LED는 이 payload가 아니라 Nixo fire/cooldown 상태 표시용입니다.

```json
{
  "schema_version": 1,
  "command": "ring_display",
  "robot_id": "go2_05",
  "ring_fill_ratio": 0.65,
  "down": false,
  "ring_display_mode": "hit_flash",
  "ttl_ms": 1000,
  "reset_hit_state": false
}
```

ESP는 이 payload를 받아 84개 HP bar LED를 갱신합니다.

- `ring_fill_ratio`: HP 잔량 비율입니다. 1.0이면 28개 bar 그룹 전체 green, 0.5이면 앞쪽 14개 그룹 green/뒤쪽 14개 그룹 red, 0이면 전체 red입니다.
- `down`: 다운 상태 표시 여부
- `ring_display_mode`: `idle`, `active`, `hit_flash`, `down`, `stale`, `disabled` 등 semantic mode
- `ttl_ms`: Command Center 표시가 유효한 시간
- `reset_hit_state`: true면 ESP 센서 latch/flag와 현재 remote display를 초기화합니다. Command Center의 `POST /api/robots/{robot_id}/hit/reset` 응답 command에서 true로 내려옵니다.


Nixo fire command는 별도 topic `battlebang/nixo/{nixo_id}/command`로 수신하며, hit/HP bar MQTT 계약은 Go2 AO hit ESP와 동일합니다.
