# MQTT / HTTP Contract

## Roots

Default root is:

```text
battlebang
```

## Config payload

Topic examples:

```text
battlebang/devices/{device_id}/config
battlebang/turrets/{turret_id}/config
```

Payload:

```json
{
  "type": "config",
  "schema": 2,
  "config_version": 1,
  "turret_id": "boss_1f_left",
  "group": "boss",
  "floor": 1,
  "side": "left",
  "coordinate_frame": {
    "frame_id": "boss_stage_v1",
    "unit": "cm",
    "origin": "boss_stage_center_floor",
    "x_axis": "stage_right",
    "y_axis": "stage_forward",
    "z_axis": "up",
    "mqtt_target_unit": "m"
  },
  "pose": {
    "x_cm": -170.0,
    "y_cm": 190.0,
    "z_cm": 134.5,
    "default_target_z_cm": 70.0
  },
  "calibration": {
    "yaw_zero_reference": "faces_frame_origin",
    "yaw_offset_deg": 0.0,
    "pitch_offset_deg": 0.0,
    "yaw_axis_offset_deg": 0.0,
    "pitch_axis_offset_deg": 0.0,
    "home_yaw_deg": 0.0,
    "home_pitch_deg": 0.0
  },
  "motion": {
    "yaw_stop_us": 1500,
    "pitch_stop_us": 1500,
    "limits": {
      "yaw_min_deg": -75.0,
      "yaw_max_deg": 75.0,
      "pitch_min_deg": -75.0,
      "pitch_max_deg": 75.0
    },
    "home": {
      "yaw_deg": 0.0,
      "pitch_deg": 0.0
    }
  },
  "wifi": {
    "ssid": "YOUR_SSID",
    "password": "YOUR_PASSWORD"
  },
  "mqtt": {
    "host": "COMMAND_CENTER_IP",
    "port": 1883,
    "root": "battlebang"
  },
  "ota": {
    "command_center_controlled": true,
    "auto_check_enabled": false,
    "channel": "boss-demo",
    "apply_only_in_safe_state": true
  }
}
```

Coordinate rules:

- `coordinate_frame.frame_id` is the Command Center world frame shared by all four boss turrets.
- MQTT `target` and pattern coordinates use meters by default (`mqtt_target_unit: "m"`); the ESP converts to centimeters before solving yaw/pitch.
- If a command includes a different `frame_id`, the ESP rejects it before motion/fire and reports the mismatch in ACK/status.

## Boot local home

Configured turrets execute a no-fire `HOME` aim to `motion.home` (default local
`yaw=0,pitch=0`) on normal power-up before waiting on Wi-Fi/MQTT. Wi-Fi/MQTT then
starts automatically. No MQTT command is required for this normal boot aim, and
it never triggers `fire`. OTA/brownout/fire-reset boots intentionally inhibit
automatic HOME drive; Command Center should send `home`/`initiate`, `target`, or
`recover` after inspecting status. World-coordinate `(x,y,z)` target solving
starts only when Command Center sends an explicit `target` command.
Command Center may re-run the same local home/init step later by publishing
`{"command":"home"}` on the command topic; `init` and `initiate` are accepted
aliases. This is not a world-coordinate target and does not use the current
position as a relative offset; current feedback is used only by closed-loop
motion to converge to the absolute local home setpoint.

The local `0,0` is not magically discovered from the world frame: it is the
calibrated software zero. Use `yaw_axis_offset_deg`/`pitch_axis_offset_deg` when
the physical turret front/level pose does not read as local `0,0`, then keep
normal motion inside `motion.limits` (default 150° total) so the controller does
not enter the observed yaw feedback deadzone/rail region.

## Clamp vs reject

Command Center and ESP both protect the turret, but not with the same response for every case.

- Command Center clamps operator/UI requests before publishing.
- ESP clamps valid `target`, `aim`, `home`, `idle`, and `dead` setpoints to `motion.limits`.
- ESP rejects malformed or unsafe requests: wrong `frame_id`, invalid config envelopes, motion/fire while unconfigured, fire while dead/locked out, and brownout/fire-reset lockout.

This keeps valid out-of-range coordinates useful by aiming at the closest safe boundary, while still making authority/state/safety failures visible to Command Center.

## Brownout/fire-reset lockout

After ESP `BROWNOUT`, a reset during active `fire`, or a persisted recovery
marker, firmware first forces fire hardware off and attempts automatic safe
recovery. Auto-recovery clears only if current yaw/pitch feedback is stable and
inside the calibrated soft window; it does not use saved pose as a resume target
and it does not drive boot HOME. If auto-recovery fails, commands that could move
or fire are rejected until explicit recovery. Status includes
`motion_state.brownout_lockout`, `fire_recovery_required_at_boot`,
`recovery_lockout_required_at_boot`, `boot_auto_recovery_attempted`, and
`boot_auto_recovery_succeeded`. Command Center can publish:

```json
{"command":"hold"}
{"command":"recover"}
```

Only when status shows `motion_state.brownout_lockout=false` should it resume
`target`, `idle`, `dead`, or `fire`. There is no MQTT/NVS fire pre-arm flag;
the actual blockers are `DEAD`, brownout lockout, and unconfigured state.

## Command payload

Topic:

