# Turret Fleet Context

This document is the compact handoff/context source for BTB-721 `turret_fleet` work.

## Goal

`src/turret_fleet` is the replacement ESP32 firmware path for operating multiple physical turrets without rebuilding per turret after first install.

- First install is still USB serial because blank ESP32 devices have no Wi-Fi credentials.
- After USB install, the ESP stores `turret_id`, pose, Wi-Fi, MQTT, calibration, motion/fire, and OTA policy in NVS.
- After provisioning, Command Center can update config and trigger commands over MQTT without USB reflashing.
- `src/turret` remains as the existing/legacy turret firmware reference; new fleet work happens in `src/turret_fleet`.

## Boot behavior

A configured fleet turret must not require a manual `start-network` command.

1. Boot in `WAIT_COMMAND` with relay/ESC safe-off and no idle sweep.
2. Start Wi-Fi/MQTT automatically, even if old NVS has `network.auto_start=false`.
3. Subscribe to command/config/OTA topics.
4. Run a small boot axis probe before any tracking drive:
   - sample yaw/pitch potentiometer ADC,
   - pulse only one axis at a time,
   - avoid outward pulses at the edge of the range,
   - learn/confirm motor direction when ADC movement is observed.
5. Only if both axes are inside the configured calibrated software envelope, run one no-fire local `HOME` aim to `motion.home` (`yaw=0,pitch=0` by default).
6. If an axis is already outside the envelope/deadzone guard, log the local home target but keep motion inhibited.
7. Keep waiting for MQTT commands/config updates.

The boot home exists so a powered turret actively returns to its calibrated local `0,0` instead of silently staying wherever the hardware happened to stop.
It is intentionally gated by the boot probe so firmware does not force yaw/pitch into the observed yaw feedback deadzone/rail region.

## Brownout/fire-reset recovery

Brownout or a reset during active firing is treated as an interrupted unsafe operation, not as a normal reboot.

- During an active fire sequence the firmware writes an NVS `fire_active` marker.
- If boot sees ESP reset reason `BROWNOUT`, `fire_active=true`, or a persisted `recover_req=true` marker, it enters brownout/fire-reset lockout.
- Lockout detaches/stops fire outputs, skips boot HOME output drive, and blocks direct fire via `brownout_lockout` until recovery. There is no MQTT/NVS pre-arm enable flag for fire.
- Firmware immediately attempts automatic safe recovery. It clears the lockout only if current yaw/pitch feedback is stable and inside the calibrated soft window; it recovers to `WAIT_COMMAND`, not to a saved target.
- If auto-recovery fails, the lockout marker stays in NVS and target/idle/dead/aim/jog/fire/pattern remain rejected until explicit `recover` succeeds.
- Saved/stale pose is used for diagnostics only and is never trusted as authority to resume motion after a brownout.
- Status exposes `motion_state.brownout_lockout`, `fire_recovery_required_at_boot`, `recovery_lockout_required_at_boot`, `boot_auto_recovery_attempted`, and `boot_auto_recovery_succeeded`.

Recovery commands:

```bash
./bin/turret fleet-mqtt turret_2 hold --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 recover --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 target 0 0 0.7 --host 10.2.80.52  # only after lockout=false
```

## Clamp/reject rule

Do not choose a single behavior for all safety cases. The intended double defense is:

- Command Center clamps desired targets/config UI values before publishing.
- ESP clamps valid motion setpoints to `motion.limits` so out-of-reach targets become nearest safe aim.
- ESP rejects invalid authority/state/safety conditions: wrong frame, invalid config envelope, unconfigured turret, brownout/fire-reset lockout, fire in DEAD mode, or unsafe OTA.

## Coordinate model

Runtime config defines the shared coordinate frame:

- `pose.x_cm`, `pose.y_cm`, `pose.z_cm`: turret position in frame centimeters.
- `pose.default_target_z_cm`: fallback target height used only when an explicit `target` omits `z`.
- MQTT `target` normally uses meters (`coordinate_frame.mqtt_target_unit = "m"`).
- Target solve converts world target coordinates into local yaw/pitch goals.

Calibration is split intentionally:

- `calibration.yaw_axis_offset_deg` / `pitch_axis_offset_deg`: local feedback zero correction. Use these when direct local `aim yaw pitch` does not match the physical center.
- `calibration.yaw_offset_deg` / `pitch_offset_deg`: world target-solver correction. Use these only after direct local aim is good but world-coordinate target lands consistently off.
- `motion.home.yaw_deg` / `motion.home.pitch_deg`: boot local home pose. Default `0,0` assumes the local feedback zero is already calibrated to physical front/level.
- `motion.limits`: persisted local command envelope. Default is 150° total (`yaw=-75..75`, `pitch=-75..75`) and is used by home/aim/target/idle/dead clamping.
- `motion.yaw_stop_us` / `pitch_stop_us`: continuous-servo neutral PWM. Use these when `hold` creeps after outputs are stopped.

## MQTT topics

