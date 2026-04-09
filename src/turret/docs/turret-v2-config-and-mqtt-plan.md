# Turret V2 설정 분리 / MQTT 연동 계획 문서

이 문서는 현재 `src/turret_demo_v2_mod.cpp`를 바로 고치기 전에,  
**여러 대의 터렛을 효율적으로 운용할 수 있는 구조**를 문서로 먼저 정리한 것입니다.

목표는 아래 2가지입니다.

1. 터렛마다 다른 좌표/식별자 값을 하드코딩에서 분리
2. 이후 MQTT 기반 `turret_1` ~ `turret_6` 구조로 자연스럽게 연결

---

## 1. 현재 문제

현재 turret v2는 아래 값이 코드 안에 고정되어 있습니다.

```text
x_turret = -300.0
y_turret = 470.0
z_turret = 134.5
z_target = 70.0
```

이 구조의 문제:

1. 터렛이 여러 대면 파일을 복사해서 숫자만 바꾸게 됨
2. 잘못된 숫자 덮어쓰기 위험이 큼
3. 어떤 바이너리가 어느 터렛용인지 추적이 어려움
4. 나중에 MQTT `turret_id`와 연결하기 어려움

---

## 2. 앞으로 분리해야 하는 최소 설정 항목

여러 대 운용 기준으로 최소 아래 항목은 제어 로직과 분리하는 것이 좋습니다.

| 항목 | 예시 | 설명 |
|---|---|---|
| `turret_id` | `turret_1` | MQTT / 로깅 / 식별용 |
| `x_turret_cm` | `-300.0` | 터렛 x 기준 위치 |
| `y_turret_cm` | `470.0` | 터렛 y 기준 위치 |
| `z_turret_cm` | `134.5` | 터렛 높이 |
| `z_target_cm` | `70.0` | 기본 타겟 높이 |
| `command_center_ip` | `192.168.0.10` | 이후 MQTT broker 주소 |
| `mqtt_topic` 또는 템플릿 | `battlebang/turrets/{turret_id}/command` | 수신 토픽 |

추가로 분리 후보:

- yaw/pitch calibration
- PID 게인
- idle sweep 범위/속도
- 발사 타이밍

하지만 1차 단계에서는 **좌표 + turret_id + broker/topic 정보**만 분리해도 효과가 큽니다.

---

## 3. 추천 구조

## 3.1 제어 로직과 설정을 분리

추천 방향:

- 제어 로직 파일: `turret_control.cpp`
- 설정 헤더: `turret_profile.h`

개념 예시:

```cpp
struct TurretProfile {
  const char* turret_id;
  float x_turret_cm;
  float y_turret_cm;
  float z_turret_cm;
  float z_target_cm;
  const char* mqtt_broker_host;
  uint16_t mqtt_broker_port;
};
```

그리고 현재 코드의:

- `x_turret`
- `y_turret`
- `z_turret`
- `z_target`

대신

- `profile.x_turret_cm`
- `profile.y_turret_cm`
- `profile.z_turret_cm`
- `profile.z_target_cm`

형태로 읽게 만드는 것이 좋습니다.

핵심은:

- **PID / 발사 / 시리얼 파서 / 좌표 계산 로직은 공통**
- **터렛별 차이는 profile로만 관리**

입니다.

---

## 3.2 현재 단계에서 가장 실용적인 주입 방식

사용 방식이 **Arduino IDE로 개별 펌웨어 업로드**인 점을 고려하면, 아래 우선순위를 추천합니다.

현재 이 저장소의 기본 문서/엔트리포인트는 turret v2가 아니라 다른 ESP32 펌웨어 흐름을 설명하고 있으므로,  
turret v2는 당분간 **독립 스케치처럼 관리**하는 쪽이 혼선을 줄입니다.

### 추천 1: 빌드 전 생성된 `turret_profile.h` 사용

가장 현실적인 방식입니다.

흐름:

1. 각 터렛별 프로필 파일을 따로 둠
2. 업로드 직전에 `turret_profile.h`를 생성하거나 복사
3. 같은 소스 코드로 빌드

예:

```text
profiles/
  turret_1.h
  turret_2.h
  turret_3.h
```

빌드 직전:

- `turret_3.h` → `turret_profile.h` 로 복사

장점:

- Arduino IDE에서도 적용 가능
- 실수 지점이 적음
- 여러 터렛 운영 시 어떤 프로필을 썼는지 관리하기 쉬움

단점:

- 완전 자동화까지는 스크립트가 필요

