# Turret Fleet Operator Usage

This document is the practical operator contract for BTB-721 `src/turret_fleet`:
first USB provisioning, MQTT commands/config, and OTA rollout.

## What was verified on `turret_2`

Latest verified flow on PR branch `feature/BTB-721-turret-fleet-rebuild-plan`:

1. USB uploaded local firmware build `4`.
2. Normal power/reset boot executed local HOME before MQTT dependency.
3. MQTT target was sent:

   ```bash
   ./bin/turret fleet-mqtt turret_2 target 0 -2 0.6 --host "$MQTT_BROKER_HOST"
   ```

   With default `mqtt_target_unit=m`, this means `(x=0m, y=-2m, z=0.6m)` and
   firmware stores/solves it internally as `(0, -200, 60)` centimeters.
4. GitHub Release `turret-fleet-v0.1.4-pr9-bootfix` was created with manifest
   build `5`.
5. OTA polling was enabled by MQTT config with `desired_build=5`. The ESP polled
   the GitHub Release manifest, downloaded firmware, verified hash, rebooted,
   and came back as build `5`.
6. Post-OTA boot intentionally stayed in `WAIT_COMMAND` and did **not** drive
   HOME automatically. This is a safety rule: OTA/brownout/fire-reset reboots do
   not move motors until Command Center sends an explicit command.
7. MQTT `initiate` was then sent and the turret returned to local `0,0` HOME.

So: polling update was tested end-to-end; the final 0,0 return after OTA was an
explicit MQTT `initiate`, not automatic post-OTA motion.

## First USB provisioning

ESP32 devices have no Wi-Fi/MQTT config on first flash, so the first install is
USB serial. Keep secrets in the module-local dotenv file:

```bash
cp src/turret_fleet/.env.turret_fleet.example src/turret_fleet/.env.turret_fleet
# edit Wi-Fi/MQTT values; do not commit this file
```

Upload one generic firmware image and inject per-turret runtime config into NVS:

```bash
./bin/turret fleet-upload 2
# equivalent id form:
./bin/turret fleet-upload turret_2
```

If firmware is already flashed and only config needs to be injected:

```bash
./bin/turret fleet-provision turret_2
```

The helper auto-detects the only connected USB serial port. Pass a port only if
more than one serial device is connected:

```bash
./bin/turret fleet-upload turret_2 /dev/cu.usbserial-120
```

## Boot behavior

Normal configured power-on:

1. ESP loads NVS config.
2. Firmware runs local HOME immediately before waiting on MQTT/network.
3. Network/MQTT starts automatically and status is published.

Safety-inhibited boot:

- After OTA reboot, brownout reset, or fire/recovery marker, firmware parks fire
  outputs and does **not** drive automatic HOME.
- Command Center should inspect status, then send `initiate`, `target`, or
  `recover` as appropriate.

Local HOME is not a world-coordinate target. It is `motion.home`, default
`yaw=0,pitch=0`, using calibrated local yaw/pitch feedback.

## MQTT topics

Default root: `battlebang`.

Per-turret topics:

```text
battlebang/turrets/{turret_id}/command
battlebang/turrets/{turret_id}/config
battlebang/turrets/{turret_id}/ota
battlebang/turrets/{turret_id}/status
```

Device/global topics:

```text
battlebang/devices/{device_id}/config
battlebang/devices/{device_id}/ota
battlebang/devices/{device_id}/status
battlebang/turrets/all/ota
```

`./bin/turret fleet-mqtt ...` publishes to these topics. It reads
`src/turret_fleet/.env.turret_fleet`; use `--host "$MQTT_BROKER_HOST"` if needed.
The `--host` value is the MQTT broker/Command Center broker address, not the
ESP's own Wi-Fi IP. Keep real lab IPs in your local gitignored env file or
shell, not in committed docs:

```bash
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
```

## Command examples

### Target: world-coordinate solve

```bash
./bin/turret fleet-mqtt turret_2 target 0 -2 0.6 --host "$MQTT_BROKER_HOST"
```

Payload:

```json
{"command":"target","target":{"x":0.0,"y":-2.0,"z":0.6}}
```

By default target units are meters (`coordinate_frame.mqtt_target_unit=m`). ESP
converts to centimeters and solves yaw/pitch from turret pose `(x_cm,y_cm,z_cm)`
to target `(x,y,z)`. Valid target/aim setpoints are clamped to `motion.limits`.

Optional frame guard:

```bash
./bin/turret fleet-mqtt turret_2 target 0 -2 0.6 --frame-id boss_stage_v1 --host "$MQTT_BROKER_HOST"
```

If `frame_id` does not match config, firmware rejects before motion/fire.

### Initiate/home: local 0,0

```bash
./bin/turret fleet-mqtt turret_2 initiate --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 home --host "$MQTT_BROKER_HOST"
```

Both publish `{"command":"home"}`. This returns to configured local
`motion.home` and does not solve world coordinates.

