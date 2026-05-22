# Go2 ESP ↔ Command Center MQTT 계약

Go2 ESP는 Command Center와 직접 MQTT로 통신합니다. Jetson UART는 Command Center 응답이 없을 때의 로컬 fallback 보험으로만 남깁니다.

## Topic

기본 prefix는 `battlebang/hit`입니다.

```text
ESP -> Command Center
battlebang/hit/{robot_id}/events

Command Center -> ESP
battlebang/hit/{robot_id}/ring_display/command
```

## ESP -> Command Center: hit_candidate

피에조 센서 값이 임계값을 넘으면 ESP가 후보 이벤트를 보냅니다. 최종 hit 수락/거절, HP, score 계산은 Command Center가 담당합니다.

```json
{
  "schema_version": 1,
  "event": "hit_candidate",
  "robot_id": "go2_05",
  "sensor_id": "piezo_t1",
  "sequence": 1,
  "peak": 4095,
  "threshold": 3000,
  "firmware_ts_ms": 12345
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
  "mode": "mqtt_connected"
}
```

- `mode=direct`: Command Center가 내려준 `ring_display`를 렌더링 중
- `mode=mqtt_connected`: MQTT는 연결되어 있지만 현재 유효한 remote display가 없음
- `mode=fallback`: MQTT 미연결/미설정 상태

## Command Center -> ESP: ring_display

Command Center는 HP 값 자체나 score/debug 정보를 ESP에 모두 넘기지 않습니다. ESP가 LED로 렌더링하는 데 필요한 semantic display state만 보냅니다.

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

ESP는 이 payload를 받아 ring LED를 갱신합니다.

- `ring_fill_ratio`: LED fill 비율
- `down`: 다운 상태 표시 여부
- `ring_display_mode`: `idle`, `active`, `hit_flash`, `down` 등 semantic mode
- `ttl_ms`: Command Center 표시가 유효한 시간
- `reset_hit_state`: true면 ESP 내부 fallback HP/down 상태와 센서 플래그를 초기화합니다. 일반 hit display command에서는 생략하거나 false로 둡니다. Command Center의 `POST /api/robots/{robot_id}/hit/reset` 응답 command에서 true로 내려옵니다.
