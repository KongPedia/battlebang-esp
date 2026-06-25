# BattleBang src/ firmware guidance

## Scope

Applies to all firmware folders under `src/**` unless a deeper `AGENTS.md` overrides it.

## Folder ownership

- Treat each production folder as an independently operated firmware surface: source, README, local env example, provisioning helper, and PlatformIO env should stay aligned.
- Keep cross-firmware sharing explicit through `platformio.ini`, documented helper scripts, or common scripts under `scripts/`; do not silently copy private config between firmware folders in tracked files.
- For generic devices, prefer ESP32-derived identity (eFuse MAC) plus runtime metadata (`group`, `location`) over numbered build profiles unless the folder README says otherwise.

## Secrets and local runtime config

- Never commit real Wi-Fi passwords, MQTT passwords, Command Center IPs, local serial ports, or generated secret headers.
- Real local env files are ignored. Use examples only in git:
  - `src/hit_target/.env.hit_target.example` -> local `src/hit_target/.env.hit_target`
  - `firmware/boss_target/.env.boss_target.example` -> local `firmware/boss_target/.env.boss_target`
  - `firmware/turret_fleet/.env.turret_fleet.example` -> local `firmware/turret_fleet/.env.turret_fleet`
  - `src/nIxo/local_secrets.example.h` -> local `src/nIxo/local_secrets.h`
- If copying values between ignored env files for bench setup, do it without printing secrets in logs and do not stage the real env file.

## CI and OTA

- The unified firmware OTA workflow should be path-filtered. Unrelated `src/<other-firmware>/**` or `firmware/<other-firmware>/**` changes must not trigger another firmware's build.
- `platformio.ini` is a shared build/dependency index, so workflow path filters may include it deliberately.
- Do not use GitHub's repo-wide `/releases/latest/download/...` URL for device polling when multiple firmware families share one release repo. Use firmware-specific stable tags instead:
  - hit target: `/releases/download/hit-target-latest/hit-target-manifest.json`
  - boss target: `/releases/download/boss-target-latest/boss-target-manifest.json`
  - turret fleet: `/releases/download/turret-fleet-latest/manifest.json`
- Versioned release tags (`hit-target-v{version}`, `boss-target-v{version}`, `turret-fleet-v{version}`) remain immutable audit points. Stable tags are for polling manifests and should not overwrite another firmware family.

## Verification

- Run the matching PlatformIO env for firmware changes.
- Run matching Python contract tests when docs/scripts/workflows/config contracts change.
- For hardware behavior changes, upload to the correct ESP32 and capture serial/MQTT evidence before claiming end-to-end success.
