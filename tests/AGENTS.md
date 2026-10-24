# BattleBang test guidance

## Scope

Applies to tests under `tests/**`.

## Role of tests

- Contract tests document the firmware monorepo rules for agents and humans.
- Add or update tests in the same branch as firmware layout, NVS schema, MQTT topic, OTA, PlatformIO, or Actions changes.
- Python text/layout contracts are not compiled C++ behavior or physical validation. Prefer existing production-code
  host seams for behavioral regressions; `test/test_go2_nixo_uart` covers the framed parser/runtime.
- Guidance-only prose/skill edits use one-off static references/metadata/diff checks, not application test runs or a new
  guidance checker/test suite. Inspect existing test-read document paths before moving/removing them.
- Software tests must not connect to real serial/MQTT hardware or actuate. Never inherit `TURRET_RUN_HW_TESTS=1`;
  hardware/integration suites and operational helpers require separately authorized targets and human safety review.

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
- For framed protocol behavior use `./.venv-pio/bin/pio test -e native_go2_nixo_uart`; the separate
  `tests/run_tests.sh` game-logic harness needs existing host GoogleTest. Run these only for relevant application work.
- Report static/host/target/deployment/physical results distinctly; do not infer CI host-test coverage from OTA builds.
