# BTB-721 Turret Fleet Rebuild / Pattern Engine / OTA Plan

Date: 2026-05-28
Ticket: BTB-721 `[Demo][Boss Turret] 4대 터렛 패턴 엔진 및 OTA 배포 전환`

## Decision

`src/turret_fleet/`의 현재 scaffold는 실제 터렛 제어가 없는 safe stub이므로, 다음 구현에서는 **부분 수정이 아니라 삭제 후 재구성**한다.
단, 이미 검증된 `src/turret/`의 하드웨어 제어 핵심은 버리지 않고 새 fleet firmware의 기준 구현으로 옮긴다.

핵심 방향:

1. `src/turret/`는 현재 USB/PlatformIO 기반 검증용 active firmware로 유지한다.
2. `src/turret_fleet/`는 새로 만든 generic OTA firmware가 된다.
3. 처음 빈 ESP에는 Wi-Fi/MQTT가 없으므로 **최초 1회는 반드시 USB serial로 generic firmware를 올리고 serial config로 `turret_id`/Wi-Fi/MQTT/pose/calibration/OTA policy를 주입**한다.
4. 시작 직후 자동 `idle`/sweep은 하지 않는다. 단, 설정된 터렛은 부팅 후 Wi-Fi/MQTT를 자동 연결하고, MQTT 연결이 확인되면 fire 없이 로컬 `motion.home`(기본 `yaw=0,pitch=0`)으로 HOME aim을 수행한다.
5. 최초 provisioning 이후에는 로컬 개발과 현장 튜닝 모두 MQTT config/command/pattern/OTA로 검증한다. 터렛별 firmware 재빌드/USB 재배포를 정상 운영 경로로 쓰지 않는다.
6. Command Center가 config와 OTA policy를 내려주면 ESP는 NVS에 저장하고 재부팅 후에도 유지한다.
7. OTA 자동배포는 Command Center가 원하는 build/channel을 판단하고 MQTT OTA job을 내리는 방식이 기본이다. ESP가 public GitHub release를 임의로 자동 적용하지 않는다.
8. OTA는 버전/build/status 기반으로 자동 업데이트 가능해야 하되, 발사/pattern 중에는 적용하지 않는다.

## Current Evidence

| Area | Evidence | Plan implication |
|---|---|---|
| Current fleet control is stub | `src/turret_fleet/control/turret_control.cpp:6-28` only stores/apply config and leaves `loop()` empty. `src/turret_fleet/control/turret_control.cpp:30-52` only changes mode strings for `idle/dead/target/fire`. | Do not patch around this scaffold. Rebuild control modules from proven `src/turret` runtime. |
| Current active turret has real hardware pins | `src/turret/runtime/state.inc:8-16` defines yaw/pitch ADC, servo pins, relay CH1/2/3, ESC pin. | New fleet firmware must preserve these defaults for current hardware revision. |
| Relay/fire electrical behavior is already encoded | `src/turret/runtime/state.inc:18-22` defines active-low relay and fire hold limits. `src/turret/runtime/support.inc:50-101` implements relay safe-off/attach. `src/turret/runtime/control.inc:148-240` defines CH2 -> CH1 -> CH3 -> BLDC hold -> CH3 off -> CH1 off -> CH2 off sequence. | Port relay/ESC sequencing as a dedicated fire state machine, not ad hoc digital writes. |
| PID and ADC mapping are proven enough to reuse | `src/turret/runtime/state.inc:46-59` defines PID gains/deadbands/min-drive/invert flags. `src/turret/runtime/support.inc:247-369` reads ADC, converts yaw/pitch, runs PID, and checks aim reached. | Fleet firmware should reuse the same algorithm first, then make gains/motion profile runtime-configurable. |
| Current build-time per-turret config exists | `scripts/turret_config.py:81-123` maps motion profile JSON into macros. `scripts/turret_config.py:152-169` injects turret pose/calibration/motion defines. | Those fields become runtime config persisted in NVS, not per-device binaries. |
| Current target command is aim-only | `src/turret/runtime/control.inc:246-309` computes target yaw/pitch and explicitly logs auto-fire disabled. `src/turret/runtime/network.inc:150-199` supports only `idle/dead/fire/target`. | Preserve aim-only `target`; add explicit `pattern` command for boss patterns and keep `fire` explicit inside pattern steps. |
| Current firmware auto-enters idle | `src/turret/main.cpp:87-90` resets state then calls `enterIdleMode()`. `src/turret/runtime/control.inc:57-69` attaches servos and starts idle sweep behavior. | New fleet firmware must replace this boot behavior with `SAFE_WAIT_COMMAND` / `WAIT_COMMAND`. |
| Current fleet already has OTA/config pieces | `src/turret_fleet/main.cpp:35-51` applies/persists serial config. `src/turret_fleet/config/runtime_config.cpp:153-201` persists config in Preferences. `src/turret_fleet/docs/mqtt-http-contract.md` documents config/status/OTA topics. | Reuse the concept/contract, but rebuild around real turret control and stronger auto-update policy. |
| Current active deployment is per-unit USB/build-time | `platformio.ini:100-122` defines `esp32dev_turret_1` ... `esp32dev_turret_6`. `scripts/turret_config.py:152-169` injects ID/pose/calibration/motion macros. | Fleet must replace per-turret firmware builds with one generic binary plus serial/MQTT runtime config. |
| Public release pipeline exists conceptually | `.github/workflows/turret-fleet-firmware.yml:74-128` builds fleet firmware and uploads release assets. `scripts/turret_fleet/make_release_manifest.py:18-50` writes manifest fields. | Keep public GitHub release artifacts, but align manifest identity with rebuilt firmware constants and Command Center rollout policy. |

