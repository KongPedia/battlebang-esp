# BattleBang shared ESP library guidance

## Scope

Applies to repo-private PlatformIO libraries under `lib/bb_esp_*`.

Use [.agents engineering criteria](../.agents/references/engineering.md) for C++ lifetime, timing and dependency review.
Root guidance-only/static-check and hardware-authorization boundaries also apply here.

## Role of `lib/`

- `lib/` owns reusable, firmware-agnostic capabilities.
- Keep libraries capability-scoped and composable. Do not create a monolithic `common` library.
- Firmware folders should include only the libraries they need so unused `.cpp` files are not linked into ESP32 images.

## Library boundaries

- `bb_esp_core`: header-first identity structs, common runtime config structs, string/json helpers, topic utilities.
- `bb_esp_nvs`: NVS/Preferences load-save glue for shared runtime fields.
- `bb_esp_net`: Wi-Fi connect/retry manager.
- `bb_esp_ota`: OTA manifest validation, HTTP download, reboot marker helpers.
- `bb_esp_hw`: low-level hardware safety helpers such as relay safe attach/off.

## What belongs here

- Generic code that has no firmware-specific app name, MQTT entity type, gameplay state, pin assignment, or product command semantics.
- Shared validation and serialization helpers used by multiple active firmware families.
- Hardware helpers only when the behavior is safety-preserving and parameterized by firmware-owned config.

## What does not belong here

- Domain commands such as `start`, `fire`, `unlock`, `pattern`, or HP gameplay logic.
- Firmware-specific MQTT topic roots/entity names beyond generic helper functions.
- Per-device defaults, local `.env` values, Wi-Fi secrets, serial ports, or Command Center IPs.
- New dependencies without explicit need and verification across affected PlatformIO envs.

## When editing shared libraries

- Update `.github/workflows/firmware-ota.yml` path filters if a firmware starts or stops depending on the library.
- Add or update tests that prove only the intended firmware families are affected.
- For application changes, build every PlatformIO env/variant that includes the changed library and review resource impact.
- Preserve NVS key maps/error reporting, bounded buffers/deadlines and product safe-state ownership during extraction.
- Reuse concrete composition/RAII where appropriate; do not add C++20, exceptions/RTTI, tasks or heap ownership from generic
  examples without selected-toolchain evidence and a demonstrated need. No automatic LOC-based split or new framework.
- Documentation-only edits use static references/diff review; builds/tests/CI/hardware are not prose completion gates.