```text
battlebang/turrets/{turret_id}/command
```

Payload examples:

```json
{"command":"idle"}
{"command":"dead"}
{"command":"home"}
{"command":"fire"}
{"command":"target","frame_id":"boss_stage_v1","target":{"x":3.0,"y":-0.4,"z":0.5}}
```


### Local MQTT helper examples

```bash
./bin/turret fleet-mqtt turret_2 target 0 0 1 --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 aim 0 10
./bin/turret fleet-mqtt turret_2 home
./bin/turret fleet-mqtt turret_2 initiate
./bin/turret fleet-mqtt turret_2 idle
./bin/turret fleet-mqtt turret_2 dead
./bin/turret fleet-mqtt turret_2 config --yaw-axis-offset-deg 9
./bin/turret fleet-mqtt turret_2 config --home-yaw-deg 0 --home-pitch-deg 0 --yaw-min-deg -75 --yaw-max-deg 75 --pitch-min-deg -75 --pitch-max-deg 75
./bin/turret fleet-mqtt turret_2 config --fire-default-hold-ms 500
# direct fire starts immediately; it is not gated by any pre-arm flag or aim stability
./bin/turret fleet-mqtt turret_2 fire --duration-ms 500
```

`yaw_axis_offset_deg` / `pitch_axis_offset_deg` correct the local feedback zero
used by direct `aim` and PID motion. `yaw_offset_deg` / `pitch_offset_deg` remain
world-coordinate target-solver offsets.
`motion.yaw_stop_us` / `motion.pitch_stop_us` correct continuous-servo neutral
PWM so `hold` does not creep while the turret is waiting for the next command.
`motion.home` defines the boot local home pose, and `motion.limits` defines the
persisted local command envelope used by target/aim/idle/dead/home clamping.

## OTA manifest

Topic examples:

```text
battlebang/turrets/all/ota
battlebang/turrets/{turret_id}/ota
battlebang/devices/{device_id}/ota
```

Payload:

```json
{
  "type": "firmware",
  "job_id": "fw-2026-04-15-001",
  "channel": "stable",
  "app": "battlebang-turret-fleet",
  "hardware": "esp32dev-turret-v2",
  "version": "0.2.0",
  "build": 42,
  "url": "http://10.2.80.105:8080/firmware/battlebang-turret-fleet-0.2.0.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 934000,
  "force": false
}
```

ESP behavior:

1. Parse manifest.
2. Check `app` and `hardware`.
3. Check `build > current_build` unless `force=true`.
4. Download `url` via HTTP.
5. Verify `sha256`.
6. Finalize OTA and reboot.

## GitHub Release manifest polling

For a minimal test without a local firmware HTTP server, `manifest.json` and the
firmware `.bin` can both live in a GitHub Release:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v0.1.0/manifest.json
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v0.1.0/battlebang-turret-fleet-0.1.0.bin
```

The ESP serial command for a manual smoke test is:

```text
check-ota https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v0.1.0/manifest.json
```

The firmware also has a default latest-manifest URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

So these serial commands can manually check the latest public firmware release during maintenance. Normal automatic rollout remains Command Center controlled over MQTT:

```text
check-ota
check-latest
```

For controlled fleet rollout, either keep MQTT `/ota` as the immediate trigger
path or enable Command Center-approved polling. Polling is disabled by default.
To permit one build through polling, send runtime config:

```bash
./bin/turret fleet-mqtt turret_2 config \
  --ota-auto-check-enabled true \
  --ota-desired-build 2 \
  --ota-public-manifest-url https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

With `ota.command_center_controlled=true`, the ESP applies a polled manifest only
when `manifest.build == ota.desired_build`; otherwise it publishes/skips with an
OTA status reason. In the current firmware, setting `ota_auto_check_enabled=true`
plus `ota_desired_build=N` is the Command Center approval/apply command for
polling. If the desired two-stage UX is “update available, then apply on command”,
Command Center should discover releases itself, wait for operator approval, then
send that polling config patch or publish the manifest directly to `/ota`.
After OTA reboot, automatic HOME is inhibited; send `initiate` or a new `target`
after status shows the updated build.

## Status payload

Topic:

```text
battlebang/devices/{device_id}/status
battlebang/turrets/{turret_id}/status
```

Payload includes:

```json
{
  "type": "status",
  "device_id": "esp32-001122334455",
  "turret_id": "boss_1f_left",
  "configured": true,
  "firmware_app": "battlebang-turret-fleet",
  "firmware_version": "0.2.0",
  "firmware_build": 42,
  "config_version": 1,
  "frame_id": "boss_stage_v1",
  "mode": "WAIT_COMMAND",
  "pattern_state": "IDLE",
  "fire_state": "SAFE_OFF",
  "aim_state": {
    "last_target_m": {"x": 3.0, "y": -0.4, "z": 0.5},
    "last_target_cm": {"x": 300.0, "y": -40.0, "z": 50.0},
    "solved_yaw_deg": 12.3,
    "solved_pitch_deg": 4.5,
    "yaw_error_deg": 0.8,
    "pitch_error_deg": 1.1,
    "reachable": true
  },
  "ota_state": "idle",
  "wifi": "UP",
  "ip": "10.2.80.123",
  "rssi": -54,
  "uptime_ms": 123456
}
```
