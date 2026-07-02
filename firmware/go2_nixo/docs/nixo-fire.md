# Integrated Nixo fire

`firmware/go2_nixo` is the optional one-ESP integrated fallback path. The same image owns:

- ESP-local hit events/status: `battlebang/hit/{robot_id}/events` and `{mqtt_root}/devices/go2_nixo/{device_id}/status`
- HP bar reset/debug commands: `battlebang/hit/{robot_id}/ring_display/command`
- Deprecated/no-op MQTT Nixo fire commands: `battlebang/nixo/{nixo_id}/command`; live fire is Jetson UART only

The active 2-ESP split uses `firmware/go2` for hit/LED and `src/nIxo` for Nixo relay fire. Use `esp32dev_nixo_*` for the standalone Nixo ESP, and use `esp32dev_go2_nixo_*` only when intentionally flashing this integrated fallback.

## Relay variants

Integrated fallback builds support both relay variants because some Nixo hardware is still one-channel and one unit is BTB-766 two-channel.

- `relay_1ch` / `esp32dev_go2_nixo_1ch` env:
  - relay 1: `GPIO23`
  - relay 2: disabled (`-1`)
  - relay polarity: active-LOW (`LOW` fires, `HIGH` stops)
- `relay_2ch` / `esp32dev_go2_nixo` or `esp32dev_go2_nixo_2ch` envs:
  - relay 1 / flywheel: `GPIO22`
  - relay 2 / chain: `GPIO23`
  - flywheel-to-chain delay default: `150ms` (NVS `nixo.relay_delay1_ms` can tune it after provisioning)
  - relay polarity: active-LOW (`LOW` fires, `HIGH` stops)
  - normal shutdown order: chain off first, flywheel off last

Variant config lives in `firmware/go2_nixo/variants/<variant>/config.json` and is selected by PlatformIO `custom_nixo_variant` or the `GO2_NIXO_RELAY_VARIANT` / `NIXO_RELAY_VARIANT` shell override. Pins, polarity, and channel count stay build variant data; fire duration/prefire/relay-delay timing is NVS runtime tuning, and ESP cooldown is disabled.

## Live mapping

For a device provisioned with `robot_id=go2_03`, the default derived Nixo id is:

```text
nixo_go2_03
```

Command topic:

```text
battlebang/nixo/nixo_go2_03/command
```

## Build examples

```bash
# BTB-801 two-channel integrated fallback
pio run -e esp32dev_go2_nixo_2ch

# BTB-766 two-channel integrated fallback
pio run -e esp32dev_go2_nixo_2ch

# Runtime identity is set after flashing:
GO2_NIXO_ROBOT_ID=go2_06 GO2_NIXO_NIXO_ID=nixo_go2_06 \
  ./.venv-pio/bin/python scripts/go2_nixo/provision.py --serial-port /dev/cu.usbserial-XXXX
```

## Fire command

Jetson UART2 is the only live fire control path for the local Nixo: send `f`, `fire`, or `1`
repeatedly while the trigger combo is held; send `x`, `0`, `stop-fire`, or
`fire off` on release. If Jetson keepalive packets stop, ESP fails safe after
`300 ms`. USB/BT fire commands are ignored. MQTT fire payloads are deprecated no-ops and log `reason=jetson_uart_required`.

## Expected serial evidence

1ch boot/fire:

```text
[NIXO] mqtt=enabled nixo_id=nixo_go2_03 command_topic=battlebang/nixo/nixo_go2_03/command relay1=23 relay2=-1 relay_on=1 relay_off=0 delay1_ms=800 fire_default_ms=3000 fire_min_ms=100 fire_max_ms=10000 cooldown_ms=0 prefire_ms=600
[NIXO MQTT] subscribed battlebang/nixo/nixo_go2_03/command qos=1
[RELAY] CH1 ON pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=23 level=0 readback=0
[RELAY] ALL OFF / FIRE done
```

2ch boot/fire:

```text
[NIXO] mqtt=enabled nixo_id=nixo_go2_06 command_topic=battlebang/nixo/nixo_go2_06/command relay1=22 relay2=23 relay_on=0 relay_off=1 delay1_ms=150 fire_default_ms=3000 fire_min_ms=100 fire_max_ms=10000 cooldown_ms=0 prefire_ms=600
[NIXO MQTT] subscribed battlebang/nixo/nixo_go2_06/command qos=1
[RELAY] CH1 ON pin=22 level=0 readback=0
[RELAY] CH2 ON pin=23 level=0 readback=0
[RELAY] CH2 OFF pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=22 level=1 readback=1
[RELAY] ALL OFF / FIRE done
```

## Direct smoke command

Use Jetson UART, not MQTT:

```bash
printf 'f\n' | sudo tee /dev/ttyTHS1 >/dev/null
printf 'x\n' | sudo tee /dev/ttyTHS1 >/dev/null
```

## Fire ring

The original ring LED remains on GPIO4 and displays only Nixo fire state: ready=full green, firing=full red, inhibited=red. ESP-side Nixo cooldown is disabled; hit/down HP states are rendered separately on the 84-LED bar on GPIO18.
