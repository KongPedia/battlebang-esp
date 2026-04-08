# Turret V2 현재 스펙 문서

이 문서는 **`src/turret_demo_v2_mod.cpp` 기준**으로 현재 turret v2가 어떻게 동작하는지 정확히 기록한 문서입니다.  
번호나 핀 정의는 임의로 바꾸지 않고, **현재 코드에 있는 값 그대로** 적었습니다.

---

## 1. 한 줄 요약

- 제어 입력 경로: **ESP32 기본 `Serial`**
- 보레이트: **115200**
- 입력 명령:
  - `x,y` → 목표 좌표 지정
  - `d` → DEAD 모드
  - `r` → IDLE 복귀
- 현재 터렛 기준 좌표:
  - `x_turret = -300.0`
  - `y_turret = 470.0`
  - `z_turret = 134.5`
  - `z_target = 70.0`
- 현재 코드에는 MQTT / Wi-Fi / Ethernet / `turret_id` 개념이 없음

---

## 2. 현재 소스 기준 파일

- 주 기준: `src/turret_demo_v2_mod.cpp`
- 참고 비교: `src/turret_demo.cpp`
- 현재 운영 가정: `src/turret_demo_v2_mod.cpp`를 Arduino IDE에서 터렛용 스케치처럼 사용

현재 저장소의 기본 README / `src/main.cpp`는 다른 펌웨어 흐름을 설명하고 있으므로,  
**turret v2 문서는 `turret_demo_v2_mod.cpp`를 별도 turret 제어 소스로 보고 해석**해야 합니다.

`turret_demo_v2_mod.cpp`가 `turret_demo.cpp`와 다른 점:

- idle에서 **yaw + pitch 둘 다 sweep**
- 현재 터렛 좌표가 다름
- `ESC_RUN_US = 1800`
- 사실상 현재 운용 기준은 v2 mod 파일

---

## 3. 현재 통신 방식

## 3.1 어떤 시리얼을 쓰는가

현재 코드는 아래처럼 시작합니다.

- `Serial.begin(115200);`

즉 현재 turret v2는:

- **ESP32 기본 시리얼 포트(`Serial`)**
- **115200 baud**

를 사용합니다.

현재 파일 안에는 아래가 **없습니다**.

- `Serial2.begin(...)`
- UART RX/TX 핀 지정
- MQTT subscribe/publish
- TCP/IP socket

따라서 현재 입력은 일반적으로 아래 중 하나입니다.

- Arduino IDE Serial Monitor
- USB 시리얼 연결
- 기본 UART0에 연결된 외부 장치

정리하면 현재 기준에서 **명령 입력은 USB 시리얼/기본 Serial** 입니다.

---

## 3.2 입력 명령 형식

현재 허용되는 입력은 아래 3개뿐입니다.

### 1) 좌표 명령

```text
x,y
```

예:

```text
100,50
-170,190
12.5,-33.75
```

의미:

- `x_target`
- `y_target`

를 입력해서 yaw/pitch 목표각을 계산합니다.

### 2) DEAD 모드

```text
d
```

또는

```text
D
```

의미:

- 현재 yaw를 고정
- pitch를 최대 위쪽으로 보냄
- 발사 중이어도 즉시 끊고 DEAD 모드 진입

### 3) IDLE 복귀

```text
r
```

또는

```text
R
```

의미:

- 발사 중이어도 즉시 끊고 IDLE 모드 복귀

---

## 3.3 입력 파싱 규칙

현재 파서는 비차단 방식입니다.

### `d` / `r`

- 버퍼가 비어 있는 상태에서
- `d`, `D`, `r`, `R` 한 글자가 들어오면
- **줄바꿈 없이 즉시 처리**

### `x,y`

- 일반 문자는 버퍼에 쌓고
- `\n` 또는 `\r` 이 들어오면 한 줄 명령으로 처리

즉 실무적으로는:

- `d`
- `r`

은 바로 보내도 되고,

- `100,50\n`
- `-170,190\r\n`

처럼 좌표 명령은 줄 종료를 붙이는 것이 안전합니다.