## Requirements Summary

### Functional requirements

1. **Generic firmware**: one `esp32dev_turret_fleet` binary works for every boss turret after provisioning.
2. **USB-first provisioning**: first install is always local USB serial. The generic image is flashed once, then serial config injects `turret_id`, boss mapping, Wi-Fi/MQTT, pose, calibration, motion/fire profile, and OTA policy into NVS.
3. **Initial local boot HOME**: after normal power-up, the turret must not idle sweep or fire automatically. A configured turret aims at local `motion.home` (default `yaw=0,pitch=0`) as a no-fire boot HOME target before waiting on Wi-Fi/MQTT, then starts Wi-Fi/MQTT automatically; OTA/brownout/fire-reset boots inhibit automatic HOME until Command Center sends an explicit command. World `(x,y,z)` target solving is only for explicit target commands.
4. **Runtime config**: Command Center can set and persist:
   - `turret_id`, floor/side/group metadata
   - coordinate frame: `frame_id`, unit convention, origin, axes, and MQTT target unit
   - pose in that frame: `x_cm`, `y_cm`, `z_cm`, `default_target_z_cm`
   - calibration/alignment: yaw zero reference, yaw/pitch offsets, and known no-fire calibration points
   - motion profile: calibrated `motion.home`, 150° default `motion.limits`, ADC/deadzone guards, cmd min/max, invert flags, min drive, PID gains, aim tolerance
   - fire profile: relay active-low, relay step delay, fire hold min/default/max
   - MQTT broker/root/credentials; no broker is compiled into fleet firmware. First USB provisioning writes the current Command Center/broker IP into NVS, and Command Center can later replace it over MQTT config.
   - OTA policy: Command Center controlled flag, optional self-check flag, channel, desired version/build, public manifest URL/local mirror URL
5. **Local MQTT development**: laptop/Jetson local broker can configure and command a turret with `mosquitto_pub` without USB after initial USB-serial provisioning.
6. **Current command compatibility**: `target`, `fire`, `idle`, `dead` stay supported.
7. **Pattern command**: add `pattern` for boss patterns. ESP executes timing-critical steps locally; Command Center sends high-level pattern/config.
8. **OTA**: ESP receives OTA manifest/policy, compares current app/version/build, downloads binary, verifies SHA-256, applies only in safe states, reboots, and reports updated status.
9. **Status/observability**: ESP publishes status with firmware version/build, config version, `frame_id`, mode, pattern state, fire state, aim/alignment state, Wi-Fi/MQTT state, IP/RSSI, last command/job IDs, OTA state.

### Non-functional requirements

- No automatic physical firing from config, boot, OTA, or plain `target`.
- `dead` must interrupt pattern/fire and force safe-off as much as the current relay/ESC sequence allows.
- OTA/config must be idempotent and versioned; stale config must be rejected.
- OTA auto-update must be controllable by Command Center and must not start during `FIRING` or active `PATTERN` unless explicitly forced by maintenance mode.
- ESP-side public release polling, if implemented, is disabled by default and must be bounded by Command Center-approved channel/build policy.
- Demo behavior must be deterministic: no uncontrolled human tracking, no random fire windows.


## Provisioning and Rollout Invariants

1. **Blank ESP path**: `pio run -e esp32dev_turret_fleet -t upload --upload-port ...` is the first and only required USB firmware install path for a new turret.
2. **Serial config immediately after USB**: use serial `config {json}` / `provision {json}` to set `turret_id`, boss floor/side/group, `coordinate_frame`, pose, calibration, motion/fire tuning, Wi-Fi, MQTT, and OTA policy.
3. **NVS ownership**: accepted config is persisted in ESP NVS/Preferences and loaded before Wi-Fi/MQTT startup on every reboot.
4. **No per-turret rebuild after provisioning**: `turret_1`, `turret_2`, ... build-time config remains legacy/oracle only. Fleet operation uses one binary plus runtime config.
5. **Command Center desired state**: Command Center owns desired `config_version`, desired `firmware_build`, approved OTA channel, and per-turret rollout status.
6. **Public release artifacts**: CI publishes `.bin`, `manifest.json`, and SHA-256 to a public GitHub release repo. Command Center reads/knows the approved manifest and sends MQTT OTA jobs.
7. **Convergence loop**: Command Center repeatedly compares turret status with desired config/build and republishes config/OTA jobs until status converges.

