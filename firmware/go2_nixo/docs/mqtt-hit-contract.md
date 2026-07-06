# Integrated Go2/Nixo ESP ↔ Command Center MQTT contract

Integrated Go2/Nixo ESP owns normal hit acceptance, HP/down state, and HP bar rendering locally. Command Center ingests `hit_event` and device status payloads for dashboard/world state. It must not publish per-hit HP bar updates.

Nixo fire is a separate local lane: the original ring LED displays Nixo ready/firing/inhibited only. HP is rendered on the 84-LED bar.

## Topics

```text
ESP -> Command Center
battlebang/hit/{robot_id}/events
{mqtt_root}/devices/go2_nixo/{device_id}/status

Command Center -> ESP reset/debug only
battlebang/hit/{robot_id}/ring_display/command

Command Center -> Nixo relay
battlebang/nixo/{nixo_id}/command
```

## ESP -> Command Center: hit_event

When the max of left/right/front piezo AO ADC crosses threshold, ESP immediately accepts one local hit, decrements `hp_remaining`, updates the HP bar, and publishes a v2 `hit_event`.

```json
{
  "schema_version": 2,
  "event": "hit_event",
  "robot_id": "go2_05",
  "sensor_id": "piezo:left",
  "sequence": 7,
  "hit": true,
  "accepted": true,
  "accepted_hit_count": 3,
  "hp_remaining": 11,
  "max_hits": 14,
  "down": false,
  "ring_fill_ratio": 0.785714,
  "peak": 2188,
  "threshold": 2400,
  "firmware_ts_ms": 45678,
  "firmware": "go2_nixo",
  "firmware_role": "integrated_hit_led_nixo",
  "metadata": {
    "hit_source": "piezo_ao_adc_threshold",
    "decision_owner": "esp_local",
    "display_owner": "esp_local",
    "hp_current": 11,
    "hp_max": 14,
    "adc_peak_raw": 2188,
    "adc_threshold_raw": 2400
  }
}
```

Queued retransmits keep the original `firmware_ts_ms` and add queue metadata. They are still already-accepted ESP-local hits; Command Center should not re-score them.

On ESP boot/power-cycle or reset, the ESP also publishes a full-HP reset envelope on the same event topic so Operator UI can clear stale combat state even if it does not ingest device status:

```json
{
  "schema_version": 2,
  "event": "hit_event",
  "event_type": "hp_reset",
  "reason": "boot",
  "robot_id": "go2_05",
  "hit": false,
  "accepted": false,
  "reset_hit_state": true,
  "hp_reset": true,
  "accepted_hit_count": 0,
  "hp_remaining": 14,
  "max_hits": 14,
  "down": false,
  "ring_fill_ratio": 1.0
}
```

After `down=true`, additional piezo triggers are not published as new accepted hits until reset. The ESP keeps local HP/down state and, when MQTT is connected, publishes device status with reason `local_hit_ignored_down`.

## ESP -> Command Center: device status

`show-status` and MQTT device status include the same local combat facet:

```json
{
  "type": "status",
  "reason": "local_hit",
  "device_id": "go2_05",
  "robot_id": "go2_05",
  "nixo_id": "nixo_go2_05",
  "accepted_hit_count": 3,
  "hp_remaining": 11,
  "max_hits": 14,
  "down": false,
  "ring_fill_ratio": 0.785714,
  "combat": {
    "accepted_hit_count": 3,
    "hp_current": 11,
    "hp_max": 14,
    "down": false,
    "ring_fill_ratio": 0.785714,
    "last_hit_sequence": 7
  },
  "hp": {
    "current": 11,
    "max": 14,
    "down": false,
    "accepted_hit_count": 3
  },
  "nixo": {
    "id": "nixo_go2_05",
    "state": "ready",
    "firing": false,
    "fire_inhibited": false
  }
}
```

Nixo `state` is one of `ready`, `inhibited`, `prefire_delay`, `flywheel_spinup`,
or `firing`. The ESP publishes device status immediately with
`reason=state_changed` when HP/down or Nixo state changes, and also on the normal
heartbeat.

## ESP -> Jetson: UART HP status

The ESP sends newline-delimited `hp_status` JSON on Jetson UART2 on boot, HP
state changes, and a 1s heartbeat. The ESP does not wait for ACKs or track
whether Jetson consumed the line. Jetson owns any downstream behavior, including
local Go2 `damp` when `hp_remaining` reaches `0`.

```json
{
  "schema_version": 1,
  "type": "hp_status",
  "event": "hp_status",
  "transport": "uart",
  "reason": "state_changed",
  "robot_id": "go2_05",
  "hp_remaining": 0,
  "max_hits": 14,
  "down": true,
  "accepted_hit_count": 14,
  "last_hit_sequence": 14,
  "uptime_ms": 123456
}
```

## Command Center -> ESP: ring_display reset/debug compatibility

The MQTT topic and field names stay legacy `ring_display`/`ring_*`, but BTB-779 ESP firmware ignores normal per-hit commands. Command Center may send this only with `reset_hit_state=true` or `debug_override=true` / `maintenance_override=true`.

```json
{
  "schema_version": 1,
  "command": "ring_display",
  "robot_id": "go2_05",
  "ring_fill_ratio": 1.0,
  "down": false,
  "ring_display_mode": "active",
  "ttl_ms": 60000,
  "reset_hit_state": true
}
```

- `reset_hit_state=true`: clears ESP local hit latch/offline queue, restores full HP on the HP bar, and clears hit down/fire inhibit.
- `debug_override=true`: temporary maintenance override for the HP bar only.
- Without reset/debug override, ESP logs and ignores the payload.

Power cycle behavior is intentionally reset-based too: the ESP does not persist `accepted_hit_count`, `hp_remaining`, or `down` to NVS. On boot it reads only the configured `max_hits` rule and starts from full HP.

## Nixo ring LED separation

The ring LED on GPIO4 is not an HP display. It displays only Nixo local fire state: ready green, firing red, inhibited red. Hit/down HP state is rendered separately on the 84-LED HP bar on GPIO18.
