# Turret MQTT Command Contract

이 문서는 **command center / 테스트 도구가 turret 펌웨어로 MQTT 명령을 보낼 때 따라야 하는 공식 contract** 입니다.

기준 구현:

- 수신 펌웨어: `src/turret/main.cpp`
- 기본 설정: `src/turret/build_config.h` + `src/turret/turrets.json` + `src/turret/local_secrets.h`

> 현재 contract 자체와 상태 전이는 코드/단위 테스트로 검증했고, 실보드 기준 활성 preset은 `turret_5` 입니다.

---

## 1. 범위

이 contract는 **외부 publisher -> turret firmware** 방향만 정의합니다.

현재 펌웨어는:

- MQTT **command subscribe** 는 구현됨
- MQTT **status / heartbeat publish** 는 아직 구현되지 않음
- 기본 동작은 **event-driven mode control**

즉, 지금은 응답 topic이 없고, 모니터링은 주로 **Serial log** 로 확인합니다.

---

## 2. Command topic

기본 topic:

```text
battlebang/turrets/{turret_id}/command
```

예:

```text
battlebang/turrets/turret_1/command
battlebang/turrets/turret_2/command
battlebang/turrets/turret_3/command
```

구성 규칙:

- prefix: `battlebang/turrets`
- turret segment: 빌드 시 주입된 `TURRET_ID`
- suffix: `command`

현재 기본 매크로:

```cpp
TURRET_MQTT_TOPIC_PREFIX = "battlebang/turrets"
TURRET_ID = "turret_5" // 예시
```

따라서 `turret_5` 펌웨어는 아래 topic만 subscribe 합니다.

```text
battlebang/turrets/turret_5/command
```

---

## 3. Payload 형식

공식 형식은 **JSON object** 입니다.

기본 필드명:

| 필드 | 타입 | 필수 | 설명 |
|---|---|---:|---|
| `command` | string | Y | 명령 이름 |
| `turret_id` | string | N | 보낼 대상 터렛 ID. 있으면 펌웨어의 `TURRET_ID`와 일치해야 함 |
| `target` | object | target일 때 Y | 목표 좌표 |
| `target.x` | number | target일 때 Y | 목표 X |
| `target.y` | number | target일 때 Y | 목표 Y |
| `target.z` | number | N | 목표 Z. 없으면 빌드값 `TURRET_DEFAULT_TARGET_Z_CM` 사용 |

현재 기본 필드명 매크로:

```cpp
TURRET_MQTT_FIELD_COMMAND   = "command"
TURRET_MQTT_FIELD_TURRET_ID = "turret_id"
TURRET_MQTT_FIELD_TARGET    = "target"
TURRET_MQTT_FIELD_TARGET_X  = "x"
TURRET_MQTT_FIELD_TARGET_Y  = "y"
TURRET_MQTT_FIELD_TARGET_Z  = "z"
```

### 최대 payload 크기

현재 펌웨어 수신 버퍼는 512 bytes 입니다.

실무상 **payload는 511 bytes 이하 JSON** 으로 보내는 것을 기준으로 합니다.

---

## 4. 지원 command

## 4.1 `idle`

의미:

- 현재 발사 시퀀스 중이면 중단
- 출력 safe off
- IDLE 모드 진입
- 이때만 idle searching / sweep 동작 시작

예시:

```json
{
  "command": "idle",
  "turret_id": "turret_1"
}
```

---

## 4.2 `dead`

의미:

- 현재 발사 시퀀스 중이면 중단
- 출력 safe off
- DEAD 모드 진입

예시:

```json
{
  "command": "dead",
  "turret_id": "turret_1"
}
```

---

## 4.3 `fire`

의미:

- 바로 발사 가능하면 즉시 발사
- 현재 `target` 모드이고 아직 조준이 안 맞았어도 현재 각도 기준으로 즉시 발사
- DEAD 모드면 무시
- `engagement_duration_ms`, `duration_ms`, `fire_duration_ms` 중 하나가 있으면 해당 시간만큼 firing hold window를 유지
- duration field가 없으면 backward-compatible 기본 hold window는 **1초**
- duration은 안전 범위로 clamp됨: 최소 1초, 최대 60초
- 이미 발사 중이면 새 fire 명령을 hold-window refresh로 처리

예시:

```json
{
  "command": "fire",
  "turret_id": "turret_1",
  "engagement_duration_ms": 5590,
  "fire_interval_ms": 500,
  "engagement_id": "nexus-warning-keyboard"
}
```

---

## 4.4 `target`

의미:

- 목표 좌표를 받아 yaw / pitch 계산
- TARGET 모드 진입
- **조준만 하고 자동 발사는 하지 않음**
- 실제 발사는 별도의 `fire` command 로만 시작

예시:

```json
{
  "command": "target",
  "turret_id": "turret_1",
  "target": {
    "x": 1.25,
    "y": -0.40,
    "z": 0.70
  }
}
```

### `target` 필수 규칙

- `target` object 가 있어야 함
- `target.x`, `target.y` 는 필수
- `target.z` 는 선택

`target.z` 가 없으면:

- 펌웨어는 `TURRET_DEFAULT_TARGET_Z_CM` 값을 사용

예:

```json
{
  "command": "target",
  "turret_id": "turret_1",
  "target": {
    "x": 1.25,
    "y": -0.40
  }
}
```

---

## 5. 좌표 단위

현재 기본 빌드값:

```cpp
TURRET_MQTT_COORDS_IN_METERS = 1
```

즉 현재 기본 contract는:

- `x`, `y`, `z` 를 **meters 단위**로 보냄
- 펌웨어 내부에서 **cm 로 변환**해서 사용

예:

