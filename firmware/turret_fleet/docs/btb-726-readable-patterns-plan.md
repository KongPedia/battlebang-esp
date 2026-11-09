# Readable pattern decision record (BTB-726)

This existing path is retained for contract readers. Pattern compilation/execution is implemented; it is not an open
branch task or authorization for a live demo. Exact current commands live in [mqtt-http-contract.md](mqtt-http-contract.md)
and [usage.md](usage.md); [architecture.md](architecture.md) owns cross-cutting safety and coordinate constraints.

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

- Per-turret MQTT command topic already exists as `battlebang/turrets/{turret_id}/command` via `buildTopics()` in `firmware/turret_fleet/mqtt/topics.cpp:21-27` and subscription in `firmware/turret_fleet/mqtt/topics.cpp:31-40`.
- `command: "pattern"` routes to `handlePatternCommand()` in `firmware/turret_fleet/control/turret_control.cpp`.
- Pattern parsing/normalization/compilation now lives in `firmware/turret_fleet/control/patterns/pattern_plan.{h,cpp}` as a pure plan compiler.
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
10. Application changes use relevant offline behavioral/contracts and explicit target builds; prose-only updates use static review. Physical execution is separately approved and evidenced.

## Limits and verification

Hardware execution remains in `TurretControl`; the pure compiler stays under `control/patterns/`. Keep minimum warning/
dwell timing, bounded fire duration and interrupt safe-off. MQTT phase offsets do not establish exact cross-unit time sync.
Retained/stale pattern replay and `ttl_ms` support must be checked in the current command path; old proposed mitigations
are not proof of implemented freshness guarantees. Software checks never authorize movement/fire or broker access.
Completed implementation steps, branch stop conditions and duplicate execution logs are removed; use Git/PR history.
