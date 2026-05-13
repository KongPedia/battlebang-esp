# Turret motion calibration profile

`./bin/turret upload <number>` builds one firmware image with the selected turret's
coordinates and motion profile from `src/turret/turrets.json`.

The motion profile exists because each physical turret can have different motor
mounting direction, potentiometer range, and mechanical pitch/yaw stops.

## Profile shape

Fleet-wide safe defaults live at top-level `motion_defaults`:

```json
"motion_defaults": {
  "yaw": {
    "adc_low_cut": 300,
    "adc_high_cut": 3700,
    "cmd_min_deg": -140.0,
    "cmd_max_deg": 140.0,
    "idle_min_deg": -70.0,
    "idle_max_deg": 70.0,
    "idle_speed_deg_per_sec": 25.0,
    "invert_motor": false,
    "min_drive_us": 85.0
  },
  "pitch": {
    "adc_low_cut": 1700,
    "adc_high_cut": 2400,
    "cmd_min_deg": -25.0,
    "cmd_max_deg": 35.0,
    "idle_min_deg": -8.0,
    "idle_max_deg": 0.0,
    "idle_speed_deg_per_sec": 8.0,
    "dead_deg": 20.0,
    "invert_motor": false,
    "min_drive_us": 75.0
  }
}
```

A specific turret can override only the values that differ:

```json
"turret_5": {
  "configured": true,
  "x_cm": -170.0,
  "y_cm": 190.0,
  "z_cm": 134.5,
  "default_target_z_cm": 70.0,
  "motion": {
    "pitch": {
      "dead_deg": 18.0,
      "invert_motor": true
    }
  }
}
```

## Meaning of key fields

- `cmd_min_deg` / `cmd_max_deg`: absolute software command envelope. Every
  `idle`, `target`, and `dead` target is clamped inside this range.
- `idle_min_deg` / `idle_max_deg`: idle searching sweep range. This should be
  narrower than the command envelope.
- `dead_deg`: pitch target for `dead` mode. Do **not** set this to the physical
  upper stop. Keep margin so the PID does not push against the hard limit.
- `invert_motor`: flips servo pulse direction for that axis when a motor is
  wired or mounted in the opposite direction.
- `adc_low_cut` / `adc_high_cut`: sensor clamp values used before converting raw
  potentiometer ADC to degrees.
- `min_drive_us`: minimum non-stop PWM delta used by the PID once outside the
  deadband.

## Bench calibration procedure

A target object is not required for pitch-limit calibration. Use the physical
unit and Serial logs.

1. Upload the current safe profile:

   ```bash
   ./bin/turret upload 5
   ```

2. Open logs:

   ```bash
   ./bin/turret monitor
   ```

3. Watch these fields:

   ```text
   Mode=... | PITCH raw=... cur=... tgt=...
   ```

4. Test only motion first; keep live fire disabled/safe:

   ```text
   idle
   dead
   idle
   ```

5. If pitch moves away from the target instead of toward it, flip
   `motion.pitch.invert_motor` for that turret.

6. If `dead` still pushes into the mechanical upper stop, lower
   `motion.pitch.dead_deg` and/or `motion.pitch.cmd_max_deg`.

7. If idle sweep gets too close to either stop, narrow
   `motion.pitch.idle_min_deg` / `motion.pitch.idle_max_deg`.

8. Rebuild/upload the turret and repeat until `PITCH cur` approaches `PITCH tgt`
   without continuous drive into a hard stop.

## Safety notes

- Start with conservative pitch ranges; widen only after observing raw/current
  values on the physical unit.
- `dead_deg` is a pose target, not a limit. The upper command limit remains
  `cmd_max_deg`.
- `target` commands can move the turret. Validate `idle` and `dead` before target
  or fire testing.
