# Update operations: config vs firmware

There are two different "update" paths:

| Goal | MQTT topic suffix | Payload type | Rebuild firmware? | Reboot? |
| --- | --- | --- | --- | --- |
| Change position/calibration/Wi-Fi/MQTT config | `/config` | `{"type":"config", ...}` | No | Usually no; Wi-Fi/broker changes should reboot in current prototype |
| Upgrade ESP firmware code | `/ota` | `{"type":"firmware", ...}` manifest | Yes, via GitHub Action/release | Yes, OTA reboots after success |

Use this rule:

```text
Config update  -> publish config JSON to .../config
Firmware update -> publish firmware manifest JSON to .../ota
```

## Current tested device

The examples below use the current bench device:

```text
device_id: esp32-1c4ec0319d48
turret_id: turret_5
broker:    10.2.80.92:1883
root:      battlebang
```

Adjust these values for other devices.

## Config update sequence

Use config updates for values already stored in ESP NVS/Preferences:

- `turret_id`
- `pose.x_cm`
- `pose.y_cm`
- `pose.z_cm`
- `pose.default_target_z_cm`
- `calibration.yaw_offset_deg`
- `calibration.pitch_offset_deg`
- `wifi.ssid`
- `wifi.password`
- `mqtt.host`
- `mqtt.port`
- `mqtt.root`
- `mqtt.username`
- `mqtt.password`

### 1. Create config patch JSON

Example: change turret position only.

```bash
cat > /tmp/turret5-pose-v2.json <<'JSON'
{
  "type": "config",
  "config_version": 2,
  "pose": {
    "x_cm": -160.0,
    "y_cm": 180.0,
    "z_cm": 134.5,
    "default_target_z_cm": 70.0
  }
}
JSON
```

Always increase `config_version`.

### 2. Publish config over MQTT

Use the logical turret topic for pose/calibration:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/turrets/turret_5/config \
  --manifest /tmp/turret5-pose-v2.json
```

Use the physical device topic for Wi-Fi, MQTT broker, or `turret_id` changes:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/devices/esp32-1c4ec0319d48/config \
  --manifest /tmp/device-network-v3.json
```

### 3. Watch status

Subscribe to:

```text
battlebang/devices/esp32-1c4ec0319d48/status
battlebang/turrets/turret_5/status
```

Expected status:

```json
{
  "type": "status",
  "reason": "config_applied",
  "device_id": "esp32-1c4ec0319d48",
  "turret_id": "turret_5",
  "config_version": 2
}
```

### 4. Verify from Serial if needed

```text
show-config
net-status
tcp-probe
mqtt-status
```

### Wi-Fi/broker config caution

Wi-Fi and broker changes can disconnect the ESP from the current command path.

Safe flow:

1. Publish new Wi-Fi/broker config while the old connection still works.
2. Wait for `config_applied`.
3. Restart/power-cycle the ESP.
4. Confirm the ESP reconnects on the new network/broker.
5. If it does not reconnect, recover over USB Serial.

Current prototype behavior:

- Serial-delivered config reconnects Wi-Fi/MQTT immediately.
- MQTT-delivered Wi-Fi/broker changes are saved, but a restart is the cleanest
  way to apply them.

## Firmware OTA update sequence

Use firmware OTA updates for code changes:

- new control logic
- new Serial/MQTT commands
- bug fixes
- OTA logic changes
- diagnostics/logging changes

Firmware OTA does **not** replace runtime config. Existing NVS config remains:

```text
turret_id, pose, calibration, Wi-Fi, MQTT host, config_version
```

### 1. Commit and push firmware source changes

GitHub Action builds from the source that exists on GitHub. If a local change is
not committed/pushed, it will not be included in the release firmware.

### 2. Run GitHub Action manually

In the private source repository:

1. Open **Actions**.
2. Select **Turret Fleet Firmware**.
3. Click **Run workflow**.
4. Fill values similar to:

```text
version: 0.0.2-manual
build: 2
firmware_base_url:
public_release_repo: KongPedia/battlebang-firmware
public_release_target_branch: main
create_release: true
```

Do not include a leading `v` in `version`. The workflow creates a tag like:

```text
turret-fleet-v0.0.2-manual
```

`build` is the OTA comparison value. It must be higher than the firmware build
currently running on the ESP.

### 3. Confirm public release assets

In `KongPedia/battlebang-firmware`, the release should contain:

```text
manifest.json
battlebang-turret-fleet-0.0.2-manual.bin
sha256.txt
```

The manifest should have:

```json
{
  "type": "firmware",
  "app": "battlebang-turret",
  "hardware": "esp32dev",
  "version": "0.0.2-manual",
  "build": 2,
  "url": "https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v0.0.2-manual/battlebang-turret-fleet-0.0.2-manual.bin",
  "sha256": "...",
  "size": 970000,
  "force": false
}
```

### 4A. Trigger OTA manually from Serial

This is the simplest smoke test.

```text
check-ota
```

or:

```text
check-latest
```

Both use:

```text
https://github.com/KongPedia/battlebang-firmware/releases/latest/download/manifest.json
```

For a specific release:

```text
check-ota https://github.com/KongPedia/battlebang-firmware/releases/download/turret-fleet-v0.0.2-manual/manifest.json
```

### 4B. Trigger OTA over MQTT

Download the latest manifest:

```bash
curl -L \
  -o /tmp/bb_fleet_manifest_v0.0.2.json \
  https://github.com/KongPedia/battlebang-firmware/releases/latest/download/manifest.json
```

Publish to all turrets:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/turrets/all/ota \
  --manifest /tmp/bb_fleet_manifest_v0.0.2.json
```

Publish to one logical turret:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/turrets/turret_5/ota \
  --manifest /tmp/bb_fleet_manifest_v0.0.2.json
```

Publish to one physical ESP:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/devices/esp32-1c4ec0319d48/ota \
  --manifest /tmp/bb_fleet_manifest_v0.0.2.json
```

### 5. Watch OTA status

Expected status reasons:

```text
ota_downloading
ota_rebooting
```

If the build is not newer, the ESP publishes/skips with:

```text
ota_skipped
```

The firmware accepts OTA when:

```text
manifest.app == current app
manifest.hardware == current hardware
manifest.build > current build
```

unless `force=true`.

### 6. Verify after reboot

Open Serial or watch status:

```text
version=0.0.2-manual build=2
```

Then verify config survived:

```text
show-config
net-status
```

Expected:

```text
turret_id still the same
pose still the same
Wi-Fi still the same
MQTT broker still the same
config_version still the same
```

## Does GitHub Release auto-update ESPs by itself?

No, not in the current prototype.

GitHub Action creates release assets:

```text
manifest.json
firmware.bin
sha256.txt
```

The ESP updates only when one of these happens:

1. Serial command `check-ota` / `check-latest`
2. MQTT firmware manifest published to an `/ota` topic

The firmware does not currently poll GitHub latest periodically in the main loop.

## Fully automatic options later

To make "GitHub Action completed -> ESPs update automatically", choose one:

1. Put MQTT broker on a cloud/public endpoint and let GitHub Action publish the
   manifest to `/ota`.
2. Use a self-hosted GitHub runner on the same LAN as the broker.
3. Let command center watch GitHub Releases and publish OTA manifests.
4. Add periodic latest-manifest polling in ESP firmware.

For controlled rollout, the recommended production direction is:

```text
GitHub Action release
-> command center detects release
-> command center publishes manifest to selected /ota topics
-> ESPs update and report status
```

This avoids every ESP polling GitHub and gives the command center control over
which devices update when.
