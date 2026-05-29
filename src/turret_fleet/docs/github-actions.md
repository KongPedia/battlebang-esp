# GitHub Actions Release Flow

## Goal

Build one generic firmware artifact from tags. Do not build one binary per turret.
Per-turret values are runtime config and should be delivered over Serial/MQTT.

## Current test mode: manual workflow only

The workflow is intentionally configured for manual runs only while we validate
the artifact/manifest flow:

```yaml
on:
  workflow_dispatch:
```

Run it in GitHub:

1. Open the repository on GitHub.
2. Go to **Actions**.
3. Select **Turret Fleet Firmware**.
4. Click **Run workflow**.
5. Fill:
   - `version`: example `0.1.0-manual`
   - `build`: numeric build, example `2` for the first OTA test because local dev firmware defaults to build `1`
   - `firmware_base_url`: optional local HTTP base URL reachable by ESP32
   - `public_release_repo`: default `KongPedia/battlebang-esp`
   - `public_release_target_branch`: default `main`
   - `create_release`: `true` by default to publish release assets to this public repo; set `false` for artifact-only testing

For same-repo releases to `KongPedia/battlebang-esp`, no extra secret is needed; the workflow uses the built-in `GITHUB_TOKEN`. `PUBLIC_RELEASE_REPO_TOKEN` is only needed when `public_release_repo` points to a different repository.

GitHub push/tag triggers can be added later after the manual artifact test is
working end-to-end.

## GitHub Release as the firmware host

If `create_release=true` and `firmware_base_url` is empty, the workflow publishes
to `KongPedia/battlebang-esp` and writes the `.bin` URL in `manifest.json`
as a public GitHub Release asset URL:

```text
https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v{version}/battlebang-turret-fleet-{version}.bin
```

That means a separate `:8080` firmware server is not required for the first OTA
smoke test, as long as the ESP32 can reach GitHub over HTTPS. The release will
contain both:

```text
manifest.json
battlebang-turret-fleet-{version}.bin
sha256.txt
```

Then the ESP can fetch the manifest directly:

```text
check-ota https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-v{version}/manifest.json
```

Or use the default latest manifest baked into the `turret_fleet` firmware:

```text
check-ota
check-latest
```

Or the same `manifest.json` can be sent over MQTT to
`battlebang/turrets/all/ota`.

Important limitations:

- `KongPedia/battlebang-esp` is public, so release assets are public and reachable by ESP32 HTTPS downloads.
- Do not put Wi-Fi/MQTT/GitHub secrets into the firmware image.
- GitHub asset downloads may redirect; the prototype firmware follows redirects.
- The current prototype uses insecure TLS for convenience. Use CA pinning or
  signed manifests before production.

## Future tag convention

```bash
git tag turret-fleet-v0.2.0
git push origin turret-fleet-v0.2.0
```

## Output artifacts

The workflow writes:

```text
dist/battlebang-turret-fleet-{version}.bin
dist/manifest.json
dist/sha256.txt
```

`manifest.json` can be used directly from GitHub Releases, or Command Center can mirror/rewrite the firmware URL to a local HTTP server before publishing an OTA job.

## Local command-center flow

```text
1. Watch GitHub releases or receive webhook.
2. Download firmware.bin + manifest.json.
3. Either keep the GitHub Release firmware URL or mirror firmware.bin from Jetson/Command Center HTTP server.
4. Set manifest.url to local HTTP URL if mirroring is needed.
5. Publish manifest to battlebang/turrets/all/ota, or set ota.desired_build and enable ESP polling.
6. Watch status topics for ota_downloading / ota_rebooting / updated version.
```

## Why not GitHub runner direct-to-MQTT?

A GitHub-hosted runner usually cannot reach a private LAN broker such as
`10.2.80.x`. If direct publish is required, use a self-hosted runner on Jetson or
let the command center poll releases.


## Command Center-approved polling

Runtime config keeps automatic polling disabled by default:

```json
{"ota":{"command_center_controlled":true,"auto_check_enabled":false,"desired_build":0}}
```

To allow one build through polling, Command Center sends a config patch:

```bash
./bin/turret fleet-mqtt turret_2 config \
  --ota-auto-check-enabled true \
  --ota-desired-build 2 \
  --ota-public-manifest-url https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json
```

The ESP then polls the manifest URL on `ota.check_interval_s`. With
`command_center_controlled=true`, it applies only if the manifest build equals
`ota.desired_build`, the app/hardware/build/hash checks pass, and the turret is
in a safe OTA state.