For `turret_2` and root `battlebang`:

- Commands: `battlebang/turrets/turret_2/command`
- Config: `battlebang/turrets/turret_2/config`
- OTA job: `battlebang/turrets/turret_2/ota`
- Fleet OTA broadcast: `battlebang/turrets/all/ota`
- Status: `battlebang/turrets/turret_2/status` and `battlebang/devices/{device_id}/status`

Useful local helper examples:

```bash
./bin/turret fleet-mqtt turret_2 hold --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 home --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 initiate --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 aim 0 0 --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 target 0 0 0.7 --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 config --yaw-axis-offset-deg 5 --host 10.2.80.52
./bin/turret fleet-mqtt turret_2 config --yaw-stop-us 1500 --pitch-stop-us 1500 --host 10.2.80.52
```

`home` / `init` / `initiate` are command-topic aliases that re-run the local
`motion.home` aim (`0,0` by default) without reboot. They are absolute local
yaw/pitch setpoints, not relative moves from the current position. MQTT
`target` remains a world-coordinate `(x,y,z)` solve and should normally be sent
after the turret has reached local home.

## USB provisioning

Provisioning reads the module-local env file:

- Default env file: `src/turret_fleet/.env.turret_fleet`
- Example file: `src/turret_fleet/.env.turret_fleet.example`
- Local env files are gitignored; do not commit real Wi-Fi/MQTT secrets.

Current expected first-install defaults:

```bash
TURRET_FLEET_WIFI_SSID=...
TURRET_FLEET_WIFI_PASSWORD=...
TURRET_FLEET_MQTT_HOST=10.2.80.52
TURRET_FLEET_MQTT_PORT=1883
TURRET_FLEET_MQTT_ROOT=battlebang
TURRET_FLEET_NETWORK_AUTO_START=true
TURRET_FLEET_YAW_STOP_US=1500
TURRET_FLEET_PITCH_STOP_US=1500
```

Upload/provision one device:

```bash
./bin/turret fleet-upload turret_2 /dev/cu.usbserial-120
# or provision only after an upload:
./bin/turret fleet-provision turret_2 /dev/cu.usbserial-120
```

## OTA / automatic deployment

OTA is Command-Center-controlled by design.

- Public release repo manifests are supported by HTTP OTA config.
- The ESP stores OTA channel/build/manifest config in NVS.
- Command Center can publish OTA jobs to turret-specific or all-turret MQTT topics.
- Firmware applies OTA only in a safe state (`WAIT_COMMAND` / no firing / no pattern).
- Status publishes firmware app/version/build/config fields for convergence checks.

## Motion debugging and current bench finding

Observed problem: during boot initial target `(0,0,default_z)`, pitch/yaw sometimes jumped and ESP brownout reset occurred.

Evidence from serial logs:

- The target solver computed `Computed Pitch[deg] ≈ -9.5` for `(0,0,0.7m)`, so the target math was not commanding “look fully up”.
- Brownout occurred immediately around servo attach/drive while Wi-Fi/MQTT was active.
- Holding both axes or guessing non-1500 neutral PWM can create current spikes or drift.

Software mitigations in this firmware:

- No boot idle sweep.
- No servo attach during Wi-Fi startup.
- Servo outputs are attached lazily only when a motion mode needs them.
- Idle axes are detached instead of continuously held.
- Large corrections drive one axis at a time to avoid two-servo current spikes on USB bench power.
- `yaw_stop_us` / `pitch_stop_us` are runtime/NVS configurable instead of hardcoded.
- Boot probe uses small pulses and refuses tracking when the current ADC readings are outside the soft operating window:
  - yaw soft window: `yaw_raw` roughly `640..3360` (`-112..112 deg`, about 80% of the hard range)
  - pitch soft window: `pitch_raw` roughly `1770..2330` (`-51..21 deg`, about 80% of the hard range)
- `target`, direct `aim`, `idle`, `dead`, and `pattern` motion all call the same soft-window guard before driving motors.
- Status includes `motion_state.safety_inhibited` so Command Center can show when software is intentionally refusing motion.

Latest turret_2 bench evidence after the yaw/pitch recovery guard (2026-05-28):

- USB upload succeeded on `/dev/cu.usbserial-120` with MAC `1c:c3:ab:d2:8d:6c`; build size was about 17.4% RAM and 59.2% flash.
- Boot network is automatic. Serial showed Wi-Fi/MQTT startup, subscription to `battlebang/turrets/turret_2/{command,config,ota}`, and then the initial target preview.
- Pitch now uses the same un-clamped raw ADC preservation as yaw. This exposed the real pitch values and allowed bounded software recovery instead of hiding movement behind a clamp.
- Pitch recovery has been verified from both sides of the soft window:
  - low example: `pitch_raw≈1168`, `+PWM` moved away, `-PWM` moved toward center, then `1349 -> 1550 -> 1772`, ending safe inside `1770..2330`;
  - high example from earlier bench runs: `2821 -> 2682 -> 2544 -> 2418 -> 2298`, ending safe.
