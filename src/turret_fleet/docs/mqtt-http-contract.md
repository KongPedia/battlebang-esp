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
    "x_axis": "stage_forward",
    "y_axis": "stage_right",
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
- For the current boss layout, left/right is the world `y` axis; top/bottom row height is the world `z` axis.
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
{"command":"pattern","pattern_id":"two_point_bounce","pattern_instance_id":"boss-phase-1-a-b","frame_id":"boss_stage_v1","params":{"loop":2}}
{"command":"pattern","pattern_id":"two_point_bounce","pattern_instance_id":"boss-phase-1-a-b","frame_id":"boss_stage_v1","params":{"loop":2,"dwell_ms":1000,"fire_ms":1000,"move_timeout_ms":60000,"return_to":"wait_command","points":[{"x":0.0,"y":0.75,"z":-0.6},{"x":0.0,"y":-0.5,"z":-0.6}]}}
```

BTB-726 keeps the player-facing catalog intentionally small and readable:
`lane_sweep`, `two_point_bounce`, and `telegraph_column`. Command Center
compiles 1F/2F pair choreography into per-turret primitive commands. Operator
calibration may still use `calibration_no_fire`, which never starts relay/ESC
fire.

Pattern coordinates/timings can be stored in runtime config and updated over the
normal `/config` topic. When `params.points` is omitted, firmware uses the
configured preset for the requested `pattern_id`; when `params.points` is
present, the command overrides the preset for that one run. `lane_sweep`
defaults to one narrow ping-pong round trip; each sweep leg starts one fire
window capped by `fire_ms`, cuts early if yaw reaches the endpoint, and then
waits safe-off plus the endpoint dwell before the next leg.
`telegraph_column` may store multiple candidate points; with `random:
true` it chooses one per command, while `params.point_index` selects a fixed
candidate.

```json
{
  "type": "config",
  "schema": 2,
  "config_version": 123,
  "patterns": {
    "presets": {
      "two_point_bounce": {
        "loop": 2,
        "move_timeout_ms": 60000,
        "dwell_ms": 1000,
        "fire_ms": 1000,
        "return_to": "wait_command",
        "points": [
          {"x": 0.0, "y": 0.75, "z": -0.6},
          {"x": 0.0, "y": -0.5, "z": -0.6}
        ]
      },
      "telegraph_column": {
        "loop": 1,
        "random": true,
        "move_timeout_ms": 10000,
        "dwell_ms": 1000,
        "fire_ms": 1000,
        "return_to": "wait_command",
        "points": [
          {"x": 0.0, "y": 0.0, "z": -0.6},
          {"x": 0.0, "y": 0.5, "z": -0.6},
          {"x": 0.0, "y": -0.5, "z": -0.6}
        ]
      }
    }
  }
}
```


### Local MQTT helper examples

Set the broker host locally first. This host is the MQTT broker/Command Center
address, not the ESP device IP:

```bash
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
```

```bash
./bin/turret fleet-mqtt turret_2 target 0 0 1 --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 aim 0 10
./bin/turret fleet-mqtt turret_2 home
./bin/turret fleet-mqtt turret_2 initiate
./bin/turret fleet-mqtt turret_2 idle
./bin/turret fleet-mqtt turret_2 dead
./bin/turret fleet-mqtt turret_2 config --yaw-axis-offset-deg 9
./bin/turret fleet-mqtt turret_2 config --profile-file src/turret_fleet/profiles/turret_2.json --patterns-file src/turret_fleet/pattern_presets/turret_2.json
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
persisted outer safety/deadzone envelope. Normal target/aim/pattern/idle/dead/
home setpoints are clamped to the inner 80% command envelope derived from those
limits around home.

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
  "url": "http://COMMAND_CENTER_IP_OR_DNS:8080/firmware/battlebang-turret-fleet-0.2.0.bin",
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
firmware `.bin` can both live in a GitHub Release. Merge-to-main Action builds
update the stable latest manifest URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json
```

Exact release URLs also exist for debugging:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v{version}/manifest.json
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v{version}/battlebang-turret-fleet-{version}.bin
```

Serial maintenance commands can manually check the configured/default public
manifest:

```text
check-ota [manifest-url]
check-latest
```

Normal fleet rollout remains Command Center controlled over MQTT. Operators do
not paste a manifest URL; they approve exactly one latest-build number by turret
id:

```bash
./bin/turret fleet-mqtt turret_2 update --desired-build <LATEST_BUILD> --host "$MQTT_BROKER_HOST"
```

That command publishes a config patch to `battlebang/turrets/turret_2/config`:

```json
{
  "type": "config",
  "schema": 2,
  "ota": {
    "command_center_controlled": true,
    "auto_check_enabled": true,
    "desired_build": 7,
    "public_manifest_url": "https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json",
    "check_interval_s": 30,
    "apply_only_in_safe_state": true
  }
}
```

With `ota.command_center_controlled=true`, the ESP applies a polled manifest only
when `manifest.build == ota.desired_build`; otherwise it publishes/skips with an
OTA status reason. If the desired two-stage UX is “update available, then apply
on command”, Command Center should discover releases itself, wait for operator
approval, then send `update --desired-build N`. Direct `/ota` remains available
for immediate jobs that publish the complete manifest body.

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
  "mode": "PATTERN",
  "command_state": "busy",
  "command_in_progress": true,
  "ready_for_next_command": false,
  "preemptible": true,
  "active_command_id": "cmd-123",
  "command_policy": "latest_wins_preemptive",
  "pattern_state": "MOVING",
  "pattern_id": "two_point_bounce",
  "pattern_instance_id": "boss-phase-1-a-b",
  "pattern_step_index": 0,
  "pattern_step_type": "MOVE",
  "pattern_step_count": 16,
  "pattern_loop_index": 0,
  "pattern_loop_count": 2,
  "pattern_last_error": "",
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
  "ota_command_center_controlled": true,
  "ota_auto_check_enabled": true,
  "ota_desired_build": 42,
  "ota_channel": "stable",
  "ota_manifest_url": "https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json",
  "wifi": "UP",
  "ip": "<ESP_IP>",
  "rssi": -54,
  "uptime_ms": 123456
}
```

Command Center should wait for `ready_for_next_command=true` when it wants
sequential choreography. While a finite command is running (`pattern`, `target`,
`home`, or fire sequence), the ESP publishes status at 1 Hz and immediately when
the command/pattern/fire state changes. New commands are still accepted while
busy: `command_policy=latest_wins_preemptive` means the ESP stops motion, forces
fire outputs safe, clears the active pattern, and then applies the newest
command.
