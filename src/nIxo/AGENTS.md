# Nixo Firmware Guide

## Scope
Applies to `src/nIxo/**` in `battlebang-esp`.

## Ownership
This folder owns the Go2-mounted Nixo/game-blaster firmware path for BTB-633.

- Production entrypoint: `src/nIxo/main.cpp`
- PlatformIO environment: `esp32dev_nixo`
- Legacy smoke baseline: `src/nIxo/BluetoothSerial.cpp` (Bluetooth SPP only, not USB Serial)

## Hardware invariants
- `relay_1ch` is the legacy/default profile: `GPIO23` active-HIGH (`HIGH` fires, `LOW` stops), `NIXO_RELAY2_PIN=-1`.
- `relay_2ch` is the BTB-766 profile: `GPIO22` flywheel first, then `GPIO23` chain after `150ms`; the relay module is active-LOW (`LOW` fires, `HIGH` stops).
- Live device mapping is `go2_03 -> nixo_go2_03` unless the hardware is deliberately reassigned.
- MQTT topic is `battlebang/nixo/{nixo_id}/command`; current live topic is
  `battlebang/nixo/nixo_go2_03/command`.

Do not copy root HP/turret pin assumptions into this firmware; use the selected `src/nIxo/variants/<variant>/config.json`.

## Secrets and config
- Keep local Wi-Fi/MQTT values in `src/nIxo/local_secrets.h`; it is gitignored.
- Prefer copying broker/Wi-Fi values from `src/turret/local_secrets.h` so Nixo and turret use the same stage broker.
- Commit only `local_secrets.example.h` and documented config defaults.

## Verification
Before claiming a Nixo firmware change works, run at least:

```bash
pio run -e esp32dev_nixo
```

For hardware changes, upload the `esp32dev_nixo` environment to the Nixo ESP32 and verify a direct MQTT fire command.
Expected legacy 1ch serial evidence includes:

```text
[PIN] ... RELAY1=23 RELAY2=-1 relay_on=1 relay_off=0
[MQTT] subscribed topic=battlebang/nixo/nixo_go2_03/command qos=1
[RELAY] CH1 ON pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=23 level=0 readback=0
```

Expected BTB-766 2ch serial evidence includes:

```text
[PIN] ... variant=relay_2ch channels=2 RELAY1=22 role=flywheel_gpio22 RELAY2=23 role=chain_gpio23 relay_on=0 relay_off=1
[RELAY] CH1 ON pin=22 role=flywheel_gpio22 level=0 readback=0
[RELAY] CH2 ON pin=23 role=chain_gpio23 level=0 readback=0
```

If USB serial disappears or the board is unreachable, say that physical relay acknowledgement was not observed instead
of implying end-to-end hardware verification.