## Target State Machine

```text
UNCONFIGURED
  - no valid turret_id or required network/MQTT config
  - physical outputs safe-off
  - accepts first USB-serial provisioning/config
  - if Wi-Fi/MQTT is already provisioned, may publish device_id status and accept device-scoped config

WAIT_COMMAND
  - configured and network ready
  - no idle sweep at boot
  - servos may be detached or holding current pose depending on lazy-output setting
  - accepts target/fire/pattern/idle/dead/config/ota
  - status includes enough config/build fields for Command Center convergence checks

TARGETING
  - target command received
  - compute yaw/pitch, run PID until aim reached or superseded
  - no fire unless separate fire or pattern step requests it

PATTERN
  - pattern instance active
  - executes target/dwell/fire/wait/loop steps locally
  - can be interrupted by dead, idle, new pattern, or config safety change

FIRING
  - relay/ESC sequence active
  - target updates rejected or queued according to explicit policy

IDLE
  - explicit command only
  - optional sweep behavior allowed only because user/operator requested idle

DEAD
  - safe-off, pitch-up/dead pose behavior if configured
  - rejects fire/pattern until cleared by idle or target

OTA_PENDING / OTA_DOWNLOADING / OTA_APPLYING
  - allowed only from WAIT_COMMAND or explicit maintenance mode
  - publishes progress/status

ERROR
  - invalid config, sensor fault, relay/ESC fault, OTA failure, or watchdog fault
```

## Files to Delete/Recreate in `src/turret_fleet/`

During implementation, remove the current scaffold modules and recreate the directory around real control. The docs can be kept/rewritten, but code should not inherit stub control behavior.

Delete/recreate candidates:

```text
src/turret_fleet/app/*
src/turret_fleet/config/*
src/turret_fleet/control/*
src/turret_fleet/mqtt/*
src/turret_fleet/net/*
src/turret_fleet/ota/*
src/turret_fleet/examples/*
src/turret_fleet/main.cpp
```

New target structure:

```text
src/turret_fleet/
  main.cpp
  app/
    firmware_info.h
    state_machine.{h,cpp}
  config/
    runtime_config.{h,cpp}
    config_store.{h,cpp}
    config_schema.md
  hardware/
    pins.h
    relay_fire.{h,cpp}
    servo_axis.{h,cpp}
    adc_reader.{h,cpp}
    output_safety.{h,cpp}
  motion/
    aim_solver.{h,cpp}
    pid_controller.{h,cpp}
    motion_profile.{h,cpp}
  pattern/
    pattern_command.{h,cpp}
    pattern_engine.{h,cpp}
    boss_patterns.{h,cpp}
  mqtt/
    topics.{h,cpp}
    mqtt_bus.{h,cpp}
    command_handler.{h,cpp}
    status_publisher.{h,cpp}
  net/
    wifi_manager.{h,cpp}
  ota/
    ota_policy.{h,cpp}
    ota_manifest.{h,cpp}
    http_ota.{h,cpp}
  docs/
    implementation-plan.md
    mqtt-contract.md
    ota-rollout.md
    local-dev.md
  examples/
    config.boss_turret.json
    pattern.sweep_lr.json
    ota-manifest.example.json
```

## Runtime Config Contract

### Example config payload

Topic:

```text
battlebang/devices/{device_id}/config
battlebang/turrets/{turret_id}/config
```

Payload:

```json
{
  "type": "config",
  "schema": 2,
  "config_version": 12,
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
    "points": [
      {"name": "center_chest", "target_m": {"x": 0.0, "y": 0.0, "z": 0.7}},
      {"name": "left_marker", "target_m": {"x": -0.6, "y": 0.0, "z": 0.7}},
      {"name": "right_marker", "target_m": {"x": 0.6, "y": 0.0, "z": 0.7}}
    ]
  },
  "motion": {
    "yaw_stop_us": 1500,
    "pitch_stop_us": 1500,
    "home": {"yaw_deg": 0.0, "pitch_deg": 0.0},
    "limits": {"yaw_min_deg": -75.0, "yaw_max_deg": 75.0, "pitch_min_deg": -75.0, "pitch_max_deg": 75.0},
    "pid": {"kp": 0.80, "ki": 0.020, "kd": 0.15},
    "aim_tolerance_deg": {"yaw": 2.0, "pitch": 2.0},
    "yaw": {
      "adc_low_cut": 300,
      "adc_high_cut": 3700,
      "cmd_min_deg": -140.0,
      "cmd_max_deg": 140.0,
      "min_drive_us": 85.0,
      "invert_motor": false
    },
    "pitch": {
      "adc_low_cut": 1700,
      "adc_high_cut": 2400,
      "cmd_min_deg": -59.4,
      "cmd_max_deg": 59.4,
      "min_drive_us": 75.0,
      "invert_motor": false,
      "dead_deg": 59.4
    }
  },
  "fire": {
    "relay_active_low": true,
    "relay_step_delay_ms": 250,
    "hold_default_ms": 1000,
    "hold_max_ms": 60000,
    "sequence": ["ch2_on", "ch1_on", "ch3_on", "esc_hold", "esc_stop", "ch3_off", "ch1_off", "ch2_off"]
  },
  "safety": {
    "boot_mode": "wait_command",
    "auto_fire_on_target": false,
    "lazy_motion_outputs": true,
    "lazy_relay_outputs": true,
    "allow_idle_sweep": true
  },
  "mqtt": {
    "host": "COMMAND_CENTER_IP",
    "port": 1883,
    "root": "battlebang",
    "username": "",
    "password": ""
  },
  "ota": {
    "command_center_controlled": true,
    "auto_check_enabled": false,
    "channel": "boss-demo",
    "desired_build": 42,
    "public_manifest_url": "https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v0.3.0/manifest.json",
    "local_mirror_url": "http://COMMAND_CENTER_IP_OR_DNS:8080/firmware/turret/manifest.json",
    "check_interval_s": 300,
    "apply_only_in_safe_state": true
  }
}
```

