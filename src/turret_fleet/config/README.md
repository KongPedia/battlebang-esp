# `config/`

Runtime config schema, validation, JSON serialization, and ESP32 NVS persistence.

Important fields:

- `turret_id`, group/floor/side: unit identity after first USB provisioning.
- `coordinate_frame.mqtt_target_unit`: MQTT target unit, normally meters.
- `pose.x_cm/y_cm/z_cm`: turret position in the shared world frame.
- `calibration.yaw_axis_offset_deg` / `pitch_axis_offset_deg`: local sensor zero correction.
- `calibration.yaw_offset_deg` / `pitch_offset_deg`: world target solver correction after local zero is good.
- `motion.limits`: local yaw/pitch command envelope; default total range is 150°.
- `motion.home`: local boot/init pose, normally `0,0`.
- `fire.hardware_enabled`: legacy/status arm marker persisted in NVS. Direct `fire` commands do not require this flag; `DEAD`, brownout lockout, and unconfigured state are the firmware fire blockers.
- `ota.*`: Command Center-approved OTA polling policy.

Validation rejects invalid envelopes: yaw span >150°, pitch span >150°, home/idle/dead outside limits, servo tuning out of allowed PWM ranges, invalid MQTT target unit, and unsafe fire timings.
