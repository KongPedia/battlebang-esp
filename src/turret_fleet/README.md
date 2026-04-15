# BattleBang Turret Fleet Firmware (future path)

> **Do not use this folder for today's turret bench test yet.**
> Existing `src/turret/` is intentionally left in place and unchanged for current testing.

This folder is a new, modular firmware path for the future fleet architecture:

- one generic ESP32 firmware image
- runtime turret configuration stored in ESP32 NVS/Preferences
- config updates via Serial or MQTT
- firmware update manifests via MQTT
- firmware binary download via HTTP
- GitHub tag/release driven build artifacts

The goal is to remove per-turret hardcoding from the firmware image. Coordinates,
`turret_id`, calibration, Wi-Fi, and MQTT settings belong to runtime config, not
compile-time macros.

## Current status

This is a scaffold that compiles separately from `src/turret/`:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
```

It includes dynamic config storage, MQTT topics, status publish, OTA manifest
parsing, and HTTP OTA download/apply logic. The physical turret motion/fire
control is currently a safe stub in `control/turret_control.cpp`; port the proven
logic from `src/turret/runtime/*.inc` module-by-module after the architecture is
validated.

## Folder layout

```text
src/turret_fleet/
  app/        firmware identity/version metadata
  config/     RuntimeConfig + NVS/Preferences storage
  control/    turret command facade; safe stub for now
  mqtt/       topic builder + MQTT config/command/OTA bus
  net/        Wi-Fi manager
  ota/        OTA manifest validation + HTTP OTA apply
  docs/       architecture and rollout notes
  examples/   sample config and OTA manifest payloads
  main.cpp    small composition root
```

## Serial provisioning

After flashing the generic firmware, send a config line over Serial:

```text
config {"type":"config","config_version":1,"turret_id":"turret_5","pose":{"x_cm":-170,"y_cm":190,"z_cm":134.5,"default_target_z_cm":70},"wifi":{"ssid":"YOUR_SSID","password":"YOUR_PASSWORD"},"mqtt":{"host":"10.2.80.105","port":1883,"root":"battlebang"}}
```

Useful commands:

```text
show-config
  clear-config
  check-ota <manifest-url>
  help
```

For a GitHub Release based OTA smoke test, `check-ota` can point at a release
asset manifest:

```text
check-ota https://github.com/<owner>/<repo>/releases/download/turret-fleet-v0.1.0-manual/manifest.json
```

The manifest's `url` may also point at a GitHub Release `.bin` asset. The current
prototype follows HTTPS redirects and uses insecure TLS for easier testing; pin a
CA certificate or signed manifest before production use.

## MQTT topics

With `mqtt.root = "battlebang"` and `turret_id = "turret_5"`:

```text
battlebang/devices/{device_id}/config
battlebang/devices/{device_id}/ota
battlebang/devices/{device_id}/status
battlebang/turrets/all/ota
battlebang/turrets/turret_5/config
battlebang/turrets/turret_5/ota
battlebang/turrets/turret_5/command
battlebang/turrets/turret_5/status
```

## Design rule

- Code change: build/release new firmware `.bin`.
- Config change: publish config JSON; ESP stores it in NVS and applies it.
- Firmware rollout: publish OTA manifest over MQTT; ESP downloads `.bin` over HTTP.
- GitHub Release test: fetch `manifest.json` and `.bin` directly from release URLs.