- Yaw is handled by the same recovery/probe path when feedback is merely outside the 80% soft window, but the current bench unit reports `yaw_raw=0` at boot and on every MQTT motion command.
- Because even tiny yaw attach attempts at `yaw_raw=0/4095` produced unstable raw jumps and USB/power disconnect behavior, the current firmware refuses yaw servo attach at hard-edge feedback and logs `yaw recovery skipped at hard-edge feedback; refusing servo attach to avoid brownout/overshoot`.
- Current boot initial target result:
  - boot raw: `yaw_raw=0`, `pitch_raw≈1810`;
  - yaw probe/recovery skipped at hard edge;
  - pitch already safe or recovered safely;
  - initial `(0,0,0.7m)` solved as `yaw=0.000`, `pitch=-9.498`, but `Motion tracking: INHIBITED` because yaw feedback remains outside the safe window.
- MQTT command delivery and config persistence were verified against broker `10.2.80.52:1883`:
  - `target (0,0,0.7)` => `yaw=0.000`, `pitch=-9.498`, tracking inhibited by yaw hard-edge;
  - `target (0,1,1.0)` => `yaw=-15.255`, `pitch=-5.661`, tracking inhibited by yaw hard-edge;
  - `target (1,-1,2.0)` => `yaw=3.180`, `pitch=8.360`, tracking inhibited by yaw hard-edge;
  - `target (-1,1,2.0)` => `yaw=-7.125`, `pitch=16.728`, tracking inhibited by yaw hard-edge;
  - idle wide config persisted with yaw `-60..60`, pitch `-15..12`, speeds `24/8`, then `idle` was received but inhibited by yaw hard-edge;
  - idle narrow config persisted with yaw `-15..15`, pitch `-5..5`, speeds `8/4`, then `idle` was received but inhibited by yaw hard-edge;
  - `dead` was received but inhibited by yaw hard-edge;
  - Historical note: the old `fire.hardware_enabled` pre-arm/status field was removed; current firmware treats explicit `fire` as the arm/trigger command unless DEAD/lockout/unconfigured.

Interpretation: the current failure is not MQTT delivery, config persistence, or coordinate solving. The firmware can compute targets and can recover pitch automatically. Yaw feedback is reporting a hard-edge ADC value before motion starts, so the safe behavior is to refuse yaw drive rather than force the motor into a possible mechanical/electrical limit. If the mechanism is visibly centered while `yaw_raw=0`, the likely issue is the yaw potentiometer/feedback path: disconnected signal, short to ground, wrong pin, ADC reference/power, or sensor wiring reversed/broken. Once yaw raw is inside a recoverable range (`>300` and `<3700`, ideally inside `640..3360`), the same bounded software recovery path will run before target/idle/dead motion.

Latest turret_2 edge-guard evidence (2026-05-29):

- Firmware was left unchanged for this pass; only MQTT command arguments were varied.
- Current feedback after previous yaw/pitch movement is at/over the guarded edge:
  - yaw jog reject examples: `yaw_raw=3957` / `3959`, guard range `300..3700`;
  - pitch jog reject examples: `pitch_raw=2873` / `2874`, hard range `1700..2400`.
- MQTT delivery is working. Serial received all four jog commands:
  - `jog yaw minus --delta-us 20 --duration-ms 40`;
  - `jog yaw plus --delta-us 20 --duration-ms 40`;
  - `jog pitch minus --delta-us 20 --duration-ms 40`;
  - `jog pitch plus --delta-us 20 --duration-ms 40`.
- All four commands were rejected before servo attach, and no new brownout occurred during this edge-guard probe.
- Earlier evidence from the same bench session showed `jog yaw plus --delta-us 40 --duration-ms 120` moved yaw raw `3639 -> 3702`; plus/cw therefore moved farther toward the high edge on this unit. From a high raw value, returning toward yaw center should start with minus/ccw, but the current `raw≈3958` is beyond the safe bench range, so firmware refuses to attach the yaw output.

Interpretation update: at the current physical/sensor state this is no longer a “try a wider MQTT command” problem. The command path works, but both axes report values outside their configured hard/bench-safe ranges. The safe software behavior is to block jog/target/idle/dead motion until the raw feedback is brought back into range or the feedback calibration/range is corrected. If the mechanism is not physically at an edge while reporting these raw values, treat it as a feedback-path/calibration issue before enabling closed-loop target tracking.

Latest unsafe/manual calibration pass (2026-05-29):

- Firmware now has `kUnsafeManualCalibrationMode=true` for the current bench session. In this mode:
  - normal soft-window and rail guards no longer block `aim`, `jog`, `idle`, or `dead`;
  - the divergence guard is bypassed so `aim -20` / negative pitch tests keep running instead of stopping early;
  - debug jog accepts wider caller-provided pulses up to `400us` and `1200ms`;
  - servo outputs remain attached at stop PWM because detaching an axis was correlated with brownout/reset on this USB-powered bench.
