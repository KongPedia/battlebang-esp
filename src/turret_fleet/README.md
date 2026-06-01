# BattleBang Turret Fleet Firmware

Existing `src/turret/` is intentionally left in place as the legacy/reference
firmware. New fleet work for BTB-721 happens here. The compact handoff/context
document is `docs/context.md`; the operator command/OTA usage guide is
`docs/usage.md`; the longer execution plan is `docs/implementation-plan.md`.

This folder is a new, modular firmware path for the future fleet architecture:

- one generic ESP32 firmware image
- runtime turret configuration stored in ESP32 NVS/Preferences
- config updates via Serial or MQTT
- firmware update manifests via MQTT
- firmware binary download via HTTP
- GitHub tag/release driven build artifacts

The goal is to remove per-turret hardcoding from the firmware image. Coordinates,
`turret_id`, calibration, Wi-Fi, and MQTT settings belong to runtime config, not
compile-time macros.

## Current status

This firmware compiles separately from `src/turret/`:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
```

It includes dynamic config storage, MQTT topics, status publish, OTA manifest
parsing, and HTTP OTA download/apply logic. The physical turret motion/fire
control now boots safe into `WAIT_COMMAND`/`UNCONFIGURED`, performs a no-fire
local `HOME` aim (`motion.home`, default `yaw=0,pitch=0`) on normal power-up
before waiting for MQTT, and then forces Wi-Fi/MQTT startup.
OTA/brownout/fire-reset boots inhibit automatic HOME until Command Center sends
an explicit command.
Production tracking is clamped to a runtime-configurable 150° envelope (default
`yaw=-75..75`, `pitch=-75..75`) and blocked when raw feedback is outside that
calibrated envelope/deadzone guard. It
accepts runtime config, validates coordinate frames, computes target yaw/pitch
with the current `src/turret` solver convention, and tracks explicit
fire/pattern state without boot-time fire.

## Folder layout

```text
src/turret_fleet/
  app/        firmware identity/version metadata
  config/     RuntimeConfig + NVS/Preferences storage
  control/    target/aim/idle/dead/fire control and motion feedback
  mqtt/       topic builder + MQTT config/command/OTA bus
  net/        Wi-Fi manager
  ota/        OTA manifest validation + HTTP OTA apply
  docs/       architecture and rollout notes
  examples/   sample config and OTA manifest payloads
  main.cpp    small composition root
```

## Serial provisioning

Recommended first install flow for the fleet firmware is dotenv-driven so the
same command can be reused for `turret_2`, `turret_3`, etc.:

```bash
cp src/turret_fleet/.env.turret_fleet.example src/turret_fleet/.env.turret_fleet
# edit Wi-Fi/MQTT values; do not commit the file

./bin/turret fleet-upload 2
# or, after firmware is already uploaded:
./bin/turret fleet-provision turret_2
```

`fleet-upload` flashes the one generic `esp32dev_turret_fleet` image, then sends
the selected turret's runtime config over USB serial. The ESP stores it in NVS,
so future config changes can arrive via MQTT without per-turret rebuilds.

Manual equivalent after flashing the generic firmware:

```text
provision {"type":"provision","schema":2,"config_version":1,"turret_id":"boss_1f_left","group":"boss","floor":1,"side":"left","coordinate_frame":{"frame_id":"boss_stage_v1","unit":"cm","origin":"boss_stage_center_floor","x_axis":"stage_right","y_axis":"stage_forward","z_axis":"up","mqtt_target_unit":"m"},"pose":{"x_cm":-170,"y_cm":190,"z_cm":134.5,"default_target_z_cm":70},"calibration":{"yaw_zero_reference":"faces_frame_origin","yaw_offset_deg":0,"pitch_offset_deg":0,"yaw_axis_offset_deg":0,"pitch_axis_offset_deg":0,"home_yaw_deg":0,"home_pitch_deg":0},"motion":{"limits":{"yaw_min_deg":-75,"yaw_max_deg":75,"pitch_min_deg":-75,"pitch_max_deg":75},"home":{"yaw_deg":0,"pitch_deg":0},"yaw_stop_us":1500,"pitch_stop_us":1500},"wifi":{"ssid":"YOUR_SSID","password":"YOUR_PASSWORD"},"mqtt":{"host":"<COMMAND_CENTER_IP>","port":1883,"root":"battlebang"},"ota":{"command_center_controlled":true,"auto_check_enabled":false,"channel":"boss-demo","apply_only_in_safe_state":true}}
```

Useful commands:

```text
show-config
show-status / debug
command {"command":"target","frame_id":"boss_stage_v1","target":{"x":1.2,"y":0.4,"z":0.7}}
command {"command":"aim","frame_id":"boss_stage_v1","yaw_deg":0,"pitch_deg":10}
clear-config
check-ota [manifest-url]
check-latest
help
```

By default, `check-ota` and `check-latest` point at the public firmware release
repository:

```text
https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