### 추천 2: 빌드 플래그로 주입

예:

```text
-DTURRET_ID=\"turret_1\" -DX_TURRET_CM=-300.0f ...
```

장점:

- 자동화에 좋음
- CI/스크립트/CLI 빌드에 적합

단점:

- Arduino IDE GUI만 쓰면 관리가 불편함

즉:

- **지금 당장**은 `turret_profile.h`
- **대량 관리 단계**에서는 Arduino CLI / PlatformIO + build flags

가 가장 효율적입니다.

---

## 4. 여러 터렛 운영 시 권장 식별 체계

현재 사용 예정 식별자는 아래입니다.

- `turret_1`
- `turret_2`
- `turret_3`
- `turret_4`
- `turret_5`
- `turret_6`

이 값은 이후 MQTT topic과 payload에 그대로 쓰는 것이 좋습니다.

권장 원칙:

1. **펌웨어 내부 식별자도 `turret_1` 형식으로 통일**
2. 파일명 / 프로필명 / MQTT topic / 로그 문자열을 동일하게 맞춤
3. 현장 라벨링도 동일하게 맞춤

예:

- 펌웨어 프로필: `turret_1.h`
- 장비 라벨: `turret_1`
- MQTT topic 대상: `battlebang/turrets/turret_1/command`

---

## 5. `battlebang-demo` 참고 결과

참고한 외부 워크스페이스 파일:

- `battlebang-demo/config.toml`
- `battlebang-demo/turrets.toml`
- `battlebang-demo/battlebang/models.py`
- `battlebang-demo/battlebang/publisher.py`
- `battlebang-demo/docs/mqtt-contract.md`

중요:

- `battlebang-demo`는 현재 구조상 **명령을 발행하는 쪽(publisher / command sender)** 입니다.
- 앞으로 ESP turret 펌웨어는 그 반대편에서 **구독하는 쪽(subscriber / command receiver)** 으로 맞물리게 됩니다.

현재 demo 쪽에서 파악되는 구조:

### MQTT topic 템플릿

```text
battlebang/turrets/{turret_id}/command
```

### 지원 명령

- `idle`
- `target`
- `fire`
- `dead`

### `target` payload 예시 구조

```json
{
  "command": "target",
  "group_id": "group_1",
  "turret_id": "turret_1",
  "target": {
    "x": 1.0,
    "y": 0.0,
    "z": 0.5
  }
}
```

### 등록된 터렛 ID

- `turret_1`
- `turret_2`
- `turret_3`
- `turret_4`
- `turret_5`
- `turret_6`

즉 나중에 ESP 쪽도 **터렛 하나당 하나의 topic**을 구독하는 구조로 가는 것이 자연스럽습니다.

---

## 6. 현재 turret v2와 MQTT 모델의 차이점

지금 바로 MQTT를 붙이기 전에 아래 차이를 먼저 인정해야 합니다.

## 6.1 명령 이름 차이

demo/MQTT 쪽:

- `idle`
- `target`
- `fire`
- `dead`

현재 turret v2 serial 쪽:

- `r` → idle에 해당
- `x,y` → target에 해당
- `d` → dead에 해당
- `fire` 직접 명령 없음

즉 나중에 MQTT를 붙일 때는 최소한 아래 매핑이 필요합니다.

| MQTT 명령 | 현재 turret v2 내부 동작 |
|---|---|
| `idle` | `abortAndEnterIdleMode()` |
| `dead` | `abortAndEnterDeadMode()` |
| `target` | `applyTargetCommand(x, y)` |
| `fire` | 별도 정책 필요 |

특히 `fire`는 현재 코드에 직접 대응이 없습니다.

선택지는 2개입니다.

1. `fire` 명령이 오면 현재 조준점에서 즉시 발사
2. `fire` 명령은 지원하지 않고 `target` 후 자동 발사만 유지

이건 구현 전에 먼저 결정해야 합니다.

## 6.2 좌표 형식 차이

현재 turret v2:

- 입력: `x,y`
- `z_target`은 코드 안에 고정
- 단위는 cm 기준

demo/MQTT:

- `target.x`
- `target.y`
- `target.z`

즉 다음 중 하나를 정해야 합니다.

1. MQTT `z`를 받아서 실제 `z_target`으로 사용
2. MQTT `z`는 무시하고 로컬 프로필의 `z_target_cm` 사용

개인적으로는 **터렛별 발사 계산 정확도**를 생각하면 `z`도 수신할 수 있게 여는 편이 좋습니다.

