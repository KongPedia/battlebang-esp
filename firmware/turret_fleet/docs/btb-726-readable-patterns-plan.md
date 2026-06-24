# BTB-726 — Readable 3-Pattern Boss Turret MVP Plan

Date: 2026-05-29
Branch: `feature/BTB-726-readable-turret-patterns`
Jira: https://kongpedia.atlassian.net/browse/BTB-726
Related: BTB-721
Scope: `src/turret_fleet/**`, `tests/python/test_turret_fleet_contract.py`, turret fleet docs/examples

## Requirements Summary

Command Center sends high-level MQTT `pattern` commands to individual turret topics and the ESP executes only a small set of readable, deterministic attack patterns for a human-vs-turret demo.

The pattern catalog is intentionally reduced to three player-facing patterns:

1. `lane_sweep`
   - One turret, or a Command Center-orchestrated same-side vertical pair, sweeps between two visible world points.
   - Supports one-way or ping-pong motion; the default preset is one narrow ping-pong round trip.
   - Moves to the first endpoint, dwells as a visible warning, starts one `fire_ms`-capped fire window while yaw sweeps to the next endpoint, cuts early at yaw arrival, then waits the 500 ms endpoint dwell before the return leg.

2. `two_point_bounce`
   - One turret alternates between point A and point B.
   - Default behavior is two round trips: A fire -> B fire -> A fire -> B fire.
   - This maps directly to the requested “two coordinates, fire at one, move to next, fire, repeat twice” pattern.

3. `telegraph_column`
   - The same-side vertical pair, e.g. `boss_1f_left` + `boss_2f_left`, aims first, pauses as a visible warning, then fires in a simple top/bottom or bottom/top order.
   - Each ESP can store multiple candidate points and randomly choose one per command by default; `point_index` forces deterministic replay.
   - ESP still receives per-turret primitive commands; Command Center compiles the pair choreography.

Non-player-facing calibration remains allowed as an operator utility (`calibration_no_fire`) but is not part of the three attack patterns.

## Current Code Facts

- Per-turret MQTT command topic already exists as `battlebang/turrets/{turret_id}/command` via `buildTopics()` in `src/turret_fleet/mqtt/topics.cpp:21-27` and subscription in `src/turret_fleet/mqtt/topics.cpp:31-40`.
- `command: "pattern"` routes to `handlePatternCommand()` in `src/turret_fleet/control/turret_control.cpp`.
- Pattern parsing/normalization/compilation now lives in `src/turret_fleet/control/patterns/pattern_plan.{h,cpp}` as a pure plan compiler.
- `TurretControl` remains the hardware executor for motion, relay/ESC fire sequencing, interrupts, and status.
- Status fields include `pattern_id`, `pattern_instance_id`, `pattern_state`, current step, loop counters, and `pattern_last_error`.
- BTB-726 supersedes the earlier larger BTB-721 candidate list for player-facing attacks; `calibration_no_fire` remains operator-only.

## Design Decision

Keep ESP pattern execution primitive and deterministic. Do not send a full block-coding tree to ESP.

Command Center owns block-style composition:

```text
BOSS PATTERN: left column telegraph
  publish boss_1f_left pattern telegraph_column phase 0ms
  publish boss_2f_left pattern telegraph_column phase 350ms
```

ESP owns safe local execution of one primitive per received command:

```json
{
  "command": "pattern",
  "command_id": "boss-phase-1-0007",
  "pattern_id": "two_point_bounce",
  "pattern_instance_id": "boss-phase-1-left-a-b",
  "frame_id": "boss_stage_v1",
  "ttl_ms": 180000,
  "params": {
    "loop": 2,
    "move_timeout_ms": 60000,
    "dwell_ms": 1000,
    "fire_ms": 1000,
    "return_to": "wait_command",
    "points": [
      {"x": 0.0, "y": 0.75, "z": -0.6},
      {"x": 0.0, "y": -0.5, "z": -0.6}
    ]
  }
}
```

Command Center can also persist those coordinates as runtime config under
`patterns.presets` and then send compact high-level pattern commands without
`params.points`; explicit command points still override the preset for one run.

## Acceptance Criteria

1. Pattern allowlist accepts only:
   - `lane_sweep`
   - `two_point_bounce`
   - `telegraph_column`
   - optional operator-only `calibration_no_fire` if retained as no-fire calibration utility.
2. Wrong `frame_id` rejects before motion/fire.
3. Unconfigured, brownout/fire-reset lockout, and `DEAD` state reject pattern/fire before motion/fire.
4. `target` remains aim-only and never auto-fires.
5. Pattern fire happens only through explicit pattern fire steps using the same fire state machine as direct `fire`.
6. `hold` and `dead` interrupt active pattern and park fire outputs safe-off.
7. Status reports at least: `pattern_id`, `pattern_instance_id`, `pattern_state`, current step index/type, last pattern error/reason.
8. `two_point_bounce` default executes two A/B round trips when `loop` is omitted.
9. `telegraph_column` supports an aim-only warning dwell before fire so a player can visually read the attack.
10. Python contract tests, `py_compile`, PlatformIO build, and `git diff --check` pass before completion.

## Implementation Steps

### 1. Lock the reduced contract with tests first

Files:
- `tests/python/test_turret_fleet_contract.py`
- `src/turret_fleet/docs/implementation-plan.md` or a new focused docs file
- `src/turret_fleet/examples/pattern.*.json`