For a specific GitHub Release based OTA smoke test, `check-ota` can point at a
release asset manifest:

```text
check-ota https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v0.1.0-manual/manifest.json
```

The manifest's `url` may also point at a GitHub Release `.bin` asset. The current
prototype follows HTTPS redirects and uses insecure TLS for easier testing; pin a
CA certificate or signed manifest before production use.

## Local MQTT command helper

See `docs/usage.md` for the full operator runbook, including OTA polling and
post-OTA `initiate` behavior.

After USB provisioning, Wi-Fi/MQTT starts automatically. You can send the same MQTT messages
that Command Center will send. The helper reads `src/turret_fleet/.env.turret_fleet` for
`TURRET_FLEET_MQTT_HOST`, port, and root. If the env file is absent, pass
`--host "$MQTT_BROKER_HOST"`.

`--host` is the MQTT broker/Command Center broker address, not the ESP device IP.
Keep real lab broker IPs in `src/turret_fleet/.env.turret_fleet` or a local shell
variable:

```bash
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
```

```bash
# world-coordinate targets; default MQTT target unit is meters
./bin/turret fleet-mqtt turret_2 target 0 0 1
./bin/turret fleet-mqtt turret_2 target 0 0 2
./bin/turret fleet-mqtt turret_2 target -1 1 2
./bin/turret fleet-mqtt turret_2 target 1 -1 3

# direct local yaw/pitch debug, bypassing target coordinate solve
./bin/turret fleet-mqtt turret_2 aim 0 10
./bin/turret fleet-mqtt turret_2 aim -10 10
./bin/turret fleet-mqtt turret_2 aim 10 10

# re-run local home/init without reboot; this aims at motion.home (default 0,0)
./bin/turret fleet-mqtt turret_2 home
./bin/turret fleet-mqtt turret_2 initiate

# neutral/stop PWM calibration for continuous servos; persisted in NVS
./bin/turret fleet-mqtt turret_2 config --yaw-stop-us 1500 --pitch-stop-us 1500

# calibrated local home and 150-degree safe envelope; persisted in NVS
./bin/turret fleet-mqtt turret_2 config --home-yaw-deg 0 --home-pitch-deg 0 \
  --yaw-min-deg -75 --yaw-max-deg 75 --pitch-min-deg -75 --pitch-max-deg 75

# explicit modes
./bin/turret fleet-mqtt turret_2 idle
./bin/turret fleet-mqtt turret_2 dead
./bin/turret fleet-mqtt turret_2 hold

# fire is an explicit live-fire command; no pre-arm config flag exists
# It starts immediately, even during TARGET motion. Firmware rejects only DEAD,
# brownout lockout, or unconfigured state.
./bin/turret fleet-mqtt turret_2 config --fire-default-hold-ms 500
./bin/turret fleet-mqtt turret_2 fire --duration-ms 500
```

Software calibration is split into two layers:

- `yaw_axis_offset_deg` / `pitch_axis_offset_deg`: local sensor-zero correction.
  Use these when direct `aim 0 10` does not physically point at the expected
  local center. Example: if the center reads `yaw_current_deg=-9`, publish
  `--yaw-axis-offset-deg 9` and retest direct aim.
- `motion.home.yaw_deg` / `motion.home.pitch_deg`: the boot local home target.
  Default is `0,0`; this assumes the local sensor zero has already been
  calibrated to the turret's physical front/level pose.
- `motion.limits`: local yaw/pitch command envelope. Default is 150° total
  (`-75..75`) so normal `HOME`, `target`, `idle`, and `dead` motion does not
  enter the observed yaw feedback deadzone/rail region.
- `yaw_stop_us` / `pitch_stop_us`: per-axis continuous-servo neutral PWM.
  Use these when `hold`/`WAIT_COMMAND` still creeps after outputs are stopped.
- `yaw_offset_deg` / `pitch_offset_deg`: world target-solver correction. Use
  these when direct aim is good but a shared world `target` lands consistently
  left/right/up/down.

