# BattleBang ESP Agent Notes

## Scope

This repository contains ESP32 firmware and helper scripts for BattleBang devices. Keep generated firmware, virtualenvs, serial logs, and secrets out of git.

## Secrets and local env

- Never commit Wi-Fi passwords, MQTT passwords, or local serial target files.
- Use `src/turret_fleet/.env.turret_fleet` for fleet provisioning/MQTT helper defaults; it is ignored.
- Use `src/hit_target/.env.hit_target` for hit-target Wi-Fi/MQTT/Command Center/provisioning defaults; it is ignored.
- Keep only examples tracked, e.g. `src/turret_fleet/.env.turret_fleet.example` and `src/hit_target/.env.hit_target.example`.

## Python and PlatformIO environments

Use the existing split virtualenv convention:

```bash
python3 -m venv .venv-pio
./.venv-pio/bin/python -m pip install -U platformio pyserial

python3 -m venv .venv-turret-tests
./.venv-turret-tests/bin/python -m pip install -r tests/python/requirements.txt
```

Preferred commands:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
./.venv-pio/bin/pio run -e esp32dev_hit_target
./.venv-pio/bin/pio run -e esp32dev_turret_fleet -t upload --upload-port /dev/cu.usbserial-120
.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py tests/python/test_hit_target_contract.py -q
python3 -m py_compile scripts/turret_fleet/*.py scripts/hit_target/*.py tests/python/test_turret_fleet_contract.py tests/python/test_hit_target_contract.py
```


## Hit target operating model

- `src/hit_target/` is the generic standalone circular LED hit-target firmware. It is separate from Go2-mounted hit targets and from turret fleet.
- Device identity is derived from ESP32 eFuse MAC by default; use runtime `group` and `location` metadata instead of numbered target build profiles.
- Wi-Fi, MQTT/Command Center host, gameplay HP phases, sensor tuning, and OTA policy are provisioned into NVS from `src/hit_target/.env.hit_target` via `scripts/hit_target/provision.py`.
- Keep hit-target GitHub Actions scoped to hit-target source/scripts plus the shared `platformio.ini` build index.

## Turret fleet operating model

- `src/turret/` is the legacy/reference turret firmware.
- `src/turret_fleet/` is the generic runtime-configured fleet firmware.
- Blank ESP32 devices still require first USB flashing/provisioning; after that, identity, Wi-Fi, MQTT, pose, calibration, motion, fire, and OTA policy are stored in NVS and can be changed via MQTT config.
- MQTT target coordinates are meters by default (`coordinate_frame.mqtt_target_unit = "m"`); firmware converts to centimeters internally.
- Direct `aim` uses local yaw/pitch degrees. `jog` uses PWM microseconds and is for bounded bench debugging only.

## Safety policy

- Command Center should clamp operator intent to the configured safe envelope before sending commands.
- ESP firmware still clamps normal target/aim/home/idle/dead setpoints to `motion.limits`.
- ESP must reject invalid frame IDs, invalid config envelopes, firing during lockout/dead/disabled hardware, and brownout/fire-reset lockout commands.
- Brownout or reset during fire is not resumed from saved pose. Firmware stores an NVS recovery marker, forces fire hardware off, and auto-recovers only to safe WAIT_COMMAND when current feedback is stable; otherwise it requires explicit `recover`.

## OTA policy

- GitHub Actions builds `esp32dev_turret_fleet` and `esp32dev_hit_target` into separate release channels by default.
- Do not use repo-wide `/releases/latest/download/...` for device polling in this multi-firmware repo. Use firmware-specific stable tags:
  - turret fleet: `https://github.com/KongPedia/battlebang-esp/releases/download/turret-fleet-latest/manifest.json`
  - hit target: `https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json`
- Versioned tags (`turret-fleet-v{version}`, `hit-target-v{version}`) remain audit points. Stable tags are only polling pointers and must not overwrite another firmware family's manifest asset.
- Automatic polling applies only when enabled by runtime config. With `ota.command_center_controlled=true`, the polled manifest build must exactly match `ota.desired_build`.
- Direct MQTT `/ota` manifest messages are treated as Command Center-approved jobs and still pass app/hardware/build/hash/safe-state checks.

## Before claiming completion

Run the relevant Python contract tests, PlatformIO build, `git diff --check`, and, for hardware changes, upload plus serial `show-status` evidence. Report remaining hardware risks separately from software verification.
