# Implementation Plan

## Phase 0 - Keep current test path stable

- Do not delete, move, or rewrite `src/turret/`.
- Keep current hardcoded/test firmware available until bench tests are complete.

## Phase 1 - Generic firmware skeleton

- Add `src/turret_fleet/` with separated modules.
- Add `esp32dev_turret_fleet` PlatformIO env.
- Verify it builds independently.

Acceptance:

- `pio run -e esp32dev_turret_fleet` succeeds.
- `pio run -e esp32dev_turret_5` still succeeds.

## Phase 2 - Runtime config

- Use NVS/Preferences for `RuntimeConfig`.
- Add Serial commands: `config`, `show-config`, `clear-config`.
- Add MQTT config topic handling.

Acceptance:

- Device boots unconfigured.
- Serial config persists after reset.
- MQTT config with higher `config_version` persists and applies.

## Phase 3 - Status and observability

- Publish heartbeat to device and turret status topics.
- Include firmware version/build, config version, IP, RSSI, mode, uptime.

Acceptance:

- Command center can list all online devices.
- Unconfigured devices still publish by `device_id`.

## Phase 4 - GitHub release artifacts

- Build generic firmware in GitHub Actions.
- Generate manifest with sha256 and size.
- Upload artifact on every push/PR.
- Create GitHub Release on `turret-fleet-v*` tags.

Acceptance:

- A tag creates release assets.
- Manifest references app/hardware/version/build/sha256/size.

## Phase 5 - OTA rollout

- Command center publishes manifest over MQTT.
- ESP downloads firmware over HTTP and verifies SHA-256.
- ESP reboots into new version.

Acceptance:

- ESP rejects old/wrong app/wrong hardware manifests.
- ESP reports status before and after OTA.

## Phase 6 - Port real turret control

- Move proven math/control/fire logic from `src/turret/runtime/*.inc` into
  `src/turret_fleet/control/` modules.
- Keep physical fire command explicit.

Acceptance:

- Existing MQTT command contract remains compatible.
- Bench tests match current `src/turret` behavior before switching paths.
