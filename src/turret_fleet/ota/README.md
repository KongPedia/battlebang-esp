# `ota/`

OTA manifest validation and HTTP/HTTPS firmware download/apply.

Manifest requirements:

- `type: firmware`
- `app: battlebang-turret-fleet`
- `hardware: esp32dev-turret-v2`
- increasing numeric `build` unless `force=true` on a direct OTA job
- firmware `url`
- `sha256` and `size`

Rollout model:

- GitHub Actions publishes `manifest.json` and firmware `.bin` as public release assets.
- Direct MQTT `/ota` manifests are Command Center-approved jobs.
- Automatic polling is disabled by default; when enabled with `command_center_controlled=true`, the manifest build must exactly match `ota.desired_build`.
- OTA applies only in safe state when `ota.apply_only_in_safe_state=true`.
