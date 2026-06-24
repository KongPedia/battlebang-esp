# Turret fleet pattern presets

Runtime-editable pattern preset JSON lives here so command payload examples stay
separate from stored turret configuration. `turret_1.json` through
`turret_4.json` currently share the same readable boss-attack presets; each
ESP solves the shared world points from its own pose in
`src/turret_fleet/profiles/<turret>.json`.

Apply a preset file over MQTT:

```bash
TURRET_FLEET_ENV_FILE=src/turret_fleet/.env.turret_fleet \
  ./bin/turret fleet-mqtt turret_2 config \
  --patterns-file src/turret_fleet/pattern_presets/turret_2.json
```

Apply both the turret runtime config and pattern presets over MQTT so the ESP
saves them to NVS:

```bash
TURRET_FLEET_ENV_FILE=src/turret_fleet/.env.turret_fleet \
  ./bin/turret fleet-mqtt turret_2 config \
  --profile-file src/turret_fleet/profiles/turret_2.json \
  --patterns-file src/turret_fleet/pattern_presets/turret_2.json
```

The presets use stage coordinates in meters with left/right on the `y` axis and
negative `z` targets so the turret pitches down. Player-facing point patterns
move first, wait for `dwell_ms`, then fire for `fire_ms`; `lane_sweep` now uses
one readable narrow ping-pong round trip by default. Each sweep leg starts a
fire window capped by `fire_ms`; yaw endpoint arrival cuts it early, otherwise
it is not refreshed past that cap. The pattern then waits safe-off and pauses
`dwell_ms` (500 ms in the default preset) before the return leg starts.

`two_point_bounce` uses a longer move timeout because the safe single-axis motion
scheduler needs more time to move between the requested high/low points before
firing.

`telegraph_column` stores multiple candidate points and defaults to
`random: true`, so each command selects one readable point, dwells, then fires.
For deterministic replay send `params.point_index` (or CLI `--point-index`); set
`random: false` to visit every configured point in order.
