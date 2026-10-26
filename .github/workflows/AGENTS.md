# BattleBang GitHub Actions guidance

## Scope

Applies to workflow files under `.github/workflows/**`.

## Firmware OTA workflow contract

- Active firmware OTA publishing is centralized in `.github/workflows/firmware-ota.yml`.
- Do not add one workflow per firmware unless there is a hard GitHub limitation that cannot be handled by the matrix.
- Firmware release independence must be represented by matrix rows: env, app, hardware, channel, artifact prefix, manifest name, version tag prefix, stable tag, version header, and path filters.
- `workflow_dispatch` builds the full matrix for explicitly requested smoke/release validation; it can publish releases
  by default. This describes behavior, not permission to dispatch or publish.
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
- For workflow application changes, run offline contract tests for matrix rows, path filters, stable tags and host-only
  script exclusions. Guidance-only edits need static parsing/references/diff, not tests, builds or dispatch.
- Report unverified release behavior. Run a branch/manual dispatch or publish only with explicit authorization and reviewed
  inputs; distinguish build-only, versioned publication and stable-pointer updates. Do not use publication as a prose gate.
- Existing main filters include firmware-folder documents; an authorized merge may publish affected images even when the
  diff is prose-only. Report that impact without silently changing CI path filters.
