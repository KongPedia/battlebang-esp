# `mqtt/`

MQTT topic construction, command/config/OTA subscription handling, and status publishing.

Default root: `battlebang`.

Per turret:

```text
battlebang/turrets/{turret_id}/command
battlebang/turrets/{turret_id}/config
battlebang/turrets/{turret_id}/ota
battlebang/turrets/{turret_id}/status
```

Device-wide:

```text
battlebang/devices/{device_id}/ota
battlebang/devices/{device_id}/status
battlebang/turrets/all/ota
```

Status includes firmware build, config version, yaw/pitch raw/current/target/goal, clamp results, fire output state, brownout lockout, OTA polling policy, Wi-Fi IP/RSSI, and last error.

Command Center usage notes:

- `command` handles `target`, `aim`, `home`/`initiate`, `idle`, `dead`, `hold`,
  `recover`, `jog`, and `fire`.
- `config` patches are persisted in NVS and are the normal way to change Wi-Fi,
  MQTT broker, calibration, motion limits/speeds, fire duration, and OTA policy.
- `ota` accepts a full firmware manifest for an immediate approved update job.
- `status.reason` reports transitions such as `command_applied`,
  `config_applied`, `ota_downloading`, `ota_rebooting`, and `connected`.

See `../docs/usage.md` for exact CLI examples.
