# BattleBang active firmware guidance

## Scope

Applies to active ESP32 firmware applications under `firmware/**`.

Follow root scope and [.agents engineering criteria](../.agents/references/engineering.md). Guidance-only changes use
static references/metadata/diff review, not builds, application tests or hardware. The application rules below do not
authorize upload, provisioning, OTA/reboot, live serial/MQTT, movement or fire.

## Role of `firmware/`

- Put new deployable ESP32 applications under `firmware/<firmware_name>/`.
- Do not add new active firmware under `src/`; `src/` is legacy/reference/compatibility only.
- A firmware folder owns product-specific behavior: identity constants, hardware profile, domain runtime config, domain MQTT commands/status, controller state machines, safety checks, local examples, and operator docs.
- A firmware folder should compose shared capabilities from `lib/bb_esp_*` instead of copying NVS, Wi-Fi, OTA, topic, or relay helper implementations.
- Keep ESP32 binary size selective: include only the `bb_esp_*` libraries that firmware actually uses.

## Required shape for new active firmware

When adding `firmware/<name>/`, add or update all of these in the same branch:

1. `firmware/<name>/README.md` with hardware purpose, pins, serial/MQTT commands, NVS fields, OTA channel, and validation commands.
2. `firmware/<name>/.env.<name>.example` for provisioning defaults; never commit real `.env.*` values.
3. `firmware/<name>/app/firmware_info.h` with unique app, hardware, release repo, version/build/git SHA, and stable manifest URL constants.
4. Runtime config bridge that persists identity, `stage_id`, Wi-Fi, MQTT, OTA policy, and tunables in NVS.
5. MQTT topic code that uses shared topic utilities and validates topic segments.
6. OTA integration that uses `bb_esp_ota` manifest/hash/build checks and a firmware-specific stable manifest tag.
7. A `platformio.ini` env that points at `+<../firmware/<name>/**>` and excludes unrelated firmware folders.
8. Host provisioning/MQTT helper scripts under `scripts/<name>/` when the firmware needs USB/MQTT operations.
9. Python contract tests under `tests/python/` that lock the firmware identity, NVS schema, MQTT topics, OTA URL, PlatformIO env, and workflow matrix entry.
10. A `.github/workflows/firmware-ota.yml` matrix row and path filters for the firmware-specific folder plus only the shared libraries it actually includes.

## NVS and identity rules

- Device IDs, stage IDs, MQTT root/host, OTA policy, and tuning values that operators adjust should be NVS-backed.
- Use runtime IDs for per-device identity. Do not create PlatformIO envs like `esp32dev_<firmware>_01` for numbered devices.
- Keep unsafe electrical defaults such as relay pin maps, relay polarity, hardware variant channel count, and hard safety envelopes as build-time or hardware-profile data unless a safety review explicitly approves NVS control.
- Preserve product-specific exceptions already implemented: Turret Fleet supports validated NVS-backed relay profiles and
  per-channel polarity, unlike Go2/Nixo's build-only relay variants. Do not silently remove that contract or bypass safe-OFF.
- `stage_id` is part of the standard runtime config so Command Center can target only devices in a stage.

## OTA channel rules

- Never use GitHub repo-wide `/releases/latest/download/...` URLs for device polling.
- Every active firmware/variant gets a unique stable manifest tag and versioned firmware tag.
- Stable tags are polling pointers; versioned tags are audit artifacts.
- Manifest `app`, `hardware`, `channel`, `version`, `build`, `url`, `sha256`, and `size` must match firmware constants and release assets.

## Verification before completion

- For application changes, run the relevant explicit `pio run -e <env>` build and affected variants/consumers.
- Run matching offline host behavioral and Python contract tests; string/layout contracts are not C++ behavior.
- Run `git diff --check`.
- Report missing deployment/physical evidence. Upload/provisioning/reboot needs separate target-specific authorization;
  movement/fire/recovery needs explicit actuation approval and human review. Do not automatically run hardware tests.
- Preserve the selected product's transport: production USB-framed Go2/Nixo is binary-only, not a `show-status` text console.