Rules:

- `config_version` must be monotonic. Lower versions are rejected.
- Missing optional fields keep previous values.
- `safety.boot_mode` defaults to `wait_command`; `idle` is never the default boot mode.
- `ota.command_center_controlled` defaults to `true`; ESP self-check is disabled unless `auto_check_enabled=true` is explicitly configured for maintenance.
- Unsafe hardware pin changes are not runtime-configurable for the current hardware revision; pin constants stay in `hardware/pins.h` unless a future board revision requires a signed hardware profile.
- Config update must not trigger fire or pattern execution.
- If config changes motion/fire settings while `FIRING`, defer apply until fire sequence reaches safe-off.


## Coordinate Frame and Target Alignment Contract

This is a blocking contract for the fleet rewrite because the current field problem is not just OTA/config; multiple turrets must agree on the same world coordinate system before `target` or patterns can be trusted.

1. **Single canonical frame**: Command Center owns a named frame such as `boss_stage_v1`. Config must define `frame_id`, unit, origin, and axis directions. All four boss turrets must report the same active `frame_id` before coordinated patterns are allowed.
2. **Unit boundary is explicit**: MQTT/Command Center target and pattern points use meters by default (`mqtt_target_unit: "m"`) to stay compatible with the current MQTT docs. ESP internally converts to centimeters before using the copied `src/turret` ballistic/aim solver.
3. **Pose is in the canonical frame**: each turret stores `pose.x_cm/y_cm/z_cm` in the same frame, plus boss metadata (`floor`, `side`, `group`). No per-turret coordinate macros are allowed in the fleet binary.
4. **Yaw/local zero is calibrated, not implicit**: current `src/turret` yaw math effectively assumes a calibrated mechanical/software zero. Fleet config makes this explicit through `calibration.yaw_axis_offset_deg` / `pitch_axis_offset_deg` for local `aim 0,0`, `motion.home` for boot HOME, and `yaw_offset_deg` / `pitch_offset_deg` only after world-coordinate solving. Do not silently assume every physical mount faces the same origin until direct local calibration proves it.
5. **Target and pattern share one solver**: `target`, `sweep_lr`, `sweep_vertical`, `point_burst`, and `calibration_no_fire` all pass through the same frame validation, unit conversion, yaw/pitch solve, limits, and PID path.
6. **Frame mismatch is a command error**: inbound `target`/`pattern` payloads may include `frame_id`; if it is present and does not match the configured frame, reject the command and publish an error/ACK. If absent, default to the configured frame and include that frame in status.
7. **Alignment observability is mandatory**: target/pattern status must include the input target, converted cm target, `frame_id`, turret pose, solved yaw/pitch, clamped yaw/pitch, yaw/pitch offsets, pitch validity/reachability, and aim-reached/error fields so Command Center can see why two turrets disagree.
8. **No-fire calibration before live fire**: each physical turret must pass `calibration_no_fire` over at least three known points (center/left/right or equivalent floor markers) before enabling live fire. The same world point should produce physically consistent aim across all four turrets after offsets are applied.

Acceptance:

- A single target point in `boss_stage_v1` can be sent to all four boss turrets and each reports the same `frame_id`, its own pose, solved yaw/pitch, and aim error without firing.
- A `frame_id` mismatch is rejected before motion/fire.
- Unit conversion is covered by tests: MQTT meters -> internal centimeters -> solver output.
- The old per-device `turrets.json`/macro coordinates are used only as migration input or parity fixtures, not as fleet runtime truth.


## First-USB Provisioning Contract

