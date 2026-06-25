# BattleBang GitHub Actions guidance

## Scope

Applies to workflow files under `.github/workflows/**`.

## Firmware OTA workflow contract

- Active firmware OTA publishing is centralized in `.github/workflows/firmware-ota.yml`.
- Do not add one workflow per firmware unless there is a hard GitHub limitation that cannot be handled by the matrix.
- Firmware release independence must be represented by matrix rows: env, app, hardware, channel, artifact prefix, manifest name, version tag prefix, stable tag, version header, and path filters.
- `workflow_dispatch` should build the full matrix for smoke/release validation.
- `push` to `main` should build only affected matrix rows.

## Path filter rules

- Firmware folder changes select only that firmware row.
- Shared library changes select only firmware that actually includes that library.
- `platformio.ini`, the workflow file, and the release manifest generator may fan out broadly because they can affect build or release mechanics globally.
- Host-only provisioning/MQTT helper folders should not trigger OTA releases unless they are build-affecting scripts used by PlatformIO.

## Release rules

- Do not publish repo-wide `latest` assets for devices.
- Use firmware-specific stable tags (`<firmware>-latest`) for polling manifests.
- Use versioned tags (`<firmware>-v{version}`) for immutable audit assets.
- Stable release titles should show which version/build currently sits behind `latest`.
- Release manifests must point firmware URLs at versioned release assets, not at stable tags.

## Verification

- Parse workflow YAML locally before claiming done.
- Run contract tests that assert matrix rows, path filters, stable tags, and host-only script exclusions.
- After changing release mechanics, run a branch/manual dispatch when possible and verify manifest URLs, firmware URL HTTP 200, sha256, and size.