### Aim: direct local yaw/pitch debug

```bash
./bin/turret fleet-mqtt turret_2 aim 0 10 --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 aim -10 5 --host "$MQTT_BROKER_HOST"
```

Use this for local axis calibration because it bypasses world target solving.

### Idle / dead / hold / recover

```bash
./bin/turret fleet-mqtt turret_2 idle --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 dead --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 hold --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 recover --host "$MQTT_BROKER_HOST"
```

- `idle`: sweeps within configured idle yaw/pitch ranges.
- `dead`: moves to `motion.dead.pitch_deg`, holds yaw, and rejects `fire`.
- `hold`: stops motion/fire outputs and waits.
- `recover`: clears brownout/recovery lockout only if feedback is stable and
  inside the calibrated safe window.

### Fire

```bash
./bin/turret fleet-mqtt turret_2 fire --duration-ms 500 --host "$MQTT_BROKER_HOST"
```

Fire is immediate; no `--fire-hardware-enabled` or pre-arm config exists. It is
rejected only when unconfigured, in `DEAD`, or in brownout/recovery lockout.

## Runtime config examples

All config patches are saved to ESP NVS and survive reboot/OTA.

### Local zero and target-solver offsets

```bash
# Local sensor zero correction: affects direct aim/home/motion feedback.
./bin/turret fleet-mqtt turret_2 config \
  --yaw-axis-offset-deg 1.0 \
  --pitch-axis-offset-deg -0.5 \
  --host "$MQTT_BROKER_HOST"

# World solver correction: use only if direct local aim is good but world target
# consistently lands left/right/up/down.
./bin/turret fleet-mqtt turret_2 config \
  --yaw-offset-deg 0 \
  --pitch-offset-deg 0 \
  --host "$MQTT_BROKER_HOST"
```

### Home and safe envelope

```bash
./bin/turret fleet-mqtt turret_2 config \
  --home-yaw-deg 0 \
  --home-pitch-deg 0 \
  --yaw-min-deg -55 \
  --yaw-max-deg 35 \
  --pitch-min-deg -45 \
  --pitch-max-deg 70 \
  --host "$MQTT_BROKER_HOST"
```

Config validation rejects envelopes wider than the firmware's allowed 150° total
range. Ordinary target/aim/home/idle/dead commands are clamped into the accepted
envelope.

### Motion speed / drive strength

```bash
./bin/turret fleet-mqtt turret_2 config \
  --yaw-max-delta-us 180 \
  --pitch-max-delta-us 120 \
  --yaw-min-drive-us 130 \
  --pitch-min-drive-us 70 \
  --servo-attach-settle-ms 200 \
  --axis-switch-cooldown-ms 500 \
  --host "$MQTT_BROKER_HOST"
```

Larger `*_max_delta_us` moves faster but can increase current draw/brownout risk.
Keep `ota.apply_only_in_safe_state=true` and watch `motion_state` during tuning.

### Idle, dead, and fire duration

```bash
./bin/turret fleet-mqtt turret_2 config \
  --idle-yaw-min-deg -45 \
  --idle-yaw-max-deg 30 \
  --idle-yaw-speed-deg-s 50 \
  --idle-pitch-min-deg -20 \
  --idle-pitch-max-deg 20 \
  --idle-pitch-speed-deg-s 25 \
  --dead-pitch-deg 65 \
  --fire-default-hold-ms 500 \
  --host "$MQTT_BROKER_HOST"
```

## OTA usage

### Build/release from GitHub Actions

After PR merge to `main`, `.github/workflows/turret-fleet-firmware.yml` runs
automatically when fleet firmware/workflow files change. The push build creates
a public GitHub Release in this repo with:

- tag `turret-fleet-v0.1.${GITHUB_RUN_NUMBER}-main`
- firmware build `${GITHUB_RUN_NUMBER}`
- assets `manifest.json`, `battlebang-turret-fleet-{version}.bin`, `sha256.txt`

Manual `workflow_dispatch` still exists for PR smoke tests or one-off builds:

```bash
gh workflow run turret-fleet-firmware.yml \
  --repo KongPedia/battlebang-esp \
  --ref feature/BTB-721-turret-fleet-rebuild-plan \
  -f version=0.1.4-pr9-bootfix \
  -f build=5 \
  -f firmware_base_url="" \
  -f public_release_repo=KongPedia/battlebang-esp \
  -f public_release_target_branch=feature/BTB-721-turret-fleet-rebuild-plan \
  -f create_release=true
```

Release assets are reachable through either the exact tag or the stable latest URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v{version}/manifest.json
https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

### Manifest-free Command Center-approved polling

Normal operator flow should **not** paste a release-specific manifest URL. Read
the latest manifest build, then approve that build by turret id:

