# Go2 Full Blaster fire control

The same `go2_full` image owns hit/HP display and the local Blaster relay sequence.
The deployed command route remains:

```text
battlebang/nixo/{nixo_id}/command
```

For `robot_id=go2_03`, the default derived ID is `nixo_go2_03` and the command
topic is `battlebang/nixo/nixo_go2_03/command`.

## Relay hardware

Both transport builds use the same fixed two-channel profile:

- GPIO23: flywheel
- GPIO22: chain
- active HIGH
- normal shutdown: chain off, then flywheel off
- default inter-channel delay: 150 ms

The pins and polarity are build-time safety data. Fire duration, prefire, and
relay delay are NVS tuning values.

## MQTT command

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

`enabled=false` stops an active sequence. `request_id` is required and
deduplicated; duration is clamped to the configured minimum/maximum.

Jetson uses framed `FireHold` and `FireStop` packets. Hold timeout and the
maximum-duration latch preserve fail-safe shutdown behavior. The ring on GPIO4
shows Blaster ready/firing/inhibited state; HP remains on the separate GPIO18 bar.
