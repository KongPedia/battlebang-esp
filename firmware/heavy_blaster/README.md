# BattleBang Heavy Blaster ESP firmware

`firmware/heavy_blaster/` is the standalone Heavy Blaster station firmware. The
folder name, MQTT collection, and OTA channel intentionally use hyphens
(`heavy-blaster` / `heavy-blasters`). C++ namespaces, PlatformIO env names, and
shell variables use underscore-safe identifiers (`heavy_blaster`,
`esp32dev_heavy_blaster`, `HEAVY_BLASTER_*`).

## Operating model

Command Center publishes slot progress over MQTT. The ESP renders the four
matrix LEDs and owns only local safety: the relay is forced OFF at boot/reset,
when unconfigured, when config is rejected, and while OTA is prepared. When all
required slots are active, the ESP unlocks the Heavy Blaster locally and turns
the relay ON using the configured polarity.

Bluetooth prototype controls are not the production control path. The prototype
branch `origin/HeavyBlaster_HWsystem_test` used Bluetooth/USB single-character
commands (`1..4`, `q/w/e/r`, `f`) for bench control; this production surface is
Command Center -> MQTT first. Local serial slot/unlock commands are rejected
unless `debug_allow_local_control=true` is provisioned.

## Hardware profile

Current profile from `HeavyBlasterStation_HWsystem_test-v2.cpp`:

- 4 WS2812B 8x8 matrices, 64 LEDs each, GRB, brightness 30
- Matrix GPIOs: `23,22,21,19`
- Relay GPIO: `26`
- Relay default: active-HIGH (`relay_active_low=false`)
- Unlock visual: 10s pre-effect, first 4s fade-out, then 40 BPM blink, then
  rainbow/arrow loop

`hardware_profile` is accepted in provision/config payloads as a guard. Matrix
pins, relay pin, LED type/order, and matrix dimensions must match the compiled
profile. Relay polarity is explicit in config/profile so active-HIGH and
active-LOW relay modules are not guessed silently.

## MQTT topics

Default root: `battlebang`.

- Device status: `battlebang/devices/{device_id}/status`
- Device config: `battlebang/devices/{device_id}/config`
- Device OTA: `battlebang/devices/{device_id}/ota`
- Blaster status: `battlebang/heavy-blasters/{blaster_id}/status`
- Blaster command: `battlebang/heavy-blasters/{blaster_id}/command`
- Blaster config: `battlebang/heavy-blasters/{blaster_id}/config`
- Blaster OTA: `battlebang/heavy-blasters/{blaster_id}/ota`
- All blaster OTA: `battlebang/heavy-blasters/all/ota`

Commands are non-retained JSON. Supported MVP commands:

```json
{"command":"status"}
{"command":"reset","request_id":"round-1-reset"}
{"command":"set_slot","request_id":"round-1-slot-1","blaster_id":"heavy-blaster-ABCDEF123456","slot_index":0,"active":true}
{"command":"set_slots","request_id":"round-1-all","slots":[true,true,true,true]}
{"command":"set_slots","request_id":"round-1-mask","bitmask":15}
{"command":"unlock","request_id":"round-1-direct-unlock"}
```

`request_id` is optional but recommended. Duplicate non-empty request IDs are
ignored to avoid replaying retained or retried commands.

Status includes:

- `blaster_id`, `device_id`, `device_mac`, `display_name`, `group`, `stage_id`, `location`
- `mode`: `UNCONFIGURED`, `LOCKED`, `PARTIAL`, `UNLOCK_PRE_EFFECT`, `UNLOCKED`,
  or `OTA_PREPARED`
- `slot_count`, `required_slots`, `slots_active`, `active_slot_count`
- `unlock_ready`, `relay_on`, `relay_pin`, `relay_active_low`, `relay_profile`
- firmware metadata, Wi-Fi/MQTT metadata, `uptime_ms`

## Provisioning

Copy the example and fill local values without committing secrets:

```bash
cp firmware/heavy_blaster/.env.heavy-blaster.example firmware/heavy_blaster/.env.heavy-blaster
./.venv-pio/bin/python scripts/heavy_blaster/provision.py --print-json --no-serial
./.venv-pio/bin/python scripts/heavy_blaster/provision.py --serial-port /dev/cu.usbserial-120
```

Publish a bench command without external MQTT dependencies:

```bash
./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  set-slot --host 10.2.80.52 --blaster-id heavy-blaster-ABCDEF123456 \
  --slot-index 0 --active true
```

## Bench command runbook

For the currently provisioned bench unit:

```bash
HOST=10.2.80.52
BLASTER_ID=heavy-blaster-489D31C0575C
```

The Command Center should publish to:

```text
battlebang/heavy-blasters/heavy-blaster-489D31C0575C/command
```

Quick status check:

```bash
./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  status --host "$HOST" --blaster-id "$BLASTER_ID"
```

Reset/lock the unit and force the relay OFF:

```bash
./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  reset --host "$HOST" --blaster-id "$BLASTER_ID"
```

Activate slots one at a time. Slot indexes are zero-based (`0..3`):

```bash
./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  set-slot --host "$HOST" --blaster-id "$BLASTER_ID" \
  --slot-index 0 --active true

./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  set-slot --host "$HOST" --blaster-id "$BLASTER_ID" \
  --slot-index 1 --active true

./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  set-slot --host "$HOST" --blaster-id "$BLASTER_ID" \
  --slot-index 2 --active true

./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  set-slot --host "$HOST" --blaster-id "$BLASTER_ID" \
  --slot-index 3 --active true
```

Expected progression:

| Slots active | Expected mode | Relay |
| --- | --- | --- |
| 1, 2, or 3 | `PARTIAL` | OFF |
| 4 | `UNLOCK_PRE_EFFECT` then `UNLOCKED` | ON |

After all four slots are active, status should report:

```json
{
  "mode": "UNLOCKED",
  "effect": "rainbow_arrow",
  "slots_active": [true, true, true, true],
  "unlock_ready": true,
  "relay_on": true
}
```

To activate all slots in one MQTT message:

```bash
./.venv-pio/bin/python scripts/heavy_blaster/mqtt_command.py \
  set-slots --host "$HOST" --blaster-id "$BLASTER_ID" \
  --slots true,true,true,true
```

Equivalent raw Command Center payloads:

```json
{"command":"set_slot","slot_index":0,"active":true}
{"command":"set_slot","slot_index":1,"active":true}
{"command":"set_slot","slot_index":2,"active":true}
{"command":"set_slot","slot_index":3,"active":true}
{"command":"reset"}
```

Bench validation on 2026-06-22 confirmed the slot-by-slot flow over MQTT:

- Slot 0 -> `active_slot_count=1`, `relay_on=false`
- Slot 1 -> `active_slot_count=2`, `relay_on=false`
- Slot 2 -> `active_slot_count=3`, `relay_on=false`
- Slot 3 -> `active_slot_count=4`, `unlock_ready=true`, `relay_on=true`
- After the pre-unlock effect, final state was `mode=UNLOCKED`,
  `effect=rainbow_arrow`, `relay_on=true`
- A subsequent `reset` returned the unit to `mode=LOCKED`,
  `slots_active=[false,false,false,false]`, `relay_on=false`

## Build

```bash
./.venv-pio/bin/pio run -e esp32dev_heavy_blaster
```

## Safety notes

- The relay is forced OFF at boot/reset/config clear/OTA preparation.
- Never assume relay polarity. Provision `HEAVY_BLASTER_RELAY_ACTIVE_LOW` and
  verify physical output before connecting the blaster mechanism.
- Local serial slot/unlock control is disabled by default; production control is
  Command Center MQTT.
