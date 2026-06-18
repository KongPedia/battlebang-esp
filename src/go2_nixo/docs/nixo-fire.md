# Integrated Nixo fire

`src/go2_nixo` is the optional one-ESP integrated fallback path. The same image owns:

- hit sensor events: `battlebang/hit/{robot_id}/events`
- ring display commands: `battlebang/hit/{robot_id}/ring_display/command`
- Nixo/game blaster fire commands: `battlebang/nixo/{nixo_id}/command`

The active 2-ESP split uses `src/go2` for hit/LED and `src/nIxo` for Nixo relay fire. Use `esp32dev_nixo_*` for the standalone Nixo ESP, and use `esp32dev_go2_nixo_*` only when intentionally flashing this integrated fallback.

## Relay variants

Integrated fallback builds support both relay variants because some Nixo hardware is still one-channel and one unit is BTB-766 two-channel.

- `relay_1ch` / existing `esp32dev_go2_nixo_go2_*` envs:
  - relay 1: `GPIO23`
  - relay 2: disabled (`-1`)
  - relay polarity: active-HIGH (`HIGH` fires, `LOW` stops)
- `relay_2ch` / `esp32dev_go2_nixo_2ch_go2_*` envs:
  - relay 1 / flywheel: `GPIO22`
  - relay 2 / chain: `GPIO23`
  - flywheel-to-chain delay: `150ms`
  - relay polarity: active-LOW (`LOW` fires, `HIGH` stops)
  - normal shutdown order: chain off first, flywheel off last

Variant config lives in `src/go2_nixo/variants/<variant>/config.json` and is selected by PlatformIO `custom_nixo_variant` or the `GO2_NIXO_RELAY_VARIANT` / `NIXO_RELAY_VARIANT` shell override.

## Live mapping

For `go2_03`, the default derived Nixo id is:

```text
nixo_go2_03
```

Command topic:

```text
battlebang/nixo/nixo_go2_03/command
```

## Build examples

```bash
# Legacy/default one-channel integrated fallback
pio run -e esp32dev_go2_nixo_1ch_go2_06

# BTB-766 two-channel integrated fallback
pio run -e esp32dev_go2_nixo_2ch_go2_06
```

## MQTT command

Command Center publishes:

```json
{
  "schema_version": 1,
  "command": "fire",
  "nixo_id": "nixo_go2_03",
  "parent_robot_id": "go2_03",
  "enabled": true,
  "duration_ms": 1000,
  "request_id": "manual-fire-001"
}
```

`enabled=false` stops an active fire sequence.

The firmware requires `request_id`, deduplicates repeated `request_id` values, and clamps `duration_ms` to the configured min/max duration.

## Expected serial evidence

1ch boot/fire:

```text
[NIXO] mqtt=enabled nixo_id=nixo_go2_03 command_topic=battlebang/nixo/nixo_go2_03/command relay1=23 relay2=-1 relay_on=1 relay_off=0 delay1_ms=800
[NIXO MQTT] subscribed battlebang/nixo/nixo_go2_03/command qos=1
[RELAY] CH1 ON pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=23 level=0 readback=0
[RELAY] ALL OFF / FIRE done
```

2ch boot/fire:

```text
[NIXO] mqtt=enabled nixo_id=nixo_go2_06 command_topic=battlebang/nixo/nixo_go2_06/command relay1=22 relay2=23 relay_on=0 relay_off=1 delay1_ms=150
[NIXO MQTT] subscribed battlebang/nixo/nixo_go2_06/command qos=1
[RELAY] CH1 ON pin=22 level=0 readback=0
[RELAY] CH2 ON pin=23 level=0 readback=0
[RELAY] CH2 OFF pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=22 level=1 readback=1
[RELAY] ALL OFF / FIRE done
```

## Direct smoke command

```bash
mosquitto_pub -h <BROKER_IP> -p 1883 -q 1 \
  -t battlebang/nixo/nixo_go2_03/command \
  -m '{"schema_version":1,"command":"fire","nixo_id":"nixo_go2_03","parent_robot_id":"go2_03","enabled":true,"duration_ms":1000,"request_id":"direct-mqtt-smoke"}'
```

## Cooldown ring

The original ring LED remains on GPIO4 and displays only Nixo fire/cooldown: ready=full green, firing=full red, and the 10-second cooldown starts dark then fills 1/10 of the ring in green every second. Hit/down HP states are rendered separately on the 84-LED bar on GPIO18.