Test/contract additions:
- Assert the docs/examples define exactly the three player-facing patterns.
- Assert stale five-pattern language is removed or explicitly marked superseded by BTB-726.
- Assert example payloads use `command: pattern`, `frame_id`, `pattern_id`, `pattern_instance_id`, and bounded timing params.
- Assert `calibration_no_fire`, if present, is documented as operator-only and no-fire.

### 2. Add pattern model/state fields in `TurretControl`

Files:
- `src/turret_fleet/control/turret_control.h`
- `src/turret_fleet/control/turret_control.cpp`

Add a small internal state machine, avoiding new dependencies:

```cpp
enum PatternKind { PATTERN_NONE, PATTERN_LANE_SWEEP, PATTERN_TWO_POINT_BOUNCE, PATTERN_TELEGRAPH_COLUMN, PATTERN_CALIBRATION_NO_FIRE };
enum PatternStepType { STEP_MOVE, STEP_DWELL, STEP_FIRE, STEP_WAIT_FIRE_SAFE, STEP_DONE };
```

State fields should include:
- active kind/id/instance
- step index
- loop counters
- dwell/fire/move timeout timestamps
- current/next point
- no-fire flag for calibration

Keep it compact because `turret_control.cpp` is already large.

### 3. Replace first-point-only pattern handling with validation + compile

Files:
- `src/turret_fleet/control/turret_control.cpp:2418-2472`

`handlePatternCommand()` should:
- validate configured/frame/DEAD/fire lockout as it does now
- validate allowlisted `pattern_id`
- normalize params with defaults and min/max bounds
- merge `patterns.presets[pattern_id]` from runtime config before applying
  per-command param overrides
- require exactly two points for `lane_sweep` and `two_point_bounce`
- require at least one and up to the bounded preset point maximum for `telegraph_column`
- compile into a small sequence of move/dwell/fire-move/fire/wait steps
- start the pattern by applying the first move target

### 4. Drive the pattern from the main control loop

Files:
- `src/turret_fleet/control/turret_control.cpp`

Add `updatePattern()` and call it from `TurretControl::loop()` after current angle/PID/fire updates are in a safe order.

Rules:
- MOVE step completes when `aimReached()` or move timeout occurs.
- DWELL step is a visible warning/settle pause.
- FIRE step calls the existing `startFireSequence()` with normalized hold ms.
- WAIT_FIRE_SAFE waits until the fire sequence returns safe before next movement unless the direct fire sequence already guarantees safe handoff.
- DONE returns to `WAIT_COMMAND` or configured `return_to`.

### 5. Make interruption semantics explicit

Files:
- `src/turret_fleet/control/turret_control.cpp:2518-2559`

- `hold` clears active pattern state and safe-offs fire.
- `dead` clears active pattern state and safe-offs fire.
- new `pattern` replaces an active pattern only after current fire outputs are safe; if firing, reject or mark pending replacement. MVP recommendation: reject while firing with clear `lastError_`.
- direct `target` during active pattern should reject unless payload has `interrupt: true`.

### 6. Status/docs/examples update

Files:
- `src/turret_fleet/docs/usage.md`
- `src/turret_fleet/docs/mqtt-http-contract.md`
- `src/turret_fleet/docs/implementation-plan.md`
- `src/turret_fleet/examples/pattern.lane_sweep.json`
- `src/turret_fleet/examples/pattern.two_point_bounce.json`
- `src/turret_fleet/examples/pattern.telegraph_column.json`
- `src/turret_fleet/pattern_presets/turret_2.json`

Document:
- 4-turret layout assumptions: 1F/2F, left/right, about 1m horizontal spacing, forward-facing.
- 3 pattern IDs and sample MQTT payloads.
- Runtime `patterns.presets` config JSON for per-turret coordinates/timings.
- Command Center compiles group/choreography into per-turret primitive commands.
- Pattern readability guideline: always include dwell/telegraph; avoid random, unreadable firing.

## Verification Steps

Run, in order:

```bash
.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py -q
python3 -m py_compile scripts/turret_fleet/*.py tests/python/test_turret_fleet_contract.py
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
git diff --check
```

If hardware is available before claiming live readiness:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet -t upload --upload-port /dev/cu.usbserial-120
# then verify serial/MQTT status for pattern start, step progress, fire safe-off, hold/dead interrupt
```

## Risks and Mitigations

- Risk: `turret_control.cpp` is already large and pattern logic may increase coupling.
  - Mitigation: keep hardware execution in `TurretControl` and extract the pure
    pattern compiler under `control/patterns/`.
- Risk: four-turret sync over MQTT is not exact.
  - Mitigation: MVP relies on simple `phase_offset_ms`; defer wall-clock `start_at_ms` until time sync is available.
- Risk: pattern timing can become unreadable or unsafe.
  - Mitigation: enforce minimum dwell/default telegraph timings and clamp `fire_ms` through existing fire config bounds.
- Risk: stale retained MQTT pattern could replay.
  - Mitigation: document non-retained command topics and implement/validate `ttl_ms` if current command path has enough timestamp context; otherwise leave as Command Center publish rule for MVP.

## Stop Condition

Stop after the reduced contract is documented, implementation plan is attached to the branch, tests/build pass, and the branch is ready for implementation work under BTB-726.
