# `control/`

Closed-loop turret motion, target solving, idle/dead modes, fire sequencing, and brownout recovery.

Command units:

- `target`: world coordinates in configured MQTT unit, normally meters. The solver converts to local yaw/pitch, then clamps to `motion.limits`.
- `aim`: direct local yaw/pitch degrees.
- `jog`: bounded PWM pulse in microseconds for bench debugging only.
- `idle`: sweeps within configured yaw/pitch idle ranges.
- `dead`: holds current yaw and moves pitch to configured dead pitch.
- `home`/`init`/`initiate`: local `motion.home`, not world target `0,0`.

Direction notes from bench observations:

- Yaw `+PWM` decreased raw/degree on the tested unit; the controller compensates for that sign.
- Local yaw/pitch `+deg` are software-frame values after calibration offsets. Verify physical left/right/up/down per turret before widening limits.
- `jog plus/minus/cw/ccw` are PWM direction probes, not degree commands.

Safety behavior:

- Normal setpoints clamp to `motion.limits`.
- Invalid config/frame/state/fire/brownout conditions reject instead of clamp.
- Soft-limit guards block outward drive at calibrated edges.
- Brownout/fire-reset lockout forces fire off, stores `recover_req`, and only auto-recovers to safe `WAIT_COMMAND` if current feedback is stable in the soft window.
