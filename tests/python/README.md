# Turret Python Tests

이 폴더는 turret MQTT / 상태 전이 검증용 Python 테스트를 담습니다.

## 구성

- `test_turret_state_contract.py`
  - 하드웨어 없이 현재 C++ 상태 전이 규칙을 검증하는 단위 테스트
- `test_turret_mqtt_integration.py`
  - 실제 MQTT broker + ESP32 serial log를 이용하는 통합 테스트

## 의존성 설치

```bash
python3 -m venv .venv-turret-tests
source .venv-turret-tests/bin/activate
pip install -r tests/python/requirements.txt
```

## 단위 테스트

```bash
pytest tests/python/test_turret_state_contract.py
```

## 통합 테스트

기본 통합 테스트는 실제 보드가 연결되어 있어야 합니다.

```bash
export TURRET_RUN_HW_TESTS=1
export TURRET_TEST_SERIAL_PORT=/dev/cu.usbserial-1130
export TURRET_TEST_TURRET_ID=turret_5
pytest tests/python/test_turret_mqtt_integration.py -m hardware
```

### 안전 관련 주의

현재 기본 펌웨어는 **event-driven** 입니다.

- 부팅 직후 기본 모드: `HOLD`
- `idle` 명령을 받아야 searching 시작
- `target` 은 조준만 수행
- `fire` 만 실제 발사 시퀀스를 시작

`target` 명령은 보드를 실제로 움직일 수 있습니다.

따라서 `dead -> target` 테스트는 기본적으로 skip 되며, 아래 환경 변수를 준 경우에만 실행됩니다.

```bash
export TURRET_TEST_ALLOW_TARGET_MOTION=1
```

이 옵션은 반드시 아래 조건에서만 사용합니다.

- 안전한 테스트 벤치
- 발사 기구 비활성화 또는 분리

실제 발사 시퀀스까지 MQTT로 검증하려면 아래 환경 변수를 추가해야 합니다.

```bash
export TURRET_TEST_ALLOW_LIVE_FIRE=1
```

이 옵션은 **실제 발사 하드웨어가 충분히 안전한 경우에만** 사용합니다.