New ESP devices do not have Wi-Fi/MQTT credentials, so the first step is not OTA:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet -t upload --upload-port /dev/cu.usbserial-XXXX
```

Then send serial provisioning config. The implementation may accept both `config {json}` and `provision {json}` as aliases, but the effect is the same: validate, persist to NVS, apply safe fields, connect Wi-Fi/MQTT, and publish status.

```text
config {"type":"config","schema":2,"config_version":1,"turret_id":"boss_1f_left","group":"boss","floor":1,"side":"left","coordinate_frame":{"frame_id":"boss_stage_v1","unit":"cm","origin":"boss_stage_center_floor","x_axis":"stage_right","y_axis":"stage_forward","z_axis":"up","mqtt_target_unit":"m"},"pose":{"x_cm":-170,"y_cm":190,"z_cm":134.5,"default_target_z_cm":70},"calibration":{"yaw_zero_reference":"faces_frame_origin","yaw_offset_deg":0,"pitch_offset_deg":0,"yaw_axis_offset_deg":0,"pitch_axis_offset_deg":0,"home_yaw_deg":0,"home_pitch_deg":0},"motion":{"home":{"yaw_deg":0,"pitch_deg":0},"limits":{"yaw_min_deg":-75,"yaw_max_deg":75,"pitch_min_deg":-75,"pitch_max_deg":75},"yaw_stop_us":1500,"pitch_stop_us":1500},"wifi":{"ssid":"...","password":"..."},"mqtt":{"host":"<COMMAND_CENTER_IP>","port":1883,"root":"battlebang"},"ota":{"command_center_controlled":true,"auto_check_enabled":false,"channel":"boss-demo","desired_build":1,"apply_only_in_safe_state":true}}
```

Provisioning acceptance:

- A blank ESP becomes a named boss turret without rebuilding firmware.
- `show-config` / status shows the persisted `turret_id`, `config_version`, `frame_id`, pose/calibration, Wi-Fi/MQTT, and OTA policy after reboot.
- Serial remains a recovery path for lost Wi-Fi/MQTT config, but normal tuning after provisioning is MQTT config.

## MQTT Command Contract

### Backward-compatible commands

Topic:

```text
battlebang/turrets/{turret_id}/command
```

Payloads:

```json
{"command":"target","command_id":"cmd-001","frame_id":"boss_stage_v1","target":{"x":3.0,"y":-0.4,"z":0.5}}
{"command":"fire","command_id":"cmd-002","duration_ms":1000}
{"command":"idle","command_id":"cmd-003"}
{"command":"dead","command_id":"cmd-004"}
```

Semantics:

- `target`: aim only, no fire.
- `fire`: fire immediately if not `DEAD` and relay/fire state is safe to start.
- `idle`: explicit operator idle; may enable configured idle sweep.
- `dead`: highest-priority interrupt; safe-off and reject fire/pattern.

### New pattern command

```json
{
  "command": "pattern",
  "command_id": "boss-phase-1-0007",
  "pattern_id": "sweep_lr",
  "pattern_instance_id": "boss-phase-1-left-right",
  "frame_id": "boss_stage_v1",
  "ttl_ms": 3000,
  "params": {
    "loop": 3,
    "phase_offset_ms": 0,
    "move_timeout_ms": 2500,
    "dwell_ms": 400,
    "fire_ms": 700,
    "return_to": "wait_command",
    "points": [
      {"x": 1.2, "y": -0.6, "z": 0.7},
      {"x": 1.2, "y": 0.6, "z": 0.7}
    ]
  }
}
```

Required pattern IDs:

1. `sweep_lr`: left/right world-coordinate sweep with optional fire at endpoints.
2. `sweep_vertical`: fixed x/y target with target-z / pitch-limited vertical sweep.
3. `point_burst`: 1/2/3 target points, dwell, optional fire at each point.
4. `telegraph_fire`: aim/dwell/visible delay/fire for predictable demo-safe attacks.
5. `calibration_no_fire`: move through points with no relay/ESC fire.

Safety:

- Pattern engine internally uses the same frame validation, unit conversion, aim solver, and PID as `target`.
- Fire steps call the same `relay_fire` state machine as `fire`.
- `dead` interrupts all patterns.
- New `pattern` with a different `pattern_instance_id` replaces the previous active pattern after safe-off.
- `target` during active pattern either interrupts pattern or is rejected; choose one policy and report it in status. Recommended: reject unless `interrupt=true`.

## OTA Plan

### OTA manifest

```json
{
  "type": "firmware",
  "job_id": "turret-fw-2026-05-28-001",
  "app": "battlebang-turret-fleet",
  "hardware": "esp32dev-turret-v2",
  "channel": "boss-demo",
  "version": "0.3.0",
  "build": 42,
  "url": "http://COMMAND_CENTER_IP_OR_DNS:8080/firmware/battlebang-turret-fleet-0.3.0.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 934000,
  "force": false,
  "min_config_schema": 2
}
```

ESP behavior:

1. Accept OTA manifest/job from `battlebang/turrets/{turret_id}/ota`, `battlebang/turrets/all/ota`, or an explicitly enabled maintenance poll URL.
2. Reject wrong `app`, wrong `hardware`, lower/equal `build` unless `force=true`, missing SHA-256, incompatible config schema, or non-approved channel/build.
3. Apply only from `WAIT_COMMAND`, `UNCONFIGURED` with enough network config, or explicit `MAINTENANCE` mode.
4. If currently `PATTERN`/`FIRING`, publish `ota_deferred` and retry when safe.
5. Download via HTTP/HTTPS from the manifest `url` (public GitHub release asset or local mirror), verify size and SHA-256, call OTA update, publish `ota_rebooting`, reboot.
6. After reboot, publish status with new `firmware_build` and `last_ota_job_id`.

Command Center responsibilities:

- Track latest approved public GitHub release manifest/build/channel.
- Compare each turret status `firmware_build`, `firmware_app`, `hardware`, `config_version`, and `ota_state` with desired state.
- Publish OTA manifest/job to one turret, group, or all turrets when status is behind.
- Re-publish config/policy if a device reboots with old config.
- Retry or mark failed/deferred based on status until convergence.
- Show rollout status: pending/downloading/rebooting/updated/failed/deferred.

## Local Development Workflow

Initial USB serial provisioning is mandatory for a blank ESP because it has no Wi-Fi/MQTT yet. After the first generic firmware upload and serial config, normal development and field tuning should use MQTT; USB becomes a recovery/fallback path only.

Example commands:

```bash
# 1. Build generic firmware
./.venv-pio/bin/pio run -e esp32dev_turret_fleet

