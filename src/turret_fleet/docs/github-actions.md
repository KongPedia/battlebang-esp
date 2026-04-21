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
   - `public_release_repo`: default `KongPedia/battlebang-firmware`
   - `public_release_target_branch`: default `main`
   - `create_release`: `false` for artifact-only testing, `true` to publish a release to the public firmware repo

Before using `create_release=true`, add a private source repo secret:

```text
PUBLIC_RELEASE_REPO_TOKEN
```

Use a fine-grained GitHub personal access token scoped only to
`KongPedia/battlebang-firmware` with **Contents: Read and write** permission.

GitHub push/tag triggers can be added later after the manual artifact test is
working end-to-end.

## GitHub Release as the firmware host

If `create_release=true` and `firmware_base_url` is empty, the workflow publishes
to `KongPedia/battlebang-firmware` and writes the `.bin` URL in `manifest.json`
as a public GitHub Release asset URL:

```text
https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v{version}/battlebang-turret-fleet-{version}.bin
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
check-ota https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v{version}/manifest.json
```

Or use the default latest manifest baked into the `turret_fleet` firmware:

```text
check-ota
check-latest
```

Or the same `manifest.json` can be sent over MQTT to
`battlebang/turrets/all/ota`.

For the full operational sequence after a release is created, including Serial
`check-ota` and MQTT `/ota` publish options, see `update-operations.md`.

Important limitations:

- The source repo can stay private. Only release assets in
  `KongPedia/battlebang-firmware` are public.
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

`manifest.json` can be published by the command center after it rewrites/sets the
HTTP URL that ESP32 devices can reach on the local network.

## Local command-center flow

```text
1. Watch GitHub releases or receive webhook.
2. Download firmware.bin + manifest.json.
3. Serve firmware.bin from Jetson/command center HTTP server.
4. Set manifest.url to local HTTP URL if needed.
5. Publish manifest to battlebang/turrets/all/ota.
6. Watch status topics for ota_downloading / ota_rebooting / updated version.
```

## Why not GitHub runner direct-to-MQTT?

A GitHub-hosted runner usually cannot reach a private LAN broker such as
`10.2.80.x`. If direct publish is required, use a self-hosted runner on Jetson or
let the command center poll releases.
