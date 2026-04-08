# Turret 1 빌드/업로드 예시

이 문서는 **`turret_1` 좌표 + 로컬 Wi-Fi/MQTT 값 주입**으로 빌드하고 업로드하는 가장 실용적인 예시입니다.

---

## 핵심 원칙

설정은 2층으로 나뉩니다.

### 1) 공개 프리셋

파일:

```text
src/turret/presets/turret_1.h
```

여기에는 보통 아래처럼 **터렛 식별자 / 좌표**만 둡니다.

- `TURRET_ID`
- `TURRET_X_CM`
- `TURRET_Y_CM`
- `TURRET_Z_CM`
- `TURRET_DEFAULT_TARGET_Z_CM`

현재 `turret_1` 값:

```cpp
#define TURRET_ID "turret_1"
#define TURRET_X_CM -300.0f
#define TURRET_Y_CM 470.0f
#define TURRET_Z_CM 134.5f
#define TURRET_DEFAULT_TARGET_Z_CM 70.0f
```

### 2) 비공개 로컬 시크릿

파일:

```text
src/turret/local_secrets.h
```

이 파일은 `.gitignore`에 들어 있으므로 커밋되지 않습니다.

여기에는:

- Wi-Fi SSID
- Wi-Fi 비밀번호
- MQTT host / port
- MQTT username / password

같이 민감하거나 현장별로 바뀌는 값을 둡니다.

---

## 왜 민감정보를 local 파일로 두는가

현재 펌웨어 구조는 **컴파일 타임 매크로 기반**입니다.

즉:

- `TURRET_WIFI_PASSWORD`
- `TURRET_MQTT_PASSWORD`

같은 값도 **빌드 시 펌웨어 안에 포함**됩니다.

따라서 답은:

### 네, 현재 구조에서는 민감정보도 “빌드할 때 주입”됩니다.

하지만 중요한 점:

1. **git에 커밋하면 안 됨**
2. **빌드된 바이너리 안에도 값이 들어감**
3. 더 강한 보안이 필요하면 나중에 NVS/provisioning 구조로 바꿔야 함

현재 단계에서는:

- 좌표/ID → preset
- 비밀번호/브로커 주소 → `local_secrets.h`

가 가장 실용적입니다.

---

## turret_1 예시

## 1. local secrets 예시 파일 복사

```bash
cp src/turret/local_secrets.example.h src/turret/local_secrets.h
```

## 2. `src/turret/local_secrets.h` 수정

예:

```cpp
#pragma once

#define TURRET_WIFI_SSID "BattleBang_AP"
#define TURRET_WIFI_PASSWORD "change-me-please"

#define TURRET_MQTT_HOST "192.168.0.10"
#define TURRET_MQTT_PORT 1883
#define TURRET_MQTT_USERNAME ""
#define TURRET_MQTT_PASSWORD ""

#define TURRET_MQTT_COORDS_IN_METERS 1
#define TURRET_AUTO_FIRE_ON_TARGET 1
```

이 상태에서 실제 최종 빌드값은 아래처럼 합쳐집니다.

### `turret_1` 프리셋에서 오는 값

- `TURRET_ID = "turret_1"`
- `TURRET_X_CM = -300.0f`
- `TURRET_Y_CM = 470.0f`
- `TURRET_Z_CM = 134.5f`
- `TURRET_DEFAULT_TARGET_Z_CM = 70.0f`

### `local_secrets.h`에서 오는 값

- `TURRET_WIFI_SSID`
- `TURRET_WIFI_PASSWORD`
- `TURRET_MQTT_HOST`
- `TURRET_MQTT_PORT`
- `TURRET_MQTT_USERNAME`
- `TURRET_MQTT_PASSWORD`
- `TURRET_MQTT_COORDS_IN_METERS`
- `TURRET_AUTO_FIRE_ON_TARGET`

---

## 3. 빌드

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_1
```

---

## 4. 업로드

포트 확인 후:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_1 -t upload --upload-port /dev/cu.usbserial-0001
```

macOS에서 포트 예시:

- `/dev/cu.usbserial-0001`
- `/dev/cu.SLAB_USBtoUART`

Linux 예시:

- `/dev/ttyUSB0`
- `/dev/ttyACM0`

Windows 예시:

- `COM3`
- `COM5`

---

## 5. 시리얼 모니터

업로드 후 로그 확인:

```bash
./.venv-pio/bin/pio device monitor -b 115200 -p /dev/cu.usbserial-0001
```

정상이라면 시작 시 아래 정보가 나옵니다.

- Turret ID
- Turret XYZ
- Default target Z
- MQTT host/port
- MQTT topic
- MQTT coord units
- Auto fire on target

---

## 6. 한 줄 요약

### turret_1 빌드/업로드 최소 흐름

```bash
cp src/turret/local_secrets.example.h src/turret/local_secrets.h
$EDITOR src/turret/local_secrets.h
./.venv-pio/bin/pio run -e esp32dev_turret_1
./.venv-pio/bin/pio run -e esp32dev_turret_1 -t upload --upload-port /dev/cu.usbserial-0001
```

---

## 7. 테스트 관점 정리

현재 이 저장소에서 검증된 것:

- `esp32dev_turret_1` 빌드 성공
- `esp32dev_turret_2` 빌드 성공
- `esp32dev_turret_3` 빌드 성공
- `esp32dev_turret_4` 빌드 성공
- `esp32dev_turret_5` 빌드 성공
- `esp32dev_turret_6` 빌드 성공

즉 preset 구조는 정상입니다.

로컬 secrets 주입은 **실제 값이 없어도 compile-time override 방식으로 동작**하도록 설계되어 있습니다.
