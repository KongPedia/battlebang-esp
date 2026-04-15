# Turret Fleet Architecture

## Non-negotiable constraint

`src/turret/` remains untouched for current hardware testing. This new path lives
under `src/turret_fleet/` and can evolve without breaking the active test firmware.

## Problem with the current approach

The current turret path reads `src/turret/turrets.json` at build time and injects
values as C/C++ macros. That is useful for a few devices, but it still creates a
per-device firmware image. With 100+ devices, build/upload coordination becomes
the product risk.

## Target architecture

```text
GitHub tag/release
  -> GitHub Actions builds one generic firmware
  -> Release contains firmware.bin + manifest.json + sha256
  -> Command center/Jetson serves firmware over HTTP
  -> Command center publishes MQTT OTA manifest
  -> ESP validates manifest, downloads firmware, applies OTA, reboots

Command center config update
  -> MQTT config JSON
  -> ESP validates config_version
  -> ESP saves config to NVS/Preferences
  -> ESP applies config without firmware rebuild
```

## Runtime ownership

| Data | Owner | Transport | Persisted on ESP? |
|---|---|---|---|
| Firmware code | GitHub release | HTTP binary after MQTT manifest | OTA partition |
| `turret_id` | command center / provisioner | Serial or MQTT config | NVS |
| Coordinates | command center / provisioner | Serial or MQTT config | NVS |
| Calibration offsets | command center / provisioner | Serial or MQTT config | NVS |
| Wi-Fi/MQTT settings | provisioner | Serial or device MQTT config | NVS |
| Command target/fire/dead | runtime game logic | MQTT command | No |
| Status/heartbeat | ESP | MQTT status | No |

## Why MQTT + HTTP

MQTT is ideal for small control-plane messages: config, OTA manifest, command,
heartbeat. Firmware binaries are large, so HTTP/HTTPS is simpler for download,
range/resume, checksums, and hosting.

## Safety rules

1. ESP must reject wrong `app` or `hardware` in OTA manifests.
2. ESP must reject older/equal `build` unless `force=true`.
3. ESP must verify SHA-256 before finalizing the update.
4. Config changes must be versioned by `config_version`.
5. Physical firing remains explicit command-driven; config/OTA must not trigger fire.