```bash
# 1) after the merge Action completes, read the build from the latest release
curl -L https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
# or: gh release view --repo KongPedia/battlebang-esp --json tagName,publishedAt,url

# 2) approve exactly that build for one turret; this publishes to /config
./bin/turret fleet-mqtt turret_2 update --desired-build <LATEST_BUILD> --host "$MQTT_BROKER_HOST"
```

Convenience one-liner when you want the helper to read latest `build` but still
keep the broker IP outside git:

```bash
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
BUILD=$(curl -fsSL https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json | python3 -c 'import sys,json; print(json.load(sys.stdin)["build"])')
./bin/turret fleet-mqtt turret_2 update --desired-build "$BUILD" --host "$MQTT_BROKER_HOST"
```

`update` is only a convenience wrapper around the existing NVS config patch. It
sends:

```json
{
  "type": "config",
  "schema": 2,
  "config_version": 1780000000,
  "ota": {
    "command_center_controlled": true,
    "auto_check_enabled": true,
    "desired_build": 7,
    "channel": "stable",
    "public_manifest_url": "https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json",
    "local_mirror_url": "",
    "check_interval_s": 30,
    "apply_only_in_safe_state": true
  }
}
```

The ESP then polls the latest manifest itself. With
`command_center_controlled=true`, it applies only when all are true:

- `ota.auto_check_enabled=true`
- manifest `app` and `hardware` match
- manifest `build` is greater than current `firmware_build`
- manifest `build == ota.desired_build`
- hash/size verification passes
- if `apply_only_in_safe_state=true`, turret is not firing, not idle/dead/pattern,
  and target/home motion is settled at stop PWM

Watch `battlebang/turrets/turret_2/status` every heartbeat/event for:

```text
config_applied
ota_poll_not_approved
ota_poll_skipped
ota_deferred
ota_downloading
ota_rebooting
connected
heartbeat
```

Important status fields proving the ESP saw and accepted the rollout policy:

```text
firmware_version, firmware_build
ota_auto_check_enabled=true
ota_desired_build=<LATEST_BUILD>
ota_manifest_url=https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

After successful OTA reboot, automatic HOME drive is inhibited to avoid motion
surprise. Command Center should then send one of:

```bash
./bin/turret fleet-mqtt turret_2 initiate --host "$MQTT_BROKER_HOST"
./bin/turret fleet-mqtt turret_2 target 0 -2 0.6 --host "$MQTT_BROKER_HOST"
```

After the desired build is reached, disable polling unless continuous polling is
intended:

```bash
./bin/turret fleet-mqtt turret_2 config --host "$MQTT_BROKER_HOST" \
  --config-version $(date +%s) \
  --ota-auto-check-enabled false \
  --ota-desired-build <LATEST_BUILD> \
  --ota-apply-only-in-safe-state true
```

### Two-stage “update available, apply when commanded” flow

If Command Center wants a visible two-stage flow, do not set
`ota_auto_check_enabled=true` immediately. Instead:

1. Command Center watches GitHub releases/webhook and decides update is available.
2. UI/operator approves.
3. Command Center sends `fleet-mqtt <turret_id> update --desired-build N` to let
   the ESP poll latest and apply exactly build `N`.

Current ESP firmware does not publish an “available but waiting for approval”
event by itself while `auto_check_enabled=false`; that discovery belongs to
Command Center. Once approved, ESP status confirms the desired build and polling
URL.

### Direct OTA MQTT job

Publish a complete manifest to one turret or all turrets only for immediate jobs
where Command Center already has the manifest body:

```bash
./bin/turret fleet-ota-publish dist/manifest.json battlebang/turrets/turret_2/ota
./bin/turret fleet-ota-publish dist/manifest.json battlebang/turrets/all/ota
```

Direct `/ota` is treated as an explicit Command Center-approved update job. It
still honors manifest validation and safe-state gating. For normal post-merge
rollout, prefer `fleet-mqtt ... update --desired-build N`.

## Status fields to monitor

Subscribe to:

```text
battlebang/turrets/turret_2/status
```

Important fields:

```text
firmware_version, firmware_build, config_version, mode, last_error
motion_state.brownout_lockout
motion_state.safety_inhibited
motion_state.yaw_current_deg / pitch_current_deg
motion_state.yaw_goal_deg / pitch_goal_deg
motion_state.yaw_command_us / pitch_command_us
motion_state.target_slew_active
motion_state.aim_reached
ota_auto_check_enabled, ota_desired_build, ota_manifest_url
fire_state, fire_sequence
```

## Safety summary

- Normal boot: auto HOME to local `0,0`.
- OTA/brownout/fire-reset boot: no auto HOME; wait for explicit Command Center
  `initiate`, `target`, or `recover`.
- New motion commands preempt stale in-flight state before tracking.
- ESP clamps normal valid setpoints to configured motion limits.
- ESP rejects wrong frame, invalid config, unconfigured operation, fire in DEAD,
  and brownout/recovery lockout.