# 2. Upload once over USB to a blank ESP
./.venv-pio/bin/pio run -e esp32dev_turret_fleet -t upload --upload-port /dev/cu.usbserial-XXXX

# 3. First-provision over serial: turret_id + Wi-Fi + MQTT + pose + OTA policy
#    Serial line example: config {json}

# 4. After reboot/network, tune config over MQTT without USB/rebuild
mosquitto_pub -h <COMMAND_CENTER_IP> -t battlebang/devices/esp32-001122334455/config -m @src/turret_fleet/examples/config.boss_turret.json

# 5. Aim only
mosquitto_pub -h <COMMAND_CENTER_IP> -t battlebang/turrets/boss_1f_left/command -m '{"command":"target","frame_id":"boss_stage_v1","target":{"x":1.2,"y":0.4,"z":0.7}}'

# 6. Fire explicitly
mosquitto_pub -h <COMMAND_CENTER_IP> -t battlebang/turrets/boss_1f_left/command -m '{"command":"fire","duration_ms":1000}'

# 7. Run a pattern
mosquitto_pub -h <COMMAND_CENTER_IP> -t battlebang/turrets/boss_1f_left/command -m @src/turret_fleet/examples/pattern.sweep_lr.json

# 8. Command Center triggers OTA manifest/job over MQTT
mosquitto_pub -h <COMMAND_CENTER_IP> -t battlebang/turrets/boss_1f_left/ota -m @src/turret_fleet/examples/ota-manifest.example.json
```

## Implementation Phases

### Phase 0 - Preserve active firmware and freeze behavior

- Keep `src/turret/` buildable and unchanged except for tests/docs needed to compare behavior.
- Add/keep tests that describe current target/fire/no-autofire behavior before porting.
- Capture a minimal hardware-in-loop smoke checklist for one turret.

Acceptance:

- `pio run -e esp32dev_turret_5` succeeds.
- Current `target` remains aim-only.
- Current relay sequence is documented with line references and test notes.

### Phase 1 - Delete/recreate fleet skeleton

- Remove current `src/turret_fleet` code modules that only stub control.
- Recreate modules listed in the target structure.
- Keep PlatformIO env `esp32dev_turret_fleet`.
- Add firmware identity: app, hardware, semver, monotonic build.

Acceptance:

- `pio run -e esp32dev_turret_fleet` succeeds.
- Boot log starts at `WAIT_COMMAND`, then normal power-up reports a no-fire local `HOME` before MQTT dependency, never `IDLE`.
- No relay/ESC/motion output is attached unless a command requires it.

### Phase 2 - Port hardware, PID, target, and fire

- Port pins and defaults from `src/turret/runtime/state.inc:8-22`.
- Port relay safe-off and lazy attach from `src/turret/runtime/support.inc:50-101`.
- Port ESC attach/run/stop behavior from `src/turret/runtime/support.inc:157-195`.
- Port ADC/PID/aim reached from `src/turret/runtime/support.inc:247-369`.
- Port target solver from `src/turret/runtime/control.inc:246-309`.
- Port fire state machine from `src/turret/runtime/control.inc:148-240` and `src/turret/runtime/control.inc:324-360`.

Acceptance:

- `target` computes same yaw/pitch for the same config and input as current firmware, including meters-to-centimeters conversion fixtures.
- Coordinate-frame mismatch is rejected before target/pattern motion.
- Status exposes enough alignment fields to diagnose inconsistent yaw/pitch across the four boss turrets.
- `fire` follows the same relay/ESC order and hold bounds.
- `dead` interrupts outputs and rejects fire.

### Phase 3 - USB-first provisioning + runtime config + NVS persistence

- Convert build-time macro fields from `scripts/turret_config.py:81-123` and `scripts/turret_config.py:152-169` into runtime config fields.
- Implement first USB-serial provisioning for blank ESP devices: `turret_id`, boss floor/side/group, pose, calibration, motion/fire tuning, Wi-Fi, MQTT, and OTA policy.
- Persist config through ESP Preferences/NVS and load it before Wi-Fi/MQTT startup.
- Implement partial MQTT updates with monotonic `config_version` after provisioning.
- Keep serial fallback for recovery, but make MQTT config the normal post-provisioning path.

Acceptance:

- One generic firmware image can become any of the four boss turrets via serial config only.
- Config survives reboot and is visible in status/show-config.
- Stale `config_version` is rejected.
- MQTT config can tune pose/calibration/motion/fire/OTA without USB or rebuild.
- Motion/fire config changes are reflected in status.
- Updating config never starts pattern/fire.

### Phase 4 - MQTT local development and status

- Implement topics for device config, turret config, command, pattern, OTA, and status.
- Add retained/last-will strategy if broker supports it.
- Add status heartbeat at 1-5 Hz during command/pattern/OTA, slower when idle/waiting.
- Add example JSON payloads under `src/turret_fleet/examples/`.

Acceptance:

- A laptop can configure, target, fire, pattern, and OTA-trigger a turret with MQTT only after initial flash.
- Status includes firmware/config/mode/pattern/fire/aim/network fields.
- Command IDs appear in ACK/status for debugging.

### Phase 5 - Pattern engine

- Implement pattern parser and validation.
- Implement `sweep_lr`, `sweep_vertical`, `point_burst`, `telegraph_fire`, `calibration_no_fire`.
- Patterns are world-coordinate based by default in the configured `frame_id`; raw angle override is maintenance-only.
- Pattern points use the same target schema/unit conversion as `target`; `calibration_no_fire` is the required alignment check before live fire.
- Add loop, dwell, fire duration, move timeout, phase offset, return-to-state.

Acceptance:

- Boss 4-turret pattern can be configured by Command Center using turret groups.
- Pattern timing is ESP-local after one MQTT command.
- `dead` stops an active pattern.
- `target` does not auto-fire unless it is a pattern fire step.
- Four turrets can run `calibration_no_fire` on the same known world points and report comparable solved/actual aim data before live-fire patterns are enabled.

### Phase 6 - Public GitHub release OTA + Command Center auto-deploy

- Keep manifest validation from the old concept but rebuild it into the new state machine.
- Align firmware constants, release script defaults, examples, and docs for `app`, `hardware`, `version`, and monotonic `build`.
- Publish generic firmware `.bin`, `manifest.json`, and SHA-256 to the public GitHub firmware release repo.
- Add Command Center controlled OTA policy: Command Center decides desired build/channel and publishes MQTT OTA jobs/manifests.
- Add safe-state gating and deferred update.
- Publish OTA status and post-reboot version so Command Center can keep checking until rollout converges.
- Keep ESP self-polling optional/disabled by default; if enabled for maintenance, it must obey approved channel/build and safe-state gates.

Acceptance:

- Older/equal build is skipped unless `force=true` in an explicit maintenance job.
- Wrong app/hardware/sha/size/schema is rejected.
- Active firing/pattern defers OTA.
- Command Center can detect an out-of-date turret and push an MQTT OTA job.
- Device updates to desired build and reports the new build after reboot without USB reflashing.

### Phase 7 - Command Center integration

Command Center side work is outside this repo but should be planned together:

- Add turret config editor/API and persistence.
- Add boss turret group model: 1F left/right, 2F left/right.
- Add MQTT publishers for config, pattern, OTA job.
- Add status collector and rollout UI/logs.
- Add release watcher or operator-triggered deploy flow against the public GitHub firmware release repo.
- Add convergence loop: compare desired config/build to status and retry config/OTA jobs until each turret is updated or explicitly failed.

Acceptance:

- Operator can set config in Command Center and see it applied/persisted on ESP.
- Operator can select pattern and see command/status correlation.
- Operator can deploy firmware and see version convergence.

## Verification Plan

### Static/build

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
./.venv-pio/bin/pio run -e esp32dev_turret_5
```

