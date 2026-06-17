# Integrated Nixo fire

`src/go2_nixo` is the optional one-ESP integrated fallback path. The same image owns:

- hit sensor events: `battlebang/hit/{robot_id}/events`
- ring display commands: `battlebang/hit/{robot_id}/ring_display/command`
- Nixo/game blaster fire commands: `battlebang/nixo/{nixo_id}/command`

The active 2-ESP split uses `src/go2` for hit/LED and `src/nIxo` for Nixo relay fire. Use `esp32dev_nixo_go2_*` for the standalone Nixo ESP, and use `esp32dev_go2_nixo_go2_*` only when intentionally flashing this integrated fallback.

## Live mapping

For `go2_03`, the default derived Nixo id is:

```text
nixo_go2_03
```

Command topic:

```text
battlebang/nixo/nixo_go2_03/command
```

## Hardware defaults

The current Go2-mounted Nixo relay invariants are:

- relay 1: `GPIO23`
- relay 2: disabled (`-1`)
- relay polarity: active-HIGH (`HIGH` fires, `LOW` stops)

These defaults live in `src/go2_nixo/build_config.h` and can be overridden from
`src/go2_nixo/robots.json`, `src/go2_nixo/local_secrets.h`, or build environment macros.

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

The firmware requires `request_id`, deduplicates repeated `request_id` values,
and clamps `duration_ms` to the configured min/max duration.

## Expected serial evidence

On boot:

```text
[NIXO] mqtt=enabled nixo_id=nixo_go2_03 command_topic=battlebang/nixo/nixo_go2_03/command relay1=23 relay2=-1
[NIXO MQTT] subscribed battlebang/nixo/nixo_go2_03/command qos=1
```

On fire:

```text
[NIXO MQTT] fire on request_id=... duration_ms=1000
[FIRE] start source=mqtt duration_ms=1000
[RELAY] CH1 ON pin=23 level=1 readback=1
[RELAY] CH1 OFF pin=23 level=0 readback=0
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