### 버퍼 크기

- 내부 버퍼: `char serialBuf[64]`
- 실제 최대 입력 길이: **63자 + null 종료**
- 넘치면:
  - 버퍼 초기화
  - `"[WARN] serial buffer overflow. input dropped."` 출력

### 숫자 파싱 주의점

- `String::toFloat()`를 사용하므로
- 숫자가 아닌 문자열이 들어가도 일부 경우 `0.0`으로 처리될 수 있습니다.

예:

- `abc,100` → `x = 0.0`, `y = 100.0`처럼 해석될 수 있음

즉 현재 파서는 **엄격 검증형이 아니라 느슨한 파서** 입니다.

---

## 4. 현재 핀맵

아래 번호는 현재 코드 기준의 정확한 핀 번호입니다.

| 용도 | 핀 번호 | 비고 |
|---|---:|---|
| YAW 가변저항(ADC) | 34 | `YAW_POT_PIN` |
| YAW 서보 | 18 | `YAW_SERVO_PIN` |
| PITCH 가변저항(ADC) | 35 | `PITCH_POT_PIN` |
| PITCH 서보 | 19 | `PITCH_SERVO_PIN` |
| Relay CH1 | 21 | `RELAY_CH1_PIN` |
| Relay CH2 | 22 | `RELAY_CH2_PIN` |
| Relay CH3 | 23 | `RELAY_CH3_PIN` |
| ESC | 25 | `ESC_PIN` |

### Relay 논리

- `RELAY_ACTIVE_LOW = true`

즉 현재 기준:

- `LOW = ON`
- `HIGH = OFF`

입니다.

---

## 5. 현재 좌표/단위 기준

현재 코드의 좌표 관련 하드코딩 값:

```text
x_turret = -300.0
y_turret = 470.0
z_turret = 134.5
z_target = 70.0
```

현재 출력 문자열에 `Target X [cm]`, `Target Y [cm]`가 찍히고,  
탄도 계산 상수도 `v0_cm_s`, `g_cm_s2`를 사용하므로 **현재 좌표 단위는 cm 기준으로 보는 것이 맞습니다.**

즉 예시로 자주 말한:

```text
-170,190
```

도 현재 코드 기준에서는 **cm 좌표 입력**으로 해석됩니다.

---

## 6. 현재 제어 모드

현재 모드는 3개입니다.

### MODE_IDLE

- yaw sweep
- pitch sweep

설정값:

- yaw 범위: `-70 deg ~ +70 deg`
- yaw 속도: `25 deg/s`
- pitch 범위: `-30 deg ~ +30 deg`
- pitch 속도: `30 deg/s`

### MODE_TARGET

- `x,y`를 받아 yaw/pitch 목표각 계산
- 목표 도달 시 자동 발사
- 발사 완료 후 다시 IDLE 복귀

### MODE_DEAD

- 현재 yaw 고정
- pitch를 최대 위로

---

## 7. 좌표 -> 각도 계산 방식

## 7.1 yaw

`computeYawDeg(x_target, y_target, x_turret, y_turret)`로 계산합니다.

개념상:

- 터렛 위치에서 원점 방향 벡터
- 터렛 위치에서 타겟 방향 벡터

사이의 각을 외적/내적으로 계산합니다.

이후 yaw는 최종적으로:

- 최소 `-140 deg`
- 최대 `+140 deg`

로 클리핑됩니다.

## 7.2 pitch

`computePitchDeg(...)`에서 포물선 탄도(저각 발사)로 계산합니다.

현재 상수:

- 발사 속도: `v0_cm_s = 3962.4`
- 중력 가속도: `g_cm_s2 = 981.0`

pitch 계산이 불가능한 경우:

- 너무 가까움
- 판별식 `disc < 0`

이면 `pitchValid = false`가 됩니다.

다만 실제 적용 pitch는 이후에도 클램프되어 사용됩니다.

---

## 8. 센서 / 제어 파라미터

## 8.1 ADC 제한

