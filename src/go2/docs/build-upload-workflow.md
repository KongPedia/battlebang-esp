# Go2 ESP 빌드 / 업로드 흐름

Go2 피격 ESP는 터렛과 동일하게 **장치별 non-secret profile + 로컬 secret + flash script** 구조로 빌드합니다.

## 1. 로컬 secrets

Go2 secrets는 터렛 secrets와 분리합니다.

```bash
cp src/go2/local_secrets.example.h src/go2/local_secrets.h
```

`src/go2/local_secrets.h`에는 Wi-Fi / MQTT broker 정보를 넣고 커밋하지 않습니다.

```cpp
#define ESP_WIFI_SSID "..."
#define ESP_WIFI_PASSWORD "..."
#define ESP_MQTT_HOST "..."
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"
```

## 2. Robot profile

커밋 가능한 non-secret 설정은 `src/go2/robots.json`에 둡니다.

- `hp_max`
- `piezo_damage_divisor`
- `hit_threshold`
- LED / piezo pin
- MQTT topic prefix

## 3. 빌드만 검증

```bash
python3 scripts/go2_flash.py flash --target go2_05 --build-only
```

## 4. USB 업로드

먼저 포트를 확인합니다.

```bash
python3 scripts/go2_flash.py list-ports
```

업로드 예:

```bash
python3 scripts/go2_flash.py flash --target go2_05=/dev/cu.usbserial-xxxx
```

## 5. PlatformIO 직접 사용

```bash
pio run -e esp32dev_go2_05
pio run -e esp32dev_go2_05 -t upload --upload-port /dev/cu.usbserial-xxxx
```

## 구조 메모

- Go2 Arduino 진입점과 runtime 오케스트레이션은 `src/go2/main.cpp`입니다.
- Go2 빌드 설정은 `src/go2/build_config.h`입니다.
- 터렛 진입점 `src/turret/main.cpp`, 터렛 설정 `src/turret/build_config.h`와 같은 배치입니다.