Both config patches are persisted in ESP NVS and can be changed later by Command
Center over MQTT without USB reflashing.

## Clamp vs reject policy

Use both, but for different layers:

- Command Center should clamp UI/operator intent to each turret's configured safe
  envelope before publishing commands.
- ESP clamps ordinary target/aim/home/idle/dead setpoints to `motion.limits` so a
  valid world target outside the reachable area becomes the nearest safe aim.
- ESP rejects invalid or unsafe states instead of pretending they are okay:
  frame mismatch, malformed config, config envelopes wider than validation
  allows, firing while unconfigured/dead/locked out, and brownout/fire-reset lockout.

So normal coordinate commands are clamp-on-ESP; authority, schema, state, and
safety failures are reject-on-ESP.

## OTA rollout

Default public release host is this repository's latest fleet manifest:

```text
https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

After PR merge to `main`, the fleet firmware workflow automatically builds one
generic `esp32dev_turret_fleet` binary and publishes `manifest.json`, `.bin`,
and `sha256.txt` as a public GitHub Release. Same-repo public releases use
GitHub Actions' built-in `GITHUB_TOKEN`; `PUBLIC_RELEASE_REPO_TOKEN` is only
needed for cross-repo publishing.

Command Center approval is still required for autonomous polling. Normal
post-merge rollout is manifest-free for the operator:

```bash
# read latest manifest, then approve its build by turret id
curl -L https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
./bin/turret fleet-mqtt turret_2 update --desired-build <LATEST_BUILD> --host "$MQTT_BROKER_HOST"
```

To avoid manually typing the build while still keeping the broker host local:

```bash
export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
BUILD=$(curl -fsSL https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json | python3 -c 'import sys,json; print(json.load(sys.stdin)["build"])')
./bin/turret fleet-mqtt turret_2 update --desired-build "$BUILD" --host "$MQTT_BROKER_HOST"
```

The `update` helper publishes an NVS config patch to
`battlebang/turrets/turret_2/config` with `ota.auto_check_enabled=true`,
`ota.command_center_controlled=true`, `ota.desired_build=<LATEST_BUILD>`, and
the stable latest manifest URL. The ESP polls only when enabled; with
`command_center_controlled=true`, the polled manifest build must exactly match
`ota.desired_build`.

For immediate Command Center jobs where the full manifest body is already known,
publish that manifest to `battlebang/turrets/all/ota` or a per-turret `/ota`
topic.

## Boot behavior

On normal power-up, a configured turret first runs a local no-fire `HOME` aim to
`motion.home` (`0,0` by default), then starts Wi-Fi/MQTT automatically even if an
old NVS config has `network.auto_start=false`. This replaces the older manual
`start-network` bench path; `target` still never auto-fires. World/frame
`(x,y,z)` targeting happens only after an explicit MQTT/serial `target` command
and is still clamped to `motion.limits`.
Command Center can re-run that same local zeroing step without reboot by sending
`{"command":"home"}`; `init` and `initiate` are accepted aliases.

If the ESP boots after OTA, brownout, or a reset while `fire_active` was set, the
firmware does not trust the interrupted motion/fire state and skips boot HOME
output drive. Brownout/fire-reset recovery uses the persisted `recover_req`
marker and attempts automatic safe recovery to `WAIT_COMMAND`: recovery clears
only when current yaw/pitch feedback is stable and inside the calibrated soft
window. If that auto-recovery fails, motion/fire commands remain blocked until
Command Center or a serial operator sends `recover`. Saved pose is diagnostic,
not a resume target. After a successful OTA reboot, confirm the new build on
status, then send `initiate` or a fresh `target`.

## MQTT topics

With `mqtt.root = "battlebang"` and `turret_id = "boss_1f_left"`:

```text
battlebang/devices/{device_id}/config
battlebang/devices/{device_id}/ota
battlebang/devices/{device_id}/status
battlebang/turrets/all/ota
battlebang/turrets/boss_1f_left/config
battlebang/turrets/boss_1f_left/ota
battlebang/turrets/boss_1f_left/command
battlebang/turrets/boss_1f_left/status
```

## Design rule

- Code change: build/release new firmware `.bin`.
- Config change: publish config JSON; ESP stores it in NVS and applies it.
- Firmware rollout: publish OTA manifest over MQTT; ESP downloads `.bin` over HTTP.
- GitHub Release test: fetch `manifest.json` and `.bin` directly from release URLs.
