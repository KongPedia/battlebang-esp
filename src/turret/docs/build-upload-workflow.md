# Turret 1~6 PlatformIO 빌드/업로드 워크플로우

이 문서는 `src/turret/main.cpp` 기준으로, **터렛 1~6을 각자의 좌표/시크릿 설정으로 빌드하고 USB 포트에 업로드하는 방법**을 정리한 문서입니다.

## 0. 제일 간단한 현장용 명령

이제는 긴 `pio run ...` 대신 아래 shell 엔트리포인트를 쓰면 됩니다.

```bash
./bin/turret ports
./bin/turret show 5
./bin/turret build 5
./bin/turret upload 5 /dev/cu.usbserial-1120
./bin/turret upload 5
./bin/turret monitor /dev/cu.usbserial-1120
```

의미:

- `ports`: 현재 연결된 USB 시리얼 포트 확인
- `show 5`: `turret_5` 좌표/config 확인
- `build 5`: `turret_5` 빌드
- `upload 5 /dev/...`: `turret_5`를 해당 ESP 포트로 빌드+업로드
- `upload 5`: USB 시리얼 장치가 1개만 연결되어 있으면 자동 감지해서 업로드
- `monitor`: 115200 시리얼 모니터

여러 대를 순차 업로드할 때:

```bash
./bin/turret build-all
./bin/turret upload-all
```

## 1. 현재 기준 파일

- 펌웨어 엔트리: `src/turret/main.cpp`
- 터렛별 설치 좌표: `src/turret/turrets.json`
- 로컬 Wi-Fi / MQTT 시크릿: `src/turret/local_secrets.h`
- USB 포트 매핑 예시: `src/turret/upload_targets.example.toml`
- 빌드/업로드 스크립트: `scripts/turret_flash.py`
- 현장용 shell 래퍼: `bin/turret`
- PlatformIO env:
  - `esp32dev_turret_1`
  - `esp32dev_turret_2`
  - `esp32dev_turret_3`
  - `esp32dev_turret_4`
  - `esp32dev_turret_5`
  - `esp32dev_turret_6`

## 2. 먼저 어디를 바꿔야 하나

### 2-1. 터렛 좌표

파일:

```text
src/turret/turrets.json
```

각 터렛은 아래 키가 필요합니다.

- `configured`
- `x_cm`
- `y_cm`
- `z_cm`
- `default_target_z_cm`

예:

```json
"turret_1": {
  "configured": true,
  "x_cm": -300.0,
  "y_cm": 470.0,
  "z_cm": 134.5,
  "default_target_z_cm": 70.0
}
```

선택 보정값:

```json
"pitch_offset_deg": -2.0,
"yaw_offset_deg": 1.5
```

> 지금은 `turret_5`만 실제 좌표가 채워져 있고, 나머지 `turret_1,2,3,4,6`은 `configured: false`입니다.
> 즉 **1~6 전체 업로드를 하려면 먼저 이 파일을 채워야 합니다.**

### 2-2. Wi-Fi / MQTT 시크릿

파일:

```text
src/turret/local_secrets.h
```

없으면 아래처럼 생성:

```bash
cp src/turret/local_secrets.example.h src/turret/local_secrets.h
```

예:

```cpp
#define TURRET_WIFI_SSID "kongstudios"
#define TURRET_WIFI_PASSWORD "cypress3428"
#define TURRET_MQTT_HOST "jetson-go2-02.local"
#define TURRET_MQTT_PORT 1883
#define TURRET_MQTT_USERNAME ""
#define TURRET_MQTT_PASSWORD ""
#define TURRET_MQTT_COORDS_IN_METERS 1
#define TURRET_AUTO_FIRE_ON_TARGET 0
```

주의:

- `local_secrets.h`는 `.gitignore` 대상입니다.
- 하지만 **펌웨어 바이너리에는 값이 컴파일되어 들어갑니다.**
- PIO 빌드에서는 `scripts/turret_config.py`가 `TURRET_FORCE_AUTO_FIRE_ON_TARGET=0`을 주입하므로, `target`은 기본적으로 조준만 하고 자동 발사하지 않습니다.

### 2-3. USB 포트 매핑

여러 대를 순서대로 올릴 때는 로컬 포트 매핑 파일을 하나 두는 것이 가장 편합니다.

```bash
cp src/turret/upload_targets.example.toml src/turret/upload_targets.toml
```

예:

```toml
[turrets]
turret_1 = "/dev/cu.usbserial-0001"
turret_2 = "/dev/cu.usbserial-0002"
turret_3 = "/dev/cu.usbserial-0003"
turret_4 = "/dev/cu.usbserial-0004"
turret_5 = "/dev/cu.usbserial-0005"
turret_6 = "/dev/cu.usbserial-0006"
```

이 파일도 머신 의존적이므로 git에 올리지 않는 걸 권장합니다. 실제 파일 `src/turret/upload_targets.toml`은 `.gitignore` 처리되어 있습니다.

## 3. 새 스크립트 사용법

### 3-1. 현재 포트 확인

```bash
python3 scripts/turret_flash.py list-ports
```

예상 출력:

