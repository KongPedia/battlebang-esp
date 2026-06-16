# Integrated Go2/Nixo fallback ESP 빌드 / 업로드 흐름

`src/go2_nixo`는 hit/ring/Nixo를 한 ESP에 통합한 fallback 경로입니다. 현재 active 2-ESP split은 `src/go2` + `src/nIxo`를 사용합니다.

## 1. 로컬 secrets

Go2 secrets는 터렛 secrets와 분리합니다.

```bash
cp src/go2_nixo/local_secrets.example.h src/go2_nixo/local_secrets.h
```

`src/go2_nixo/local_secrets.h`에는 Wi-Fi / MQTT broker 정보를 넣고 커밋하지 않습니다.

```cpp
#define ESP_WIFI_SSID "..."
#define ESP_WIFI_PASSWORD "..."
#define ESP_MQTT_HOST "..."
#define ESP_MQTT_PORT 1883
#define ESP_MQTT_TOPIC_PREFIX "battlebang/hit"
```

## 2. Robot profile

커밋 가능한 non-secret 설정은 `src/go2_nixo/robots.json`에 둡니다.

- `hit_cooldown_ms`
- offline hit queue capacity / flush interval
- LED pin / LED count / brightness
- piezo AO ADC pin / threshold / rearm raw / capture window
- piezo D0 pin은 hit 판정에 쓰지 않고 debug readback 용도
- MQTT topic prefix

ESP에는 스코어/down 기준을 넣지 않습니다. 해당 정책은 Command Center config가 소유합니다.

## 3. 빌드만 검증

```bash
pio run -e esp32dev_go2_nixo_go2_05
```

## 4. USB 업로드

먼저 포트를 확인합니다.

```bash
pio device list
```

업로드 예:

```bash
pio run -e esp32dev_go2_nixo_go2_05 -t upload --upload-port /dev/cu.usbserial-xxxx
```

## 5. PlatformIO 직접 사용

```bash
pio run -e esp32dev_go2_nixo_go2_05
pio run -e esp32dev_go2_nixo_go2_05 -t upload --upload-port /dev/cu.usbserial-xxxx
```

## 구조 메모

- Go2 Arduino 진입점과 runtime 오케스트레이션은 `src/go2_nixo/main.cpp`입니다.
- Go2 빌드 설정은 `src/go2_nixo/build_config.h`입니다.
- 터렛 진입점 `src/turret/main.cpp`, 터렛 설정 `src/turret/build_config.h`와 같은 배치입니다.
