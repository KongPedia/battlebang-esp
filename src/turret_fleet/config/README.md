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
- `fire.*`: fire timing/ESC parameters only. There is no MQTT/NVS pre-arm enable flag; explicit `fire` commands run unless blocked by `DEAD`, brownout lockout, or unconfigured state.
- `ota.*`: Command Center-approved OTA polling policy.

Validation rejects invalid envelopes: yaw span >150°, pitch span >150°, home/idle/dead outside limits, servo tuning out of allowed PWM ranges, invalid MQTT target unit, and unsafe fire timings.