- Upload succeeded on `/dev/cu.usbserial-120`; build size remained about `17.4%` RAM / `59.2%` flash.
- Brownout did not recur during the unsafe/manual jog and direct-aim sweeps.
- Direct jog evidence:
  - `pitch plus 120us 300ms`: `raw 2255 -> 2215`, `deg 34.81 -> 28.02`, so plus lowers the reported pitch angle.
  - `pitch minus 120us 300ms`: `raw 2217 -> 2317`, `deg 28.36 -> 45.34`, so minus raises the reported pitch angle.
  - `yaw plus 120us 300ms`: `raw 1394 -> 1057`, `deg -48.71 -> -73.74`.
  - `yaw minus 120us 300ms` from `raw=4095` did not move (`4095 -> 4095`), showing the yaw feedback can sit on the ADC rail/wrap edge.
  - `yaw plus 200us 250ms` later jumped `raw 1015 -> 4095` with wrap, confirming yaw can move but wraps/noises aggressively.
- Direct aim evidence:
  - `aim 0,0` did not converge fully; after the window it still had roughly `pitch≈26deg` and yaw jumped around.
  - yaw targets can sometimes get close (`aim yaw -20` reached about `-39.95deg`, `~20deg` error), but yaw raw repeatedly jumps between useful range and `0/4095`, so absolute yaw is not reliable yet.
  - pitch positive target works well: `aim pitch 60` reached `pitch≈57.2deg` within the 5s window.
  - pitch negative target still does not reach: `aim pitch -20` stayed around `pitch≈26.7deg`; this points to sign/range/mechanical load or mapping limits below center, not MQTT delivery.
- Runtime config validation was widened for calibration: `deadPitchDeg` and idle pitch min/max now accept `-90..90`. A persisted config with `yaw_max_delta_us=140`, `pitch_max_delta_us=80`, `axis_divergence_guard_ms=0`, `dead.pitch_deg=45`, and idle pitch `-20..20` was accepted as `config_version=1780021081`.
- `dead` with `dead_pitch=45` was verified after that config: serial showed `mode=DEAD`, `pitch_goal=45`, `pitch≈44.15`, `yaw_max=140`, `pitch_max=80`, `guard=0`.
- Final bench state was parked with `hold`; status showed `mode=WAIT_COMMAND`, `yaw_us=1500`, `pitch_us=1500`, `dead_pitch=45`, `yaw_max=140`, `pitch_max=80`.

Latest extreme range sweep evidence (2026-05-29):

- Logs:
  - `.omx/logs/turret_fleet_extreme_sweep_20260529.log`
  - `.omx/logs/turret_fleet_yaw_stop_scan_20260529.log`
  - `.omx/logs/turret_fleet_yaw_extreme_detach_20260529.log`
- Pitch extreme sweep with `jog pitch minus --delta-us 220 --duration-ms 350` repeatedly raised pitch until it saturated near:
  - `pitch_raw≈2850..2864`
  - computed `pitch≈136..138deg`
  - later pulses changed only `~0..10 raw`, so this is the current effective high/up edge for this mechanism/sensor mapping.
- Pitch plus/down sweep from that edge moved back through the usable range:
  - examples: `2850 -> 2725 -> 2612 -> 2496 -> 2395 -> 2299 -> 2093`
  - computed pitch moved from roughly `136deg` down to roughly `7deg`.
  - Stop/hold at `pitch_stop_us=1500` can still drift slightly, but pitch is mechanically responsive and repeatable.
- Yaw extreme sweep confirms yaw is not dead:
  - plus/cw from rail: `raw 0 -> 4095` on a `400us/500ms` pulse, i.e. it crosses/wraps the ADC range immediately.
  - repeated plus/cw after the wrap stepped back down through the range: `4095 -> 3961 -> 3533 -> 3194 -> 2937 -> 2756`.
  - repeated minus/ccw returned toward the opposite edge: `2753 -> 2765 -> 2957 -> 3229 -> 3648 -> 4095`.
- Direction convention from the latest sweep:
  - yaw `plus/cw` generally decreases raw once away from the `0/4095` wrap edge;
  - yaw `minus/ccw` generally increases raw toward `4095`;
  - at exactly `0/4095`, a pulse can appear as a tiny wrap delta (`0 -> 4095`) even though the mechanism physically moved.