## 6.3 단위 차이 가능성

demo의 예시값은:

- `1.0`
- `0.5`

형태이고, 현재 turret v2는 사실상 **cm 기반**입니다.

따라서 MQTT 연동 전 반드시 결정해야 할 것:

- MQTT 좌표 단위를 **m**로 할지
- MQTT 좌표 단위를 **cm**로 할지

추천:

- 외부 계약은 m 또는 mm 중 하나로 명시
- 펌웨어 내부 계산은 cm로 통일
- 수신 시 한 번만 변환

예:

- MQTT는 m
- 펌웨어 진입 직후 `m -> cm` 변환

---

## 7. 추천 구현 순서

문서 기준으로 다음 구현 순서를 추천합니다.

### 단계 1: 프로필 분리

먼저 아래만 하드코딩에서 뺍니다.

- `turret_id`
- `x_turret`
- `y_turret`
- `z_turret`
- `z_target`

이 단계에서는 아직 Serial 유지.

### 단계 2: 내부 명령 API 정리

문자열 파서와 제어 로직을 분리합니다.

예:

- `commandIdle()`
- `commandDead()`
- `commandTarget(x, y, z)`
- `commandFire()`

이렇게 되면 입력이 Serial이든 MQTT든 같은 내부 API를 호출하면 됩니다.

### 단계 3: MQTT 수신 추가

ESP가 broker에 접속해서 `turret_id` 기준 topic 구독:

```text
battlebang/turrets/{turret_id}/command
```

### 단계 4: payload 매핑

JSON payload를 읽어서 내부 명령 API로 변환:

- `idle` → `commandIdle()`
- `dead` → `commandDead()`
- `target` → `commandTarget(...)`
- `fire` → 정책 결정 후 연결

---

## 8. 지금 문서 기준의 권장 결론

현재 요구사항에 가장 맞는 결론은 아래입니다.

1. **현재 turret v2 제어 로직은 유지**
2. **터렛별 값만 분리**
3. **`turret_id`를 펌웨어 기본 속성으로 추가**
4. **Serial 입력 모델을 내부 명령 API로 추상화**
5. **그 다음 MQTT를 붙임**

즉 순서는:

```text
하드코딩 제거 -> 터렛 프로필화 -> 내부 명령 API화 -> MQTT 연결
```

이 가장 안전합니다.

---

## 8.1 이번 구현에서 실제로 적용한 결정

이번 작업에서는 위 결론 중 핵심을 실제 코드에 반영했습니다.

### 추가된 파일

- `src/turret/main.cpp`
- `src/turret/turrets.json`

### 적용한 선택

1. **제어 로직은 한 파일 유지**
   - 너무 길게 쪼개지 않도록 `src/turret/main.cpp` 중심으로 구현
2. **터렛별 차이만 헤더/빌드 플래그로 분리**
   - `turrets.json`
3. **MQTT와 Serial 둘 다 같은 내부 제어 흐름으로 연결**
4. **topic 기본값은 demo 구조와 맞춤**
   - `battlebang/turrets/{turret_id}/command`
5. **MQTT 좌표는 기본적으로 meters -> cm 변환**
   - `TURRET_MQTT_COORDS_IN_METERS=1`
6. **기본 동작은 target 후 자동 발사 유지**
   - `TURRET_AUTO_FIRE_ON_TARGET=1`
   - 필요하면 빌드 시 0으로 끌 수 있음

### 새 펌웨어에서 지원하는 명령

#### Serial

- `x,y`
- `x,y,z`
- `f` / `fire`
- `d` / `dead`
- `r` / `idle`

#### MQTT JSON

- `idle`
- `target`
- `fire`
- `dead`

즉 현재는 **기존 Serial 디버깅 경로를 유지하면서 MQTT subscriber 펌웨어로 확장된 상태**입니다.

---

## 9. 다음 구현 때 바로 옮겨야 하는 핵심 체크리스트

- [ ] `x_turret`, `y_turret`, `z_turret`, `z_target` 분리
- [ ] `turret_id` 추가
- [ ] 현재 `Serial` 명령을 내부 command 함수로 분리
- [ ] `fire` 직접 명령 정책 결정
- [ ] MQTT 좌표 단위 확정
- [ ] MQTT payload의 `z` 사용 정책 확정
- [ ] topic 규칙을 `turret_1` ~ `turret_6` 기준으로 확정

이 체크리스트가 끝나면 그 다음부터 실제 C++ 리팩터링을 진행하면 됩니다.
