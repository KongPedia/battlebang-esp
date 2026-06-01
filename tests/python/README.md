# Python Tests

This repository uses a separate Python virtualenv for fast contract tests so it does not pollute the PlatformIO environment.

## Environment

```bash
python3 -m venv .venv-turret-tests
./.venv-turret-tests/bin/python -m pip install -r tests/python/requirements.txt
```

PlatformIO lives in a separate venv:

```bash
python3 -m venv .venv-pio
./.venv-pio/bin/python -m pip install -U platformio pyserial
```

## Contract tests

Hardware-free checks for `src/turret_fleet` contracts, helper payloads, config validation, OTA policy, and safety strings:

```bash
python3 -m py_compile scripts/turret_fleet/*.py tests/python/test_turret_fleet_contract.py
./.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py -q
```

## Hardware notes

MQTT/serial hardware tests are intentionally manual. `target`, `aim`, `idle`, `dead`, and `jog` can move real motors; `fire` is now an explicit live-fire command and can energize relays/ESC without a separate `fire.hardware_enabled=true` pre-arm.

Before live hardware checks:

1. Confirm the ESP is provisioned and Wi-Fi/MQTT are connected.
2. Do not run `fire` unless a live fire test is explicitly intended; the explicit command itself is the arm/trigger action.
3. Use `hold` or serial `show-status` to verify `motion_state.brownout_lockout=false` and `last_error=""`.
4. Keep target tests inside `motion.limits`.