- The requested `detach_after=true` jog option did not detach the yaw output in this firmware build because `kKeepMotionServosAttachedAtStop=true` intentionally parks motion servos attached at stop PWM to avoid the earlier detach/reattach brownout. Status therefore still shows `yaw_attached=true` and `yaw_command_us=1500` after the jog.
- The attempted `yaw_stop_us` scan (`1400..1600`) was inconclusive for neutral tuning because yaw was not attached when those hold samples were taken; all samples stayed at `yaw_raw=0`. A useful neutral scan must be done immediately after a yaw jog while the output is attached, or with a firmware mode that can deliberately detach/attach for the scan.
- After the extreme sweep, yaw was parked off the hard `4095` rail with three plus/cw jogs and a final hold. Final serial debug showed `WAIT_COMMAND`, `yaw_raw≈3350`, `yaw≈96.5deg`, `pitch_raw≈2359`, `pitch≈52.5deg`, `yaw_us=1500`, `pitch_us=1500`.
- Follow-up full-direction yaw sweep used the intended method: from the current yaw, drive `minus/ccw` to the high edge first, then reverse and keep driving `plus/cw` through the mid/origin side without resetting to zero.
  - Start: `yaw_raw≈3344`, `yaw≈96.1deg`.
  - `minus/ccw` high-edge phase: `3348 -> 3642 -> 4095`, then another `minus` pulse stayed `4095 -> 4095`. So this direction reaches the high/wrap edge in about two 400us/500ms pulses from that start point.
  - Reverse `plus/cw` phase: first pulse at the rail stayed `4095 -> 4095`, then moved through the range: `4095 -> 3725 -> 3354 -> 3066 -> 2839 -> 2599 -> 2379 -> 2342 -> 2296`.
  - After `raw≈2290..2296` / computed `yaw≈18deg`, additional `plus/cw` pulses mostly stopped changing raw (`2295 -> 2293`, `2290 -> 2291`, `2293 -> 2294`). This is a repeatable low-side/stall/dead-zone behavior for the current hardware state, not an origin reset.
  - Final hold after that sweep: `WAIT_COMMAND`, `yaw_raw≈2293`, `yaw≈18.0deg`, `pitch_raw≈2360`, `pitch≈52.6deg`, `yaw_us=1500`, `pitch_us=1500`.

Current interpretation after removing safety blocks:

- The earlier “not moving” was partly software blocking from overly narrow safe ranges, and partly brownout from detach/reattach behavior. Those two blockers are now removed for calibration.
- The remaining yaw problem is not the safety guard. With guards bypassed, yaw moves strongly in both directions, but the feedback is cyclic and clamps/wraps at `0/4095`. The current degree mapping also flattens at the top edge (`yaw_current_deg` sticks near `122.52deg` while raw still changes from about `3600..4095`). This needs yaw unwrap/homing/index calibration before reliable coordinate targeting.
- The pitch path is usable on the positive/up side (`~60deg` reached), but the negative/down side does not reach negative setpoints from the current mechanism state. Keep using direct `aim`/`jog` with serial feedback to derive the usable pitch envelope before trusting world target coordinates.
- Do not leave `kUnsafeManualCalibrationMode=true` for unattended production. It is intentionally for watched bench calibration only.

If motion is still unstable, debug in this order:

1. Send `hold`; confirm status shows `WAIT_COMMAND` and outputs stopped/detached.
2. Read status/debug first. If `motion_state.safety_inhibited=true` or raw ADC is outside the soft window, do not keep sending wider target/idle/dead commands; they will be ignored by design.
3. If the mechanism is visibly safe but raw ADC is wrong, debug wiring/mapping or use a deliberately bounded recovery/jog workflow before enabling target tracking.
4. Once raw ADC is inside the soft window, test direct local `aim 0 0`, then `aim 10 0`, `aim -10 0`, `aim 0 10`, `aim 0 -10`.
5. Watch serial `yaw_raw`, `pitch_raw`, `yaw_current_deg`, `pitch_current_deg`, `yaw_command_us`, `pitch_command_us`.
6. If `hold` creeps, tune `motion.yaw_stop_us` / `motion.pitch_stop_us` first.
7. If direct aim center is offset, tune axis offsets next.
8. Only after direct aim is stable, test world `target` coordinates and tune world offsets.
9. If brownout still occurs, the bench power path is insufficient for servo + ESP Wi-Fi current peaks; slower software helps, but stable power is still required for fast motion.

## Safety notes

- Fire is explicit. For bench work, do not send `fire`; there is no pre-arm config flag to toggle first.
- Fire duration default is 500 ms and bounded by config.
- `dead` pitch is config-driven and should stay within safe range.
- Wrong `frame_id` commands are rejected before motion/fire.
- Secrets must stay in ignored env/secret files only.

Latest centered-origin calibration attempt and yaw PID direction fix (2026-05-29):

- Corrected test method per operator intent: do not sweep from an arbitrary current pose. First drive to local `yaw=0,pitch=0`, then test symmetric steps from that origin: `0,+1,+2,+3` and `0,-1,-2,-3` with `1 step = 15deg`.
- Unit clarification:
  - serial/MQTT `jog` uses PWM microseconds (`delta_us`, `duration_ms`), not degrees/radians;
  - serial/MQTT `aim` uses local turret degrees (`yaw_deg`, `pitch_deg`);
  - serial/MQTT `target` uses world target coordinates in configured `mqtt_target_unit` and solves yaw/pitch internally.