| 항목 | 값 |
|---|---:|
| YAW_LOW_CUT | 300 |
| YAW_HIGH_CUT | 3700 |
| PITCH_LOW_CUT | 1700 |
| PITCH_HIGH_CUT | 2400 |

ADC는:

- 12-bit (`analogReadResolution(12)`)
- 8회 평균
- 샘플 간 `500us` 지연

방식으로 읽습니다.

## 8.2 PID

| 항목 | 값 |
|---|---:|
| `Kp` | 0.80 |
| `Ki` | 0.020 |
| `Kd` | 0.15 |
| `YAW_DEADBAND` | 15.0 |
| `PITCH_DEADBAND` | 20.0 |
| `YAW_MIN_DRIVE` | 85.0 |
| `PITCH_MIN_DRIVE` | 75.0 |
| `YAW_I_LIMIT` | 3000.0 |
| `PITCH_I_LIMIT` | 3000.0 |

목표 도달 판정:

- yaw 오차 `<= 2 deg`
- pitch 오차 `<= 2 deg`

---

## 9. 발사 시퀀스

현재 발사는 **직접 `fire` 명령을 받는 구조가 아닙니다.**

현재 구조는:

1. `x,y` 입력
2. MODE_TARGET 진입
3. 조준 완료
4. 자동 발사 시작

입니다.

### 발사 시작 조건

- `MODE_TARGET`
- `fireState == FIRE_IDLE`
- 아직 이번 타겟에 대해 발사 안 함
- `isAimReached() == true`

### 발사 순서

현재 순서는 정확히 아래입니다.

1. `CH2 ON`
2. 250ms 대기
3. `CH1 ON`
4. 250ms 대기
5. `CH3 ON`
6. 250ms 대기
7. `ESC = 1800us`
8. 5000ms 유지
9. `ESC = 1000us`
10. 250ms 대기
11. `CH3 OFF`
12. 250ms 대기
13. `CH1 OFF`
14. 250ms 대기
15. `CH2 OFF`
16. IDLE 복귀

### 타이밍 상수

| 항목 | 값 |
|---|---:|
| `RELAY_STEP_DELAY_MS` | 250 |
| `FIRE_HOLD_MS` | 5000 |
| `ESC_RUN_US` | 1800 |
| `ESC_STOP_US` | 1000 |

### 중단 가능 여부

발사 중에도 아래는 즉시 먹습니다.

- `d`
- `r`

이 경우:

- 발사 상태머신 중단
- ESC 정지
- Relay 전체 OFF
- 새 모드로 전환

---

## 10. 현재 Serial 디버그 출력

주기:

- `100ms`

출력 정보:

- 현재 모드
- 마지막 타겟 `TargetXY`
- YAW raw / warp / current / target
- PITCH raw / current / target
- pitch 유효 여부
- yaw 방향(CW/CCW)
- 조준 완료 여부
- 발사 상태

즉 현재 Serial은:

1. **입력 명령 채널**
2. **상태 디버깅 채널**

을 같이 쓰고 있습니다.

---

## 11. 현재 코드의 제한사항

현재 기준으로 아직 없는 것:

1. `turret_id`
2. 터렛별 좌표 외부 설정 파일
3. MQTT 명령 수신
4. `target`에 포함된 `z` 입력
5. 직접 `fire` 명령 처리
6. 엄격한 입력 검증

즉 지금은 **단일 터렛, 단일 Serial 제어, 좌표 하드코딩 모델** 입니다.

---

## 12. 구현 전에 꼭 지켜야 할 해석

이 문서 기준에서 현재 turret v2의 사실은 아래입니다.

- 현재 통신은 `Serial @ 115200`
- 현재 좌표 입력은 `x,y`
- 현재 좌표 단위는 cm 기준
- 현재 터렛 기준 위치는 `(-300, 470, 134.5)`
- 현재 타겟 높이는 `70.0`
- 현재 발사는 자동 조준 후 자동 트리거
- 현재 핀 번호는 34, 18, 35, 19, 21, 22, 23, 25

이 값들을 기준선으로 두고 이후 리팩터링해야 합니다.
