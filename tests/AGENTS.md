# BattleBang test guidance

## Scope

Applies to tests under `tests/**`.

## Role of tests

- Contract tests document the firmware monorepo rules for agents and humans.
- Add or update tests in the same branch as firmware layout, NVS schema, MQTT topic, OTA, PlatformIO, or Actions changes.

## Required coverage for new active firmware

When adding a firmware family, include tests that assert:

- The firmware lives under `firmware/<name>/`, not active `src/<name>/`.
- `platformio.ini` has one generic env per firmware/variant, not per numbered device ID.
- Firmware identity constants (`app`, `hardware`, release repo, stable manifest URL) are unique and match release manifests.
- Provisioning writes NVS-backed identity, `stage_id`, Wi-Fi, MQTT, OTA policy, and tunables.
- MQTT topics use safe topic segments and expected device/entity roots.
- OTA manifest validation rejects wrong app/hardware/build/hash and uses firmware-specific stable tags.
- `.github/workflows/firmware-ota.yml` has the correct matrix row and path filters.
- Host-only provisioning/MQTT helper changes do not trigger OTA release builds.

## Verification commands

- Use `.venv-turret-tests/bin/python -m pytest tests/python/<matching_contract>.py -q` for focused checks.
- Use `python3 -m py_compile` for edited scripts/tests.
- Run `git diff --check` before completion.
