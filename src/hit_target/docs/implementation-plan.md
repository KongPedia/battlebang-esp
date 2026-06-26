# Hit Target implementation status and runtime plan

## Scope

`src/hit_target/` is the generic standalone BattleBang hit-target firmware: ESP32 + piezo sensor + circular LED ring. It is separate from Go2-mounted firmware and turret firmware because this device owns its local HP/effect loop while still exposing remote config/status/OTA hooks.

## Implemented requirements

- MAC-derived identity: `target_id` defaults to `hit_target_<ESP32 eFuse MAC>`; numbered target profiles are not used.
- Local UX loop:
  - green ready circle;
  - phase HP: default green 5 hits → yellow/orange 5 hits → red 5 hits;
  - dim next-phase backfill behind missing phase HP with black boundary gaps;
  - phase-transition reveal so hit 5/10 grows the next phase behind the shrinking removed chunk;
  - compact sequential red/orange damage chip, lockout, and no orbit/hit-flash/remaining-HP pulse;
  - final HP chunk wipe, short HP=0 blackout beat, rainbow defeat sweep, then blackout.
- Runtime activation:
  - `always_on` keeps standalone/Go2-style targets damageable whenever enabled;
  - `linked_device` subscribes to a linked device status topic, lights the ring and accepts piezo hits only while that device is active; kind `turret` maps to `{root}/turrets/{linked_device_id}/status`, while other kinds use `{root}/devices/{linked_device_kind}/{linked_device_id}/status`.
- Runtime config model:
  - factory defaults still come from `src/hit_target/config.json` via `scripts/hit_target_config.py`;
  - after provisioning, NVS namespace `bb_hit_target` is the source of truth;
  - `show-config`, `config {json}`, `provision {json}`, and `clear-config` allow USB serial provisioning without rebuilding.
- MQTT model:
  - device topics: `{root}/devices/hit_target/{device_id}/status|config|ota`;
  - hit-target topics: `{root}/hit_targets/{target_id}/status|config|command|ota`;
  - fleet OTA topic: `{root}/hit_targets/all/ota`.
- OTA model:
  - app: `battlebang-hit-target`;
  - hardware: `esp32dev-hit-target-ring-v1`;
  - default latest manifest asset: `hit-target-manifest.json`, intentionally separate from turret fleet `manifest.json`;
  - SHA-256 and app/hardware/build gates are enforced before update.
- PlatformIO env `esp32dev_hit_target` builds only `src/hit_target/**` and uses `min_spiffs.csv` OTA-capable partitioning.
- GitHub Actions workflow `.github/workflows/hit-target-firmware.yml` builds release firmware and manifest artifacts.

## Runtime config rules

- Gameplay/activation/sensor values are runtime-configurable:
  - `hp.phase_count`, `hp.hits_per_phase`, `hp.palette`;
  - `visual.damage_chip_ms`, `visual.defeat_*`;
  - `activation.mode`, `activation.linked_device_kind`, `activation.linked_device_id`, `activation.stale_ms`;
  - `sensor.hit_threshold`, `sensor.digital_hit_min_edges`, `sensor.digital_isr_debounce_us`, cooldown/rearm/capture timings;
  - Wi-Fi, MQTT, and OTA policy.
- Gameplay or sensor-pin config changes reset the local HP state to full. This avoids ambiguous mid-game remapping when changing 15 hits to 30/50 hits.
- `led.pin`, `led.type`, and `led.color_order` remain hardware-profile build values because FastLED binds them through compile-time templates. Runtime config can tune brightness, power, and active LED count up to the compiled capacity.
- Secrets are persisted in NVS but masked in `show-config` and status by default.
- `config_version` must be monotonic; stale patches are rejected.

## Verification performed for current implementation

- Python static/contract checks:
  - `python3 -m py_compile scripts/hit_target_config.py scripts/firmware/make_release_manifest.py tests/python/test_hit_target_contract.py`
  - `.venv-turret-tests/bin/python -m pytest tests/python/test_hit_target_contract.py -q`
- Firmware build:
  - `./.venv-pio/bin/pio run -e esp32dev_hit_target`
- Hardware upload:
  - `./.venv-pio/bin/pio run -e esp32dev_hit_target -t upload --upload-port /dev/cu.usbserial-120`
- Serial smoke tests:
  - boot/status shows `battlebang-hit-target`, app/hardware metadata, and MAC-derived `target_id`;
  - `config` patch changed `hits_per_phase` to 10 and reported `total_hits=30` without rebuild;
  - `clear-config` restored factory `hits_per_phase=5` / `total_hits=15`;
  - `h` simulated hit decremented HP to 14;
  - `r` reset restored HP to 15.

## Remaining follow-ups

- MQTT end-to-end needs a real broker/Wi-Fi provisioning test.
- OTA end-to-end needs a published `hit-target-manifest.json` release artifact.
- HTTPS currently follows the repo's turret_fleet prototype behavior (`setInsecure()`); production should pin a CA or use signed manifests/images.
- Physical piezo sensitivity still depends on comparator potentiometer/wiring; use `digital_hit_min_edges`, `digital_isr_debounce_us`, and optional AO thresholds with bench data.
