# Hit Target initial implementation plan

## Requirements summary

- Add a monorepo-style firmware target under `src/` for a generic wall/circular hit target device: ESP32 + piezo sensor + circular LED ring.
- Keep it independent from `src/go2_nixo/` because the standalone target owns local HP/effects instead of using Go2 MQTT authority.
- Implement the updated visual model: green ready circle, then large green/yellow/red HP phases (5 accepted hits per phase by default), faster red/orange sequential recent-damage chip, a 360° neutral-white orbit overlay, and a final HP=0 rainbow defeat sweep.
- Migrate the wall-mounted target sketch hardware values while keeping pins, LED count, LED type, and color order tunable without source edits.
- Derive each device identity from the ESP32 eFuse MAC instead of numbered target profiles.
- Support PlatformIO build/upload as a separate env.

## Acceptance criteria

- `pio run -e esp32dev_hit_target` builds only `src/hit_target/**`, with no `src/Wall_Target/**` path kept.
- Boot/reset renders a full green circle.
- A hit keeps remaining HP as the current phase color, decrements HP immediately, triggers only a very short full-white confirmation flash, starts lockout, and emits a serial JSON event.
- The removed HP segment remains visible as a red/orange recent-damage chip, then wipes away LED-by-LED from the old HP edge quickly enough that HP loss feels immediate.
- During phase 1/2, removed rear HP can show a dim next-phase color with black boundary gaps so the future warning color is readable without confusing it with current HP.
- On a phase-transition hit, the existing dim next-phase backfill must remain visible while the final current-color segment wipes away; the next phase color must also grow behind that shrinking segment in proportion to the wipe, then the next full/current ring appears after the wipe completes.
- During lockout, additional hits are ignored while the orbit animation continues and remaining phase-colored HP stays readable and briefly pulses after the hit flash.
- The neutral-white orbit overlay continues over both lit and dark HP segments until HP reaches zero without showing green in dark gaps.
- The neutral-white orbit also runs during ready-green state so the ring advertises itself as a target before any hit.
- HP count is phase-based: `max_hits` must equal `hp_phase_count * hits_per_phase`; default is 3 phases × 5 hits = 15.
- DO-only piezo hit acceptance requires `digital_hit_min_edges` edges inside the capture window and uses `digital_isr_debounce_us` to tune how much fast piezo ringing is counted. AO threshold capture remains available when AO is wired.
- At HP zero, the final HP chunk must still run the normal damage-chip wipe first; then a short all-off beat confirms HP=0, then a rainbow defeat sweep plays once and all LEDs turn off.
- `r`/`h`/`s` serial commands work for bench debugging.
- ESP32 BOOT/GPIO0 long-press initializes/resets the target after firmware boot.
- Serial JSON includes a MAC-derived `target_id` and `device_mac` for controller discovery.

## Implementation steps

1. Create `src/hit_target/` with `main.cpp`, `build_config.h`, `config.json`, and README.
2. Add `scripts/hit_target_config.py` to inject default/env hardware overrides without target numbering.
3. Add PlatformIO env `esp32dev_hit_target` with a narrow `build_src_filter`.
4. Update root README env table and build instructions.
5. Verify with PlatformIO build and diff checks.

## Maintainability plan

- Keep the firmware as one Arduino entrypoint for now, but classify it by local responsibilities: identity, target HP state, capture/sensor re-arm, effect timers/rendering, serial bench commands.
- Keep tunable values in `config.json` / `build_config.h`; do not scatter pin numbers, HP counts, timing values, or LED order directly in effect logic.
- Keep PlatformIO injection in one option table so adding phase HP, LED wiring, or timing knobs does not require editing separate default/env maps.
- Avoid splitting into many files until a second hit-target form factor appears; that keeps the first firmware easy to upload/debug from PlatformIO while still leaving clear extraction seams.
- Contract tests should guard naming, MAC identity, old-path removal, default wiring, ready-green + phase HP behavior, recent-damage chip, neutral orbit, BOOT-button reset, and lockout semantics before visual tuning changes.

## Known hardware follow-ups

- Default LED data GPIO is `GPIO18`, with 60 WS2812B LEDs and GRB color order, migrated from the wall-mounted target sketch; confirm visually on the actual ring.
- HP step count is gameplay-tunable by changing `hp_phase_count` and/or `hits_per_phase`; keep `max_hits` equal to their product.
- Hit readability is tuned with `hit_flash_ms` for the short confirmation flash and `damage_chip_ms` for the fast sequential red/orange removed-HP segment plus a short remaining-HP pulse. Final defeat is tuned separately with `defeat_blackout_ms`, `defeat_rainbow_ms`, and `defeat_rainbow_spins` so HP readability does not fight the reward animation.
- DO-only noise/sensitivity is tuned with `digital_hit_min_edges` and `digital_isr_debounce_us`; default `2` edges filters single-edge idle chatter, while `5000us` debounce catches faster piezo ringing than the old 20ms behavior.
- BOOT/GPIO0 long-press reset is enabled by default; do not hold BOOT while resetting/power-cycling because it is also the ESP32 download-mode strap pin.
- Confirm whether the piezo board has AO wired to an ADC1 pin; if not, keep `BATTLEBANG_HIT_TARGET_PIEZO_AO_PIN=-1`.
- Tune `hit_threshold` and `hit_rearm_threshold` from serial heartbeat/status values after physical tap tests.