- Center-first sweep log: `.omx/logs/turret_fleet_centered_axis_sweep_20260529.log`.
  - Initial state before centering: `yaw_raw≈3249`, `yaw≈89.0deg`, `pitch_raw≈2359`, `pitch≈52.5deg`.
  - Direct `aim 0,0` did not converge from that state: after the command yaw stayed near `89deg` and pitch near `51deg`.
  - Bounded `jog` centering did move the mechanism close to the intended raw origin: final manual center was `yaw_raw=2084`, `yaw≈2.5deg`, `pitch_raw=2137`, `pitch≈14.8deg`.
  - The subsequent direct degree step sweep exposed a yaw PID direction bug: `aim yaw=-15` moved yaw away from center (`yaw≈0.6deg -> 29.3deg`), then further commands drifted to about `47deg`; positive yaw commands near center barely moved.
- Root cause fixed in code: `yawInvertMotor_` default changed to `false`.
  - Bench jog evidence says yaw `+PWM` decreases raw/deg and yaw `-PWM` increases raw/deg.
  - With the monotonic raw-to-degree mapping, positive yaw error must command `-PWM`; the previous `yawInvertMotor_=true` did the opposite and drove yaw away from the target.
  - Firmware build and upload after this change both succeeded for `esp32dev_turret_fleet` on `/dev/cu.usbserial-120`.
- Post-fix verification was blocked by the hardware/feedback state, not by command parsing or MQTT:
  - First post-upload debug showed `yaw_invert_motor=false` as expected.
  - Correct serial command format is `command {json}`; plain JSON is intentionally ignored by the serial shell.
  - After upload/reboot, yaw feedback was stuck at the ADC rail: `yaw_raw=0`, computed `yaw≈-129.95deg`.
  - `aim 0,0` selected yaw and commanded `yaw_us=1415`, but yaw raw stayed `0` for the full observation window.
  - Direct yaw rail probe log: `.omx/logs/turret_fleet_yaw_rail_probe_after_fix_20260529.log`.
    - `jog yaw plus` (`pulse_us=1720`, `220ms`): `raw_before=0`, `raw_after=0`.
    - `jog yaw minus` (`pulse_us=1280`, `220ms`): `raw_before=0`, `raw_after=0`.
    - A second `plus` probe also stayed `0 -> 0`.
  - Conclusion: after the post-upload state, yaw cannot be calibrated in software until feedback leaves the hard rail or the yaw feedback path is repaired. Likely causes are yaw feedback signal short/open, wrong pin/contact, feedback pot/sensor at hard stop, or mechanism jam/over-rotation. The PID sign fix remains valid for the earlier in-range data, but it cannot overcome a raw ADC reading stuck at `0`.
- Pitch was recovered to a safe near-center hold after yaw rail testing:
  - pitch recovery log: `.omx/logs/turret_fleet_pitch_recover_probe_after_yaw_rail_20260529.log`.
  - `pitch minus` jogs raised raw from `1211 -> 1363 -> 1519 -> 1713`.
  - final safe-centering log: `.omx/logs/turret_fleet_pitch_safe_center_after_yaw_rail_20260529.log`.
  - extra `pitch minus` jogs moved `1708 -> 1905 -> 2072`; final hold was `pitch_raw=2073`, `pitch≈3.9deg`.
  - Final bench state after this pass: `WAIT_COMMAND`, `yaw_raw=0` stuck at rail, `pitch_raw≈2073` near center, outputs at stop PWM.

Current calibration stop condition:

- Do not continue symmetric yaw/pitch origin sweeps while `yaw_raw=0`. The origin cannot be established and yaw linearity cannot be measured from a rail-stuck sensor.
- Once yaw feedback is no longer stuck at `0`/`4095`, resume with this exact sequence:
  1. `command {"command":"aim","command_id":"center_0_0","frame_id":"boss_stage_v1","yaw_deg":0,"pitch_deg":0}` and wait until `yaw_current_deg`/`pitch_current_deg` are within about `±2deg`.
  2. Yaw positive from origin: `aim 15,0`, `aim 30,0`, `aim 45,0`, then `aim 0,0`.
  3. Yaw negative from origin: `aim -15,0`, `aim -30,0`, `aim -45,0`, then `aim 0,0`.
  4. Pitch positive from origin: `aim 0,15`, `aim 0,30`, `aim 0,45`, then `aim 0,0`.
  5. Pitch negative from origin: `aim 0,-15`, `aim 0,-30`, `aim 0,-45`, then `hold`.
  6. For each step, record commanded degrees vs `yaw_raw/pitch_raw`, actual degrees, command PWM, and whether `aim_reached` became true.
- Only derive production soft limits after the above centered step sweep shows monotonic raw movement and no rail wrap/stall within the desired envelope.

Latest yaw breakaway clarification (2026-05-29):

