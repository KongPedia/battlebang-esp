# Turret 5 PlatformIO 빌드/업로드 예시

현재 기준은 `src/turret/main.cpp`입니다. 설정은 `build_config.h`, `turrets.json`, `local_secrets.h`로 분리되어 있습니다.

## 1. 로컬 시크릿 방식

```bash
cp src/turret/local_secrets.example.h src/turret/local_secrets.h
$EDITOR src/turret/local_secrets.h
```

예:

```cpp
#define TURRET_WIFI_SSID "YOUR_WIFI_SSID"
#define TURRET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define TURRET_MQTT_HOST "COMMAND_CENTER_IP_OR_DNS"
#define TURRET_MQTT_PORT 1883
#define TURRET_MQTT_USERNAME ""
#define TURRET_MQTT_PASSWORD ""
#define TURRET_MQTT_COORDS_IN_METERS 1
#define TURRET_AUTO_FIRE_ON_TARGET 0
```

`local_secrets.h`는 `.gitignore` 대상입니다. 단, 값은 컴파일되어 firmware binary 안에 들어갑니다.

## 2. shell env로 빌드 시 직접 주입

`local_secrets.h`를 수정하지 않고, 업로드 직전 한 번만 주입할 수도 있습니다.

```bash
TURRET_WIFI_SSID='YOUR_WIFI_SSID' \
TURRET_WIFI_PASSWORD='YOUR_WIFI_PASSWORD' \
TURRET_MQTT_HOST='COMMAND_CENTER_IP_OR_DNS' \
TURRET_MQTT_PORT=1883 \
./.venv-pio/bin/pio run -e esp32dev_turret_5
```

업로드까지 한 번에:

```bash
TURRET_WIFI_SSID='YOUR_WIFI_SSID' \
TURRET_WIFI_PASSWORD='YOUR_WIFI_PASSWORD' \
TURRET_MQTT_HOST='COMMAND_CENTER_IP_OR_DNS' \
./.venv-pio/bin/pio run -e esp32dev_turret_5 -t upload --upload-port /dev/cu.usbserial-1130
```

이 방식은 내부적으로 `scripts/turret_config.py`가 `TURRET_BUILD_*` 매크로를 주입하고, `build_config.h`가 `local_secrets.h`보다 뒤에서 override합니다.

## 3. turret_5 좌표

`src/turret/turrets.json`:

```json
"turret_5": {
  "configured": true,
  "x_cm": -170.0,
  "y_cm": 190.0,
  "z_cm": 134.5,
  "default_target_z_cm": 70.0
}
```

## 4. 빌드 / 업로드 / 모니터

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_5
./.venv-pio/bin/pio run -e esp32dev_turret_5 -t upload --upload-port /dev/cu.usbserial-1130
./.venv-pio/bin/pio device monitor -b 115200 -p /dev/cu.usbserial-1130
```