### Host tests

- Extend `tests/python/test_turret_state_contract.py` for:
  - boot state is `WAIT_COMMAND`, not `IDLE`
  - `target` does not auto-fire
  - stale config rejected
  - OTA manifest lower build rejected
  - coordinate frame mismatch rejected
  - MQTT target meter units convert to internal centimeters
  - pattern command validates required fields
- Add pure C++/host tests where feasible for:
  - aim solver parity against `src/turret` fixtures
  - four-turret coordinate/alignment fixtures using one shared world target
  - PID output clamp/min drive
  - fire sequence order
  - OTA version/build comparison

### MQTT integration

- Local broker test publishes config/command/pattern/OTA payloads.
- Verify ACK/status topics include command/job IDs.
- Verify offline/reconnect resubscribe behavior.

### Hardware-in-loop

1. Build one generic fleet image.
2. Flash one blank ESP over USB.
3. Provision `turret_id`, Wi-Fi/MQTT, `coordinate_frame`, pose, calibration, motion/fire config, and OTA policy over serial.
4. Reboot and verify `WAIT_COMMAND`/`UNCONFIGURED` + safe-off and persisted config/status.
5. Send MQTT config update and verify no USB/rebuild is required.
6. Send target with matching `frame_id` and verify aim only plus reported solved yaw/pitch/aim error.
7. Send target/pattern with a wrong `frame_id` and verify rejection before motion/fire.
8. Send the same known target point to all four boss turrets and compare reported frame/pose/solved yaw/pitch/aim error.
9. Send fire and verify relay/ESC sequence.
10. Send calibration_no_fire pattern over at least three known points.
11. Send sweep/point pattern with dry-fire disabled first.
12. Enable live fire under controlled test.
13. Publish higher build to public GitHub release repo, trigger OTA via Command Center MQTT job, and verify reboot/status convergence.
14. Repeat on 4 boss turrets.

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Deleting scaffold loses useful OTA/config code | Treat current code as reference only; copy concepts after reviewing line-by-line. Keep this plan and contracts in git before deletion. |
| Runtime-configurable motion settings can create unsafe behavior | Validate bounds, keep hardware pins compile-time by hardware version, require safe state before applying motion/fire changes. |
| Auto-update starts during boss fight | Command Center owns desired rollout; ESP gates OTA to `WAIT_COMMAND`/maintenance and publishes `ota_deferred` during pattern/fire. |
| Pattern engine accidentally auto-fires from target | Keep `target` and `fire` separate; only pattern step type `fire` can call fire state machine. |
| Turrets disagree on target alignment because coordinate frames or mechanical zero differ | Make `frame_id`/units/axes explicit, reject frame mismatch, require no-fire calibration points, and publish solved-vs-actual aim diagnostics before live fire. |
| Command Center and ESP schema drift | Version config schema and status schema; include `schema` and `firmware_build` in every status. |
| MQTT retained stale commands replay on boot | Do not retain command messages; use `ttl_ms`, `command_id`, and timestamp/stale rejection. Config may be retained/versioned. |
| OTA brick risk | SHA-256, size, app/hardware/build checks, OTA partition table, public manifest identity alignment, one-device canary before group rollout, USB serial fallback. |