- Operator clarified that the physical yaw mechanism had been set to the desired visual origin, but serial still reported `yaw_raw=0`.
- Important distinction: current firmware's yaw degree mapping expects local `yaw=0deg` near `yaw_raw≈2050`; `yaw_raw=0` is the ADC electrical rail/wrap point, not the firmware's linear center.
- A stronger breakaway probe was run after smaller `220us/220ms` jogs appeared stuck:
  - log: `.omx/logs/turret_fleet_yaw_breakaway_probe_20260529.log`.
  - start: `yaw_raw=0`, `yaw≈-129.95deg`, `pitch_raw≈2071`, `pitch≈3.6deg`.
  - `jog yaw plus` with `delta_us=400`, `duration_ms=600` commanded `pulse_us=1900`; immediate jog log still saw `0 -> 0`, but the next debug sample showed `yaw_raw=2430`, `yaw≈28.2deg`.
  - `jog yaw minus` with `delta_us=400`, `duration_ms=600` from `yaw_raw≈2455` commanded `pulse_us=1100` and moved/wrapped back to `yaw_raw=0`.
- Interpretation:
  - yaw motor is not dead; larger pulses can move it.
  - the physical visual origin is currently sitting on the yaw feedback discontinuity/ADC rail (`0/4095`), so tiny ± jogs around origin are not linearly observable by the current raw-to-degree mapping.
  - starting a linear calibration at `yaw_raw=0` is invalid for the current firmware; the first plus/minus direction may appear stuck or may wrap immediately.
  - before deriving safe yaw limits, either mechanically set the visual origin so feedback raw is around mid-scale (`~2050`) or implement a wrap-aware yaw calibration/unwrapping model where raw `0/4095` can intentionally be the zero crossing.

Latest calibrated-home/deadzone policy update (2026-05-29):

- Existing `src/turret` was checked for the historical guard model:
  - yaw ADC clamp: `YAW_LOW_CUT=300`, `YAW_HIGH_CUT=3700`;
  - local yaw mapping center: `b_line=2050`;
  - per-profile `cmd_min_deg` / `cmd_max_deg` command envelopes.
- `turret_fleet` now treats the operator-confirmed visual center as a calibrated software fact, not something the ESP can infer automatically from world coordinates.
  - First calibrate direct local `aim 0 0` with `yaw_axis_offset_deg` / `pitch_axis_offset_deg` until physical front/level is local `0,0`.
  - Then boot `HOME` uses `motion.home` (default `0,0`) and explicit world `target` commands use the coordinate solver afterward.
- Production motion defaults are now a 150-degree total envelope:
  - `motion.limits.yaw_min_deg=-75`, `yaw_max_deg=75`;
  - `motion.limits.pitch_min_deg=-75`, `pitch_max_deg=75`.
- The raw soft/deadzone guard is derived from `motion.limits` plus `yaw_axis_offset_deg` / `pitch_axis_offset_deg`, so if software-zero calibration shifts, the protected raw ADC window shifts with it.
- If yaw feedback is outside the calibrated raw window, automatic boot-home/target/idle/dead tracking is inhibited instead of trying to cross the 360 feedback discontinuity. Use bounded `jog` only for deliberate bench calibration, then return to `aim 0 0` before production target tests.

Latest yaw multi-turn ambiguity observation (2026-05-29):

- Operator observed the turret output physically facing about 180 degrees opposite while firmware status still reported near local yaw zero (`yaw_raw≈2050`, `yaw_current_deg≈0..2`).
- This means yaw feedback is effectively modulo/360 and can repeat the same raw center after an extra motor revolution. With the current sensor alone, firmware cannot distinguish “true front zero” from “one motor revolution later, output 180 degrees off through gearing.”
- Safety policy: once the operator mechanically returns the unit to the true front/level home, normal automatic yaw must remain inside the configured envelope and never require >±90 degrees from home. The current persisted production envelope is more conservative: `motion.limits.yaw=-75..75`, with raw soft window about `1040..3060` when `yaw_axis_offset_deg=0`.
- Do not use repeated wide yaw jogs as production behavior. Bounded jog is only for bench recovery/calibration; after mechanical realignment, verify with small local `aim` steps (`±15`, then `±30`) before world `target` tests.
- A fully software-only fix for arbitrary multi-turn startup would require an additional absolute output-shaft reference, index switch, hard stop homing routine, or a persistent turn counter that never loses synchronization. Without that, the operator-confirmed physical home is a required premise.

Latest full MQTT/serial hardware suite after safe-envelope config (2026-05-29):

- Hardware/bench identity:
  - ESP serial: `/dev/cu.usbserial-120`;
  - turret ID: `turret_2`;
  - MQTT broker: `10.2.80.52:1883`, root `battlebang`;
  - ESP IP during test: `10.2.80.83`.
- Final persisted config after the run:
  - `config_version=1780028414`;
  - `motion.limits.yaw=-55..35deg` (`yaw_soft_raw≈1309..2521`) because the earlier `+45deg` max sat too close to the yaw high deadzone/noise edge;
  - `motion.limits.pitch=-45..70deg`;
  - idle wide/visible sweep: yaw `-45..30deg` at `50deg/s`, pitch `-20..20deg` at `25deg/s`;
  - dead pitch `65deg`;
  - fire has no `hardware_enabled` status/config flag; direct `fire` will energize outputs unless DEAD/lockout/unconfigured.
