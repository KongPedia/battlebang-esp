# Turret V2 문서 모음

이 폴더는 현재 저장소 안에 있는 **turret v2 제어 파일의 현재 스펙**과, 이후 **터렛별 설정 분리 / MQTT 연동**으로 확장하기 위한 기준 문서를 모아두는 곳입니다.

## 현재 기준 소스

- 현재 분석 기준 파일: `src/turret_demo_v2_mod.cpp`
- 비교용 이전 파일: `src/turret_demo.cpp`
- 현재 저장소의 `src/main.cpp`는 별도 ESP32 게임/HP 펌웨어이며, turret v2 제어 파일과는 역할이 다릅니다.
- 현재 실사용 가정은 **`src/turret_demo_v2_mod.cpp`를 Arduino IDE 쪽 스케치 기준으로 사용해 개별 보드에 업로드**하는 방식입니다.
- 이번 작업으로 새 펌웨어 파일을 추가했습니다:
  - `src/turret/main.cpp`
  - `src/turret/build_config.h`

즉, **지금 turret v2 동작의 기준은 `src/turret_demo_v2_mod.cpp`** 입니다.

## 새 구현 결과

이번 변경으로 실제로 사용할 수 있는 새 펌웨어 골격이 추가되었습니다.

- 펌웨어 본체: `src/turret/main.cpp`
- 빌드 주입 설정: `src/turret/build_config.h`

설계 원칙:

- 파일을 과도하게 쪼개지 않음
- 제어 로직은 한 파일에 유지
- 터렛별 차이만 헤더/빌드 플래그로 주입

### 새 펌웨어가 지원하는 것

- 기존 turret v2 제어 로직 기반
- 기본 `Serial @ 115200` 수동 제어 유지
- MQTT subscribe 기반 명령 수신 추가
- turret ID / 좌표 / Wi-Fi / MQTT host를 빌드 시점에 주입 가능

### 새 펌웨어 명령

#### Serial

- `x,y`
- `x,y,z`
- `f` / `fire`
- `d` / `dead`
- `r` / `idle`

#### MQTT

- `idle`
- `target`
- `fire`
- `dead`

topic 기본값:

```text
battlebang/turrets/{turret_id}/command
```

공식 topic / payload 형식은 아래 contract 문서를 기준으로 합니다.

### 빌드 방법

#### PlatformIO

새 env:

```text
esp32dev_turret
esp32dev_turret_1
esp32dev_turret_2
esp32dev_turret_3
esp32dev_turret_4
esp32dev_turret_5
esp32dev_turret_6
```

예:

```bash
pio run -e esp32dev_turret
pio run -e esp32dev_turret -t upload
pio run -e esp32dev_turret_3 -t upload
```

#### 빌드 시 설정 주입

`platformio.ini`의 `build_flags` 또는 `src/turret/build_config.h` 수정으로 주입합니다.

#### Arduino IDE fallback

Arduino IDE를 계속 쓸 경우:

1. `src/turret/main.cpp` 내용을 스케치 본문으로 사용
2. `src/turret/build_config.h` 값 수정
3. 빌드/업로드

즉 **자동 주입까지 생각하면 PlatformIO 쪽이 더 적합하고**, Arduino IDE는 수동 수정 방식 fallback으로 보면 됩니다.

대표 항목:

- `TURRET_ID`
- `TURRET_X_CM`
- `TURRET_Y_CM`
- `TURRET_Z_CM`
- `TURRET_DEFAULT_TARGET_Z_CM`
- `TURRET_WIFI_SSID`
- `TURRET_WIFI_PASSWORD`
- `TURRET_MQTT_HOST`
- `TURRET_MQTT_PORT`
- `TURRET_MQTT_COORDS_IN_METERS`
- `TURRET_AUTO_FIRE_ON_TARGET`

### 현재 프리셋 테이블

| Env | Preset header | Turret ID | 좌표(cm) |
|---|---|---|---|
| `esp32dev_turret_1` | `src/turret/presets/turret_1.h` | `turret_1` | `(-300, 470, 134.5), z_target=70` |
| `esp32dev_turret_2` | `src/turret/presets/turret_2.h` | `turret_2` | `(-300, 470, 134.5), z_target=70` |
| `esp32dev_turret_3` | `src/turret/presets/turret_3.h` | `turret_3` | `(-170, 190, 134.5), z_target=70` |
| `esp32dev_turret_4` | `src/turret/presets/turret_4.h` | `turret_4` | `(-170, 190, 134.5), z_target=70` |
| `esp32dev_turret_5` | `src/turret/presets/turret_5.h` | `turret_5` | `(-300, 470, 134.5), z_target=70` |
| `esp32dev_turret_6` | `src/turret/presets/turret_6.h` | `turret_6` | `(-300, 470, 134.5), z_target=70` |

> 참고: 현재 실제 설치 좌표가 확인된 것은 일부 예시뿐이라, 나머지는 현재 v2 기본값을 넣어둔 상태입니다.

## 문서 목록

- `build-upload-example.md`
  - `turret_1` 기준 빌드/업로드 예시
  - local secrets 주입 방식
  - 민감정보 처리 원칙
- `mqtt-command-contract.md`
  - command center -> turret MQTT command contract
  - topic 규칙
  - payload JSON 형식
  - command별 필수/선택 필드와 예시
- `turret-v2-current-spec.md`
  - 현재 시리얼 통신 방식
  - 정확한 핀 번호
  - 모드/발사 시퀀스
  - 좌표 계산과 하드코딩된 값
- `turret-v2-config-and-mqtt-plan.md`
  - 터렛별 하드코딩 좌표를 분리하는 방법
  - Arduino IDE / 빌드 시점 주입 전략
  - `turret_1` ~ `turret_6` 구조와 MQTT 연계 시 고려사항

## 현재 꼭 기억할 점

1. 현재 turret v2는 **MQTT가 아니라 `Serial` 입력**으로 제어됩니다.
2. 현재 입력 명령은 사실상 아래 3개만 있습니다.
   - `x,y`
   - `d`
   - `r`
3. 현재 터렛 위치는 코드 안에 하드코딩되어 있습니다.
   - `x_turret = -300.0`
   - `y_turret = 470.0`
   - `z_turret = 134.5`
   - `z_target = 70.0`
4. 현재 코드에는 `turret_id` 개념이 없습니다.
5. 나중에 MQTT와 연결하려면 **명령 모델, 좌표 단위, 터렛 식별자**를 먼저 정리해야 합니다.