## ADR

### Decision

Rebuild `src/turret_fleet/` from scratch around the proven `src/turret/` hardware/PID/fire logic, while making identity/config/pattern/OTA runtime-driven through USB-first serial provisioning, MQTT, NVS, Command Center desired-state control, and public GitHub release OTA artifacts.

### Drivers

1. The current fleet folder has OTA/config scaffolding but no real turret control.
2. The current active turret path has real pin/PID/relay behavior but is per-device build-time configured.
3. Blank ESP devices require first local USB/serial provisioning before Wi-Fi/MQTT/OTA can work.
4. Boss demo needs four coordinated turrets with high-level Command Center control and OTA rollout.

### Alternatives considered

1. **Keep patching current `src/turret_fleet` scaffold**
   Rejected: control is a stub and would encourage a half-real architecture.
2. **Keep using only `src/turret` per-device builds**
   Rejected: not scalable for OTA/config/pattern iteration and four-turret boss deployment.
3. **Rebuild fleet firmware from proven active turret logic**
   Chosen: preserves hardware behavior while solving runtime config and OTA.

### Consequences

- There will be one deliberate rewrite of `src/turret_fleet`.
- `src/turret` remains the comparison oracle until fleet reaches hardware parity.
- USB serial remains mandatory only for first install/recovery; normal operation moves to MQTT config and OTA.
- Command Center needs matching config/pattern/OTA desired-state and convergence support.

### Follow-ups

- Create implementation branch/PR for BTB-721 after this plan is accepted.
- Update Jira BTB-721 with the final plan path and acceptance criteria.
- Add Command Center ticket/subtasks for desired config state, public release watcher, MQTT OTA jobs, status convergence, and rollout UI/logging if not already covered.
- Keep `BB_TURRET_FLEET_APP_NAME` / `BB_TURRET_FLEET_HARDWARE`, release manifest generator defaults, example manifests, and docs aligned whenever OTA identity changes.
- Create/measure the real `boss_stage_v1` coordinate frame and no-fire calibration points for all four boss turrets before live-fire rollout.