```json
{
  "command": "target",
  "turret_id": "turret_1",
  "target": {
    "x": 1.25,
    "y": -0.40,
    "z": 0.70
  }
}
```

내부 해석:

- `x = 125.0 cm`
- `y = -40.0 cm`
- `z = 70.0 cm`

### 주의

만약 어떤 펌웨어 빌드에서:

```cpp
TURRET_MQTT_COORDS_IN_METERS = 0
```

이라면 같은 필드는 **cm 단위 그대로** 해석됩니다.

따라서 command center 쪽 기본 정책은:

> **모든 turret MQTT 좌표는 meters 로 보낸다.**

로 고정하는 것을 권장합니다.

---

## 6. `turret_id` 처리 규칙

`turret_id` 는 payload에서 **선택 필드**이지만, 실무에서는 **항상 넣는 것을 권장**합니다.

펌웨어 동작:

- `turret_id` 가 비어 있으면 command 처리 계속
- `turret_id` 가 있고, 펌웨어의 `TURRET_ID` 와 다르면 command 무시

즉 아래 payload를 `turret_1` topic에 보내도:

```json
{
  "command": "idle",
  "turret_id": "turret_2"
}
```

`turret_1` 펌웨어는 무시합니다.

---

## 7. 에러 / 무시 조건

아래 경우 command 가 거부되거나 무시될 수 있습니다.

- JSON 파싱 실패
- `command` 가 지원되지 않음
- `turret_id` mismatch
- `target` command 인데 `target` object 없음
- `target.x` 또는 `target.y` 없음
- payload 크기 초과
- 현재 모드/발사 상태상 실행 불가

예:

- 발사 중에 새 `target` → 무시
- DEAD 모드에서 `fire` → 무시

---

## 8. 상태 전이 규칙

현재 C++ 구현 기준 상태 전이는 아래처럼 해석하면 됩니다.

| 현재 상태 | `idle` | `dead` | `target` | `fire` |
|---|---|---|---|---|
| `HOLD` | `IDLE` 로 전이 | `DEAD` 로 전이 | `TARGET` 으로 전이 | 즉시 발사 가능 |
| `IDLE` | 유지 | `DEAD` 로 전이 | `TARGET` 으로 전이 | 발사 후 자동 idle 복귀 없음 |
| `TARGET` | `IDLE` 로 전이 | `DEAD` 로 전이 | 새 target으로 갱신 | 현재 각도 기준으로 즉시 발사 |
| `DEAD` | `IDLE` 로 전이 | 유지 | `TARGET` 으로 전이 | 무시 |
| 발사 중 | 발사 중단 후 `IDLE` | 발사 중단 후 `DEAD` | 무시 | duration metadata로 hold-window 갱신 또는 재시작 예약 |

즉 질문한 핵심인 아래 두 가지는 **현재 코드에서 이미 가능**합니다.

1. `DEAD -> IDLE`
2. `DEAD -> TARGET`

단, `DEAD -> FIRE` 는 막혀 있습니다.

### 중요한 동작 변경

- 부팅 직후 기본 모드는 `IDLE`이며 즉시 searching을 시작
- `idle` 명령은 언제든 `IDLE` searching으로 복귀시키는 명령
- `target` 은 조준만 수행
- `fire` 가 끝나도 자동으로 `idle` 로 돌아가지 않음
- `fire` 는 duration metadata가 있으면 그 시간만큼 유지된다. duration field가 없는 legacy command만 1초 hold-window를 사용한다. Command Center BTB-609 demo는 같은 `fire`를 반복 publish하지 않고 duration-bearing `fire` 1회와 final `idle` 1회를 보낸다.

---

## 9. Legacy / 디버그 호환 입력

현재 펌웨어는 **non-JSON payload가 들어오면 serial command parser 로 fallback** 합니다.

즉 아래 같은 raw payload도 동작할 수 있습니다.

```text
idle
fire
d
r
100,-50
100,-50,70
```

하지만 이것은 **공식 MQTT contract가 아니라 디버그/호환용 동작**입니다.

따라서 command center 구현은 반드시:

> **JSON payload contract만 사용**

하도록 합니다.

---

## 10. 추천 publish 예시

## 10.1 Python

```python
import json
import paho.mqtt.publish as publish

publish.single(
    "battlebang/turrets/turret_5/command",
    json.dumps({
        "command": "target",
        "turret_id": "turret_5",
        "target": {
            "x": 1.25,
            "y": -0.40,
            "z": 0.70,
        }
    }),
    hostname="127.0.0.1",
    port=1883,
)
```

## 10.2 mosquitto_pub

```bash
mosquitto_pub \
  -h 127.0.0.1 \
  -p 1883 \
  -t battlebang/turrets/turret_5/command \
  -m '{"command":"idle","turret_id":"turret_5"}'
```

---

## 11. 모니터링

현재 펌웨어는 MQTT status topic을 publish 하지 않습니다.

대신 Serial monitor 에서 아래를 확인할 수 있습니다.

- 구독 성공:
  - `[MQTT] subscribed to battlebang/turrets/turret_5/command`
- 수신 payload:
  - `[MQTT] topic=... payload=...`
- 연결 상태:
  - `[NET] WiFi=UP MQTT=UP`

---

## 12. 운영 규칙 요약

command center 기준으로는 아래만 기억하면 됩니다.

1. topic은 항상

```text
battlebang/turrets/{turret_id}/command
```

2. payload는 항상 JSON
3. `command` 는 `idle | dead | fire | target`
4. `target` 은 `target.x`, `target.y` 필수
5. 좌표 단위는 기본적으로 **meters**
6. `turret_id` 는 payload에도 넣는다
7. status 응답 topic은 아직 없다