- Full suite log: `.omx/logs/turret_fleet_corrected_hw_suite_20260529_131433.log`.
  - 15/17 strict checks passed on the first sweep. The two strict failures were final-sample `aim_reached=false` at about `2.1..2.2deg` pitch error while nearby serial samples were already within target; this is a control jitter/tolerance observation, not MQTT/config rejection.
  - Follow-up HOME restore with restored drive config reached local home: final `yaw≈0.37deg`, `pitch≈1.19deg`, `aim_reached=true`, `last_error=""`.
  - Follow-up `target 0 0 0` reached: final `yaw≈0.22deg`, `pitch≈-19.53deg`, solved/clamped pitch `≈-19.81deg`, `aim_reached=true`, `last_error=""`.
- Verified behaviors:
  - Boot after RTS reset auto-started Wi-Fi/MQTT, subscribed topics, then ran boot HOME from a high dead pitch (`pitch≈62.7deg`) back to local `0,0`: status showed `mode=HOME`, `last_command_id=boot-home-0-0`, `yaw≈0.30deg`, `pitch≈0.00deg`, `aim_reached=true`.
  - NVS persistence verified across reset: `config_version=1780028080` survived reset in the full suite; final restored config was then updated to `1780028414`.
  - MQTT `initiate`/`home` from a non-zero target returned to `0,0`: final `yaw≈-0.07deg`, `pitch≈1.19deg`, `aim_reached=true`.
  - World `target 0,0,z` changes pitch monotonically:
    - `z=0m`: solved pitch `≈-19.81deg`, follow-up final `pitch≈-19.53deg`;
    - `z=1m`: solved pitch `≈-4.82deg`, actual `≈-3.40deg` in full sweep;
    - `z=3m`: solved pitch `≈25.30deg`, actual `≈24.62deg`.
  - Yaw clamp/deadzone behavior:
    - target `(-3.947,-1.886,1)` solved `+70.01deg`, clamped to `+35deg`, actual `≈34.23deg`, raw `2511` within soft `1309..2521`;
    - target `(0.223,4.369,1)` solved `-70.01deg`, clamped to `-55deg`, actual `≈-53.76deg`, raw `1326` within soft `1309..2521`.
  - MQTT config updates changed behavior live: narrow idle (`-20..20`, `-8..8`) then wide/fast idle (`-45..30`, `-20..20`) were both accepted and visible in status/config; wide idle stayed inside yaw soft window.
  - `dead` command moved pitch high safely: observed `pitch≈61.8deg` for configured `65deg`, no last_error, fire outputs safe-off.
  - Historical note: `fire --duration-ms 500` was previously blocked by a now-removed pre-arm flag. Current firmware no longer has that flag and does not use it to block explicit fire.
- Important tuning note:
  - Attempting to lower `pitch_min_drive_us` to `40` reduced authority too much: pitch stalled around `+12deg` on HOME and around `-6deg` for `target 0,0,0` while command PWM was only `1540us`.
  - Restored final config uses `pitch_min_drive_us=90` / `pitch_max_delta_us=140` (runtime tracking still caps pitch delta internally) because this reliably crosses friction/backlash and reaches HOME/targets.
- Remaining engineering risks:
  - Pitch has small endpoint jitter around the strict `2deg` `aim_reached` threshold. Current explicit `fire` does not wait for `aim_reached`; if Command Center needs target-locked firing later, implement that policy outside direct `fire` or add a separate queued-fire command.
  - Actual physical fire was not energized in this suite. Current direct `fire` command will energize the launch outputs, so only run it when the mechanism and bench area are safe.
  - Yaw remains modulo/continuous; software cannot distinguish a mechanically 180-degree-wrong output if raw feedback is still near center. Operator-confirmed true physical home remains required before production deployment.


## OTA release and polling

- Public release repo is `KongPedia/battlebang-esp`; verified via `gh repo view` as PUBLIC on 2026-05-29.
- Firmware default latest manifest URL is `https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json`.
- GitHub Actions workflow `.github/workflows/turret-fleet-firmware.yml` builds `esp32dev_turret_fleet` and publishes `manifest.json`, `.bin`, and `sha256.txt`. Same-repo release uses `GITHUB_TOKEN`; `PUBLIC_RELEASE_REPO_TOKEN` is only for cross-repo publishing.
- Direct MQTT `/ota` manifest is immediate Command Center approval.
- Automatic polling is disabled by default. Command Center enables it with `ota.auto_check_enabled=true` and `ota.desired_build=<build>`. With `ota.command_center_controlled=true`, polled manifest build must exactly match `desired_build`.

Example:

```bash
./bin/turret fleet-mqtt turret_2 config \
  --ota-auto-check-enabled true \
  --ota-desired-build 2 \
  --ota-public-manifest-url https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json \
  --host 10.2.80.52
```
