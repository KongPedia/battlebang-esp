# Turret Fleet Architecture

## Current ownership

`firmware/turret_fleet/` is the active generic turret product; `src/turret/` is retained legacy/reference, not a scaffold
that this task should delete or rebuild. One fleet image is configured per turret through runtime NVS, not numbered builds.
The implemented controller is `control/turret_control.cpp`; pure pattern parsing/compilation lives in `control/patterns/`.

| Owner | Responsibility |
| --- | --- |
| Product `TurretControl` | Motion/PID, relay/ESC sequence, command/state admission, calibration, interrupts and safe-off |
| Pattern compiler | Validate/normalize supported primitives into a plan; no direct hardware output |
| Product config adapter | Domain validation, persisted key map, pose/calibration and supported tuning |
| Shared `bb_esp_*` | Selective topic/config/NVS/network/OTA mechanisms; not turret safety policy |
| Provisioner / Command Center | Runtime identity and desired config/build, approved rollout and multi-turret choreography |

The external-owner row is a responsibility boundary, not evidence that every server-side convergence feature is deployed.
Concrete composition is sufficient; do not add a generic hardware command executor, new RTOS task or interface hierarchy.
See [repo C++ criteria](../../../.agents/references/engineering.md) for lifetime, timing, SRP and resource review.

## Config, control and update paths

```text
Approved first USB install -> validated serial provisioning -> persisted runtime NVS
Command Center -> versioned MQTT config -> domain validation -> NVS/apply/status
Command Center -> explicit MQTT target/fire/pattern -> local admission -> bounded controller execution
Approved release manifest -> product safe-state gate -> shared HTTP(S)/size/SHA-256 update -> reboot/status
```

MQTT carries small control/config/status/OTA messages; HTTP(S) carries the binary from the manifest URL, which may be a
versioned release asset or an approved mirror. A Command Center binary server is not mandatory. No range/resume, trusted
TLS, image signatures or rollback guarantee is established by this diagram; review the actual transport before claiming one.

NVS owns accepted identity, `stage_id`, network, pose, calibration, motion/fire tuning and OTA policy. Commands/status are
runtime observations, not persisted authority to resume firing. Preserve namespaces/key maps, config-version rejection,
failed-save reporting and secret masking. Individual Preferences writes are not a proven atomic config transaction.

## Motion and coordinate contract

- Command Center names the canonical frame; pose and targets must agree on frame/origin/axes. A supplied mismatched
  `frame_id` is rejected before effects; absent frame uses the configured frame and status reports it.
- MQTT target/pattern coordinates are meters by default; the solver uses centimeters internally. Preserve explicit conversion,
  `pose.x_cm/y_cm/z_cm`, shared target/pattern solver, yaw/pitch limits and unreachable-target reporting.
- Local `aim`/HOME zero uses axis offsets; world-solving correction uses separate yaw/pitch offsets. Do not substitute one
  for the other or assume mounts share a mechanical origin. Per-device legacy macro coordinates are reference inputs only.
- `target` is aim-only. Player patterns are `lane_sweep`, `two_point_bounce`, `telegraph_column`; `calibration_no_fire` is
  an operator utility, not a player attack. Cross-turret choreography belongs to Command Center, not a block interpreter on ESP.
- Status must make frame/pose, input/converted target, solved/clamped angles, offsets, reachability and aim errors observable.
  See [wire contract](mqtt-http-contract.md) and [operator usage](usage.md) for exact supported payloads/status.
- Before approved live-fire validation, calibrate each physical unit without firing at at least three known points and
  compare common-point alignment. No-fire calibration can move hardware and still needs actuation approval.

## Local safety and timing

- Establish relay OFF polarity before enabling outputs. Turret Fleet supports validated NVS-backed relay profiles and
  per-channel polarity; active-low is the factory default, not the only supported configuration. The sequence is CH2 → CH1
  → CH3, ESC fire hold, then CH3 → CH1 → CH2 shutdown. Preserve configured inter-step timing and safe-off on interruption/error; this is
  not the integrated Go2/Nixo two-relay profile. Do not copy one family's pins/polarity into another.
- Preserve calibrated ADC/neutral/PID parameters, hard feedback/rail guards and normal `motion.limits`. Do not treat dated
  raw readings or an unsafe bench session as a new hardware default. `kUnsafeManualCalibrationMode` is false in current source.
- Normal configured boot can perform its guarded no-fire local HOME; no automatic idle sweep or fire. OTA/brownout/fire-reset
  boots inhibit automatic HOME. Retain probe/envelope checks and protocol/status observability described in current usage.
  Upload/provision/reboot or serial reset can therefore cause motion without MQTT. Before those operations, require
  explicit approval covering boot motion or verified physical output isolation; no-fire does not mean no-motion.
- Fire is explicit and locally checked against configuration, inhibit/DEAD, lockout and active-operation rules. `hold`/`dead`
  interrupt patterns and stop fire. Do not infer direct fire waits for aim stability or a pre-arm flag.
- Reset during fire preserves recovery markers and does not resume saved fire/pose. Stable valid feedback may permit
  recovery only to safe `WAIT_COMMAND`; otherwise explicit `recover` remains required. Do not erase markers to bypass this.
- Keep local controller work bounded and ahead of network retry where required. Existing gated OTA can be synchronous in
  the MQTT callback; inspect scheduling/expiry effects rather than claiming every callback is nonblocking.
- MQTT delivery is not synchronized physical execution. Inspect current replay/retained-message/TTL handling; a proposed
  freshness or time-sync mechanism must not be presented as already enforced.

## OTA and evidence

Identity is `battlebang-turret-fleet` / `esp32dev-turret-v2`; polling uses `turret-fleet-latest/manifest.json`, while binary
URLs point to versioned artifacts. Keep app/hardware/build, size/hash, schema and product safe-state checks.
Two-stage update policy distinguishes availability from approval: when `command_center_controlled=true`, a polled build
must equal `desired_build`. Direct `/ota` is an approved job but does not bypass admission or authorize an agent to publish.
See [release flow](github-actions.md) for actual matrix/dispatch behavior; do not claim downloads or current remote health here.

Post-OTA boot intentionally stayed in `WAIT_COMMAND` in the historical operator observation; that record is not fresh
boot evidence. The current requirement is to preserve post-OTA automatic-HOME inhibition until an explicit command.

Use offline behavior/contracts and explicit target builds for application work. Static docs/skills need only references,
metadata and diff review. Deploy/provision/OTA/reboot and physical movement/fire/recovery are separately authorized stages.
Historical per-unit ADC/PWM/power observations remain in [context.md](context.md), explicitly historical and not defaults.
Completed execution plans are replaced by short compatibility records; current commands belong in the wire/usage guides.
