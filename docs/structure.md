# Firmware repository structure

This is an existing Arduino ESP32 / PlatformIO monorepo, not a generated native starter. The root
[AGENTS.md](../AGENTS.md), [README](../README.md) and owning guides describe actual supported work.

| Path | Owner |
| --- | --- |
| `firmware/` | Active Go2, Go2/Nixo newline/framed UART/USB, Boss Target, Heavy Blaster, Station and Turret Fleet products |
| `lib/bb_esp_*` | Capability-scoped core, NVS, networking, OTA and hardware mechanisms; product safety policy stays local |
| `src/` | Retained legacy/reference/compatibility firmware; no new active applications |
| `platformio.ini` | Actual compilation map and env inheritance/filters; select an explicit env, not its legacy default |
| `scripts/`, `bin/` | Host provisioning, protocol, operator and release helpers; not safe smoke tests or thin generic starters |
| `test/`, `tests/` | Production-code native seams, game-logic harness, focused Python contracts and separately gated hardware tests |
| `.github/workflows/` | Firmware OTA build/package/publication matrix; not proof of native/Python test execution |
| `docs/` | Navigation and cross-cutting pointers; current product/operator contracts remain beside firmware |
| `.agents/` | Project workflow, ESP engineering policy and plugin setup; reusable bodies/forms/provenance/licenses stay with the provider |
| `.codex/config.toml`, `.claude/settings.json` | Selected plugin activation, not installation/trust/discovery or custom Codex agent registration |

Existing environments are `.venv-pio` and `.venv-turret-tests`; setup is separately requested work. Host C++17 checks do
not set the target language standard, and no universal compiler/platform version is inferred here. Native manifests,
locks and runtime dependencies are not introduced by guidance enrollment. Root and owner guides retain the selected
offline/target checks; guidance-only work uses static parsing, references, provenance/licenses and diff inspection.
Builds, ACKs and hashes do not prove successful boot, electrical behavior or physical safety.
