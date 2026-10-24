# GitHub Actions Release Flow

This is a publication-capable operator procedure, not a document-verification checklist. Do not dispatch, publish,
move stable pointers or send MQTT jobs without explicit scope. Upload/NVS/OTA/reboot and subsequent motion/fire/recovery
are separate approvals. No current remote release or device readiness is asserted by this page.

## Goal

Build one generic `esp32dev_turret_fleet` firmware artifact. Do not build one
binary per turret. Per-turret values are runtime config delivered over USB serial
first provisioning and later MQTT/NVS config.

## Triggers

The unified `Firmware OTA Releases` workflow (`.github/workflows/firmware-ota.yml`) supports both normal
merge-to-main releases and explicitly requested manual builds. Dispatch selects the full matrix, not fleet alone.

### Automatic merge-to-main release

The workflow selects the fleet row for relevant `main` changes. Current fleet/global selection includes:

```yaml
.github/workflows/firmware-ota.yml
platformio.ini
firmware/turret_fleet/**
lib/bb_esp_core/**
lib/bb_esp_net/**
lib/bb_esp_nvs/**
lib/bb_esp_ota/**
scripts/firmware/make_release_manifest.py
```

Host-only `scripts/turret_fleet/**` helpers are not fleet release inputs. Firmware-folder Markdown is included by the
current filter, so a prose-only merge can still publish images. See the committed matrix for all families; do not change
release policy silently while editing documentation.

For push builds the workflow derives release identity from the GitHub run number:

```text
version = 0.2.${GITHUB_RUN_NUMBER}-main
build   = 1000 + ${GITHUB_RUN_NUMBER}
tag     = turret-fleet-v0.2.${GITHUB_RUN_NUMBER}-main
```

The build number is what the ESP compares during OTA. It must be greater than the
currently running `firmware_build`; the main push build is offset by 1000 so it
stays above older manual/PR smoke builds.

### Manual workflow dispatch

Manual runs are external actions requiring authorization. For an authorized build-only smoke run, explicitly disable
publication; the workflow's default `create_release` is true, so omitting that input is not harmless:

```bash
gh workflow run firmware-ota.yml \
  --repo KongPedia/battlebang-esp \
  --ref "<reviewed-ref>" \
  -f version="<reviewed-version>" \
  -f build="<reviewed-build>" \
  -f firmware_base_url="" \
  -f public_release_repo=KongPedia/battlebang-esp \
  -f public_release_target_branch="<reviewed-ref>" \
  -f create_release=false \
  -f update_stable_latest=false
```

Build-only smoke runs keep both `create_release=false` and `update_stable_latest=false`. Publishing versioned test
assets requires separate authorization and `create_release=true`; keep stable updates off. Moving a stable pointer is a
further intentional rollout decision, never a side effect of document validation. Main push builds update stable tags.

For same-repo releases to `KongPedia/battlebang-esp`, no extra secret is needed;
the workflow uses the built-in `GITHUB_TOKEN`. `PUBLIC_RELEASE_REPO_TOKEN` is
only needed when `public_release_repo` points to a different repository.

## GitHub Release as the firmware host

If `create_release=true` and `firmware_base_url` is empty, the workflow publishes
versioned release assets to `KongPedia/battlebang-esp` and writes the `.bin` URL
in `manifest.json` as a public GitHub Release asset URL:

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
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json
```

## Post-merge OTA operator flow

1. Merge the PR to `main`.
2. Wait for the **Firmware OTA Releases** Action run to finish and create a
   release.
3. Set the MQTT broker host locally. This is the Command Center/MQTT broker IP
   or DNS name, not the ESP device IP:

   ```bash
   export MQTT_BROKER_HOST=COMMAND_CENTER_IP_OR_DNS
   ```

4. Read the latest manifest/build:

   ```bash
   curl -L https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json
   ```

5. Approve that exact build for a turret. This publishes an MQTT config patch to
   `battlebang/turrets/{turret_id}/config`; the ESP then polls the latest
   manifest itself:

   ```bash
   ./bin/turret fleet-mqtt turret_2 update --desired-build <LATEST_BUILD> --host "$MQTT_BROKER_HOST"
   ```

   Or read the latest build and approve it without typing the number:

   ```bash
   BUILD=$(curl -fsSL https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json | python3 -c 'import sys,json; print(json.load(sys.stdin)["build"])')
   ./bin/turret fleet-mqtt turret_2 update --desired-build "$BUILD" --host "$MQTT_BROKER_HOST"
   ```

6. Watch `battlebang/turrets/turret_2/status` for `config_applied`,
   `ota_downloading`, `ota_rebooting`, then `connected` with the new
   `firmware_build`.
7. After OTA reboot, automatic HOME is intentionally inhibited. Do not move the device just to prove an update.
   Only after separate movement approval, use the reviewed device's explicit command:

   ```bash
   ./bin/turret fleet-mqtt turret_2 initiate --host "$MQTT_BROKER_HOST"
   # or a target command
   ./bin/turret fleet-mqtt turret_2 target 0 -2 0.6 --host "$MQTT_BROKER_HOST"
   ```

8. Disable polling when the rollout is done unless continuous polling is desired:

   ```bash
   ./bin/turret fleet-mqtt turret_2 config --host "$MQTT_BROKER_HOST" \
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
    "public_manifest_url": "https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json",
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
`<PRIVATE_LAN_MQTT_BROKER>`. Let Command Center/operator approve via MQTT from the LAN, or use a
self-hosted runner on Jetson if direct publish is required.

## Security notes

- Confirm approved artifact visibility/reachability for the target device before deployment; this page is not a fresh remote availability check.
- Do not put Wi-Fi/MQTT/GitHub secrets into the firmware image.
- GitHub asset downloads may redirect; the firmware follows redirects.
- The current prototype uses insecure TLS for convenience. Use CA pinning or
  signed manifests before production.