```text
/dev/cu.usbserial-1120    USB-Serial Controller D    USB VID:PID=...
```

### 3-2. 현재 좌표/시크릿 설정 확인

```bash
python3 scripts/turret_flash.py show-config
```

이 명령은:

- `src/turret/turrets.json`의 configured 상태
- `src/turret/local_secrets.h` 존재 여부
- 현재 로컬 시크릿 값 유무

를 요약해서 보여줍니다.

### 3-3. 터렛 하나만 빌드

```bash
python3 scripts/turret_flash.py flash --build-only --target turret_5
```

내부적으로 실행되는 명령은 사실상 아래와 같습니다.

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5
```

### 3-4. 터렛 하나만 빌드 + 업로드

```bash
python3 scripts/turret_flash.py flash --target turret_5=/dev/cu.usbserial-1120
```

내부적으로는 아래 명령을 실행합니다.

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5 -t upload --upload-port /dev/cu.usbserial-1120
```

### 3-5. 여러 터렛을 한 번에 순차 업로드

직접 지정:

```bash
python3 scripts/turret_flash.py flash \
  --target turret_1=/dev/cu.usbserial-0001 \
  --target turret_2=/dev/cu.usbserial-0002 \
  --target turret_3=/dev/cu.usbserial-0003
```

포트 파일 사용:

```bash
python3 scripts/turret_flash.py flash --map-file src/turret/upload_targets.toml
```

`--map-file`을 생략해도 `src/turret/upload_targets.toml`이 있으면 자동으로 읽습니다.

즉 보통은:

```bash
python3 scripts/turret_flash.py flash
```

만 쳐도 됩니다.

단, 이 경우 `upload_targets.toml`에 적은 터렛이 모두 `configured: true`여야 합니다.

### 3-6. 실제 업로드 전에 검증만 하고 싶을 때

```bash
python3 scripts/turret_flash.py flash \
  --target turret_5=/dev/cu.usbserial-1120 \
  --dry-run
```

이렇게 하면 실제 빌드/업로드 없이:

- 좌표 설정이 들어있는지
- 시크릿이 준비됐는지
- 포트가 현재 감지되는지
- 어떤 `pio` 명령이 실행될지

만 확인합니다.

## 4. local_secrets.h 대신 빌드 시 주입하는 방법

로컬 파일을 수정하지 않고, 업로드할 때만 값을 주입할 수도 있습니다.
스크립트 인자를 쓰면 됩니다.

```bash
python3 scripts/turret_flash.py flash \
  --target turret_5=/dev/cu.usbserial-1120 \
  --wifi-ssid kongstudios \
  --wifi-password cypress3428 \
  --mqtt-host jetson-go2-02.local \
  --mqtt-port 1883
```

이 값들은 내부적으로 환경변수로 들어가고, `scripts/turret_config.py`가 `TURRET_BUILD_*` 매크로로 변환해서 빌드에 주입합니다.

우선순위는 아래와 같습니다.

1. `turrets.json`의 좌표
2. `local_secrets.h`의 Wi-Fi/MQTT 값
3. `turret_flash.py` 인자로 넘긴 값이 있으면 그것이 최우선

## 5. 자주 쓰는 실제 절차

### 패턴 A: 터렛 하나만 연결해서 바로 업로드

```bash
python3 scripts/turret_flash.py list-ports
python3 scripts/turret_flash.py flash --target turret_5=/dev/cu.usbserial-1120
```

### 패턴 B: 좌표는 고정, Wi-Fi/MQTT만 그때그때 바꿔서 업로드

```bash
python3 scripts/turret_flash.py flash \
  --target turret_5=/dev/cu.usbserial-1120 \
  --wifi-ssid kongstudios \
  --wifi-password cypress3428 \
  --mqtt-host jetson-go2-02.local
```

### 패턴 C: 1~6 포트를 다 적어두고 순차 업로드

```bash
cp src/turret/upload_targets.example.toml src/turret/upload_targets.toml
$EDITOR src/turret/upload_targets.toml
python3 scripts/turret_flash.py flash
```

## 6. 수동 명령으로도 하고 싶다면

스크립트 없이 직접 해도 됩니다.

### 빌드

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5
```

### 업로드

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5 -t upload --upload-port /dev/cu.usbserial-1120
```

### 시크릿까지 빌드 시 직접 주입

```bash
TURRET_WIFI_SSID='kongstudios' \
TURRET_WIFI_PASSWORD='cypress3428' \
TURRET_MQTT_HOST='jetson-go2-02.local' \
TURRET_MQTT_PORT=1883 \
./.venv-pio/bin/pio run -e esp32dev_turret_5 -t upload --upload-port /dev/cu.usbserial-1120
```

## 7. 실패 조건

아래 경우 스크립트가 실패하도록 되어 있습니다.

- `turret_1`~`turret_6` 중 해당 터렛이 `configured: false`
- `x_cm/y_cm/z_cm/default_target_z_cm` 중 하나라도 없음
- 업로드 포트가 현재 감지되지 않음
- `local_secrets.h`도 없고 CLI 시크릿도 안 넘김

즉, 업로드 전에 잘못된 상태를 먼저 잡아내도록 만들었습니다.
