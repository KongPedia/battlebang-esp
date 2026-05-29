# GitHub Actions Release Flow

## Goal

Build one generic `esp32dev_turret_fleet` firmware artifact. Do not build one
binary per turret. Per-turret values are runtime config delivered over USB serial
first provisioning and later MQTT/NVS config.

## Triggers

The workflow `.github/workflows/turret-fleet-firmware.yml` supports both normal
merge-to-main releases and manual smoke-test builds.

### Automatic merge-to-main release

A push to `main` automatically runs the workflow when one of these paths changes:

```yaml
.github/workflows/turret-fleet-firmware.yml
platformio.ini
src/turret_fleet/**
scripts/turret_fleet/**
```

For push builds the workflow derives release identity from the GitHub run number:

```text
version = 0.1.${GITHUB_RUN_NUMBER}-main
build   = ${GITHUB_RUN_NUMBER}
tag     = turret-fleet-v0.1.${GITHUB_RUN_NUMBER}-main
```

The build number is what the ESP compares during OTA. It must be greater than the
currently running `firmware_build`; current workflow run numbers are already above
the local build-5 smoke-test firmware.

### Manual workflow dispatch

Manual runs remain available for PR smoke tests or one-off builds:

```bash
gh workflow run turret-fleet-firmware.yml \
  --repo KongPedia/battlebang-esp \
  --ref feature/BTB-721-turret-fleet-rebuild-plan \
  -f version=0.1.4-pr9-bootfix \
  -f build=5 \
  -f firmware_base_url="" \
  -f public_release_repo=KongPedia/battlebang-esp \
  -f public_release_target_branch=feature/BTB-721-turret-fleet-rebuild-plan \
  -f create_release=true
```

For same-repo releases to `KongPedia/battlebang-esp`, no extra secret is needed;
the workflow uses the built-in `GITHUB_TOKEN`. `PUBLIC_RELEASE_REPO_TOKEN` is
only needed when `public_release_repo` points to a different repository.

## GitHub Release as the firmware host

If `create_release=true` and `firmware_base_url` is empty, the workflow publishes
release assets to `KongPedia/battlebang-esp` and writes the `.bin` URL in
`manifest.json` as a public GitHub Release asset URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v{version}/battlebang-turret-fleet-{version}.bin
```

Each release contains:

```text
manifest.json
battlebang-turret-fleet-{version}.bin
sha256.txt
```

The ESP's default public polling URL is stable and does not require operators to
type a release-specific manifest URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

## Post-merge OTA operator flow

1. Merge the PR to `main`.
2. Wait for the **Turret Fleet Firmware** Action run to finish and create a
   release.
3. Read the latest manifest/build:

   ```bash
   curl -L https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
   ```

4. Approve that exact build for a turret. This publishes an MQTT config patch to
   `battlebang/turrets/{turret_id}/config`; the ESP then polls the latest
   manifest itself:

   ```bash
   ./bin/turret fleet-mqtt turret_2 update --desired-build <LATEST_BUILD> --host 10.2.80.52
   ```

5. Watch `battlebang/turrets/turret_2/status` for `config_applied`,
   `ota_downloading`, `ota_rebooting`, then `connected` with the new
   `firmware_build`.
6. After OTA reboot, automatic HOME is intentionally inhibited. Send an explicit
   command:

   ```bash
   ./bin/turret fleet-mqtt turret_2 initiate --host 10.2.80.52
   # or a target command
   ./bin/turret fleet-mqtt turret_2 target 0 -2 0.6 --host 10.2.80.52
   ```

7. Disable polling when the rollout is done unless continuous polling is desired:

   ```bash
   ./bin/turret fleet-mqtt turret_2 config --host 10.2.80.52 \
     --config-version $(date +%s) \
     --ota-auto-check-enabled false \
     --ota-desired-build <LATEST_BUILD> \
     --ota-apply-only-in-safe-state true
   ```

## Command Center-approved polling semantics

Runtime config keeps automatic polling disabled by default:

```json
{"ota":{"command_center_controlled":true,"auto_check_enabled":false,"desired_build":0}}
```

`fleet-mqtt ... update --desired-build N` sends this NVS config patch:

```json
{
  "type": "config",
  "schema": 2,
  "ota": {
    "command_center_controlled": true,
    "auto_check_enabled": true,
    "desired_build": N,
    "public_manifest_url": "https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json",
    "check_interval_s": 30,
    "apply_only_in_safe_state": true
  }
}
```

With `command_center_controlled=true`, the ESP applies a polled manifest only if
`manifest.build == ota.desired_build`, app/hardware/build/hash checks pass, and
the turret is in a safe OTA state.

Direct `/ota` remains supported, but it requires publishing a complete manifest
body to `battlebang/turrets/{turret_id}/ota` or `battlebang/turrets/all/ota`.
Use direct `/ota` only when Command Center already has the manifest body and wants
an immediate job. Use `update --desired-build N` for the normal post-merge flow.

## Why not GitHub runner direct-to-MQTT?

A GitHub-hosted runner usually cannot reach a private LAN broker such as
`10.2.80.x`. Let Command Center/operator approve via MQTT from the LAN, or use a
self-hosted runner on Jetson if direct publish is required.

## Security notes

- `KongPedia/battlebang-esp` is public, so release assets are public and
  reachable by ESP32 HTTPS downloads.
- Do not put Wi-Fi/MQTT/GitHub secrets into the firmware image.
- GitHub asset downloads may redirect; the firmware follows redirects.
- The current prototype uses insecure TLS for convenience. Use CA pinning or
  signed manifests before production.
