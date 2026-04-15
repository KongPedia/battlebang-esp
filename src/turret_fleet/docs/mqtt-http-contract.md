# MQTT / HTTP Contract

## Roots

Default root is:

```text
battlebang
```

## Config payload

Topic examples:

```text
battlebang/devices/{device_id}/config
battlebang/turrets/{turret_id}/config
```

Payload:

```json
{
  "type": "config",
  "schema": 1,
  "config_version": 1,
  "turret_id": "turret_5",
  "pose": {
    "x_cm": -170.0,
    "y_cm": 190.0,
    "z_cm": 134.5,
    "default_target_z_cm": 70.0
  },
  "calibration": {
    "yaw_offset_deg": 0.0,
    "pitch_offset_deg": 0.0
  },
  "wifi": {
    "ssid": "YOUR_SSID",
    "password": "YOUR_PASSWORD"
  },
  "mqtt": {
    "host": "10.2.80.105",
    "port": 1883,
    "root": "battlebang"
  }
}
```

## Command payload

Topic:

```text
battlebang/turrets/{turret_id}/command
```

Payload examples:

```json
{"command":"idle"}
{"command":"dead"}
{"command":"fire"}
{"command":"target","target":{"x":3.0,"y":-0.4,"z":0.5}}
```

## OTA manifest

Topic examples:

```text
battlebang/turrets/all/ota
battlebang/turrets/{turret_id}/ota
battlebang/devices/{device_id}/ota
```

Payload:

```json
{
  "type": "firmware",
  "job_id": "fw-2026-04-15-001",
  "channel": "stable",
  "app": "battlebang-turret",
  "hardware": "esp32dev",
  "version": "0.2.0",
  "build": 42,
  "url": "http://10.2.80.105:8080/firmware/battlebang-turret-0.2.0.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 934000,
  "force": false
}
```

ESP behavior:

1. Parse manifest.
2. Check `app` and `hardware`.
3. Check `build > current_build` unless `force=true`.
4. Download `url` via HTTP.
5. Verify `sha256`.
6. Finalize OTA and reboot.

## GitHub Release manifest polling

For a minimal test without a local firmware HTTP server, `manifest.json` and the
firmware `.bin` can both live in a GitHub Release:

```text
https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v0.1.0/manifest.json
https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v0.1.0/battlebang-turret-fleet-0.1.0.bin
```

The ESP serial command for a manual smoke test is:

```text
check-ota https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v0.1.0/manifest.json
```

The firmware also has a default latest-manifest URL:

```text
https://github.com/KongPedia/battlebang-firmware/releases/latest/download/manifest.json
```

So these serial commands check the latest public firmware release:

```text
check-ota
check-latest
```

For controlled fleet rollout, keep MQTT as the trigger path and put the GitHub
Release `.bin` URL in the MQTT manifest payload.

## Status payload

Topic:

```text
battlebang/devices/{device_id}/status
battlebang/turrets/{turret_id}/status
```

Payload includes:

```json
{
  "type": "status",
  "device_id": "esp32-001122334455",
  "turret_id": "turret_5",
  "configured": true,
  "firmware_app": "battlebang-turret",
  "firmware_version": "0.2.0",
  "firmware_build": 42,
  "config_version": 1,
  "mode": "IDLE",
  "wifi": "UP",
  "ip": "10.2.80.123",
  "rssi": -54,
  "uptime_ms": 123456
}
```
