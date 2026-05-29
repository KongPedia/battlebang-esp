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
- Merge-to-main GitHub Actions releases update `https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json`.
- Direct MQTT `/ota` manifests are Command Center-approved immediate jobs.
- Automatic polling is disabled by default; `./bin/turret fleet-mqtt <turret_id> update --desired-build N` enables polling against the stable latest URL and approves exactly build `N`. With `command_center_controlled=true`, the manifest build must exactly match `ota.desired_build`.
- OTA applies only in safe state when `ota.apply_only_in_safe_state=true`.

Post-OTA behavior:

- Successful OTA writes a reboot marker before `ESP.restart()`.
- On the next boot, automatic HOME drive is inhibited even if normal power-on would HOME.
- Command Center should confirm the new `firmware_build` on status, then send
  `initiate`/`home` or a fresh `target` command.

Polling semantics:

- `ota.auto_check_enabled=false` means the ESP does not discover updates itself.
- Command Center approval for polling is the config tuple
  `auto_check_enabled=true`, `command_center_controlled=true`,
  `desired_build=<manifest build>`, and the stable latest manifest URL. The CLI
  shorthand is `./bin/turret fleet-mqtt turret_2 update --desired-build N`.
- Direct MQTT `/ota` manifest publishing is an immediate Command Center-approved
  OTA job, still gated by manifest validation and safe-state checks.
