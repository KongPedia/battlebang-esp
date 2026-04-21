# Runtime config operations

This document explains how to change a turret's runtime configuration without
rebuilding firmware.

The important rule is:

```text
Firmware code change -> build/release a new .bin
Turret placement/network/config change -> send config JSON
```

`src/turret/` still exists for the current bench-test path. This document is for
the future `src/turret_fleet/` firmware path.

## What is stored as config?

The ESP stores the latest accepted config in NVS/Preferences. It survives reset
and power loss.

| Field | Meaning | Example |
| --- | --- | --- |
| `config_version` | Monotonic config revision. Increase this for every real change. | `2` |
| `turret_id` | Logical turret name used by command center topics. | `turret_5` |
| `pose.x_cm` | Turret X position in centimeters. | `-170.0` |
| `pose.y_cm` | Turret Y position in centimeters. | `190.0` |
| `pose.z_cm` | Turret height in centimeters. | `134.5` |
| `pose.default_target_z_cm` | Default target height in centimeters. | `70.0` |
| `calibration.yaw_offset_deg` | Yaw correction in degrees. | `1.5` |
| `calibration.pitch_offset_deg` | Pitch correction in degrees. | `-0.8` |
| `wifi.ssid` | Wi-Fi SSID. | `Kong Studios` |
| `wifi.password` | Wi-Fi password. | not printed in logs |
| `mqtt.host` | Broker host/IP reachable by the ESP. | `10.2.80.92` |
| `mqtt.port` | Broker TCP port. | `1883` |
| `mqtt.root` | MQTT topic root. | `battlebang` |
| `mqtt.username` | Optional MQTT username. | `turret` |
| `mqtt.password` | Optional MQTT password. | not printed in logs |

The firmware applies config patches on top of the existing config. You do not
need to resend every field every time.

Example: this only changes X/Y and keeps Wi-Fi/MQTT/turret ID unchanged:

```json
{
  "type": "config",
  "config_version": 2,
  "pose": {
    "x_cm": -160.0,
    "y_cm": 180.0
  }
}
```

## Version rule

Always increase `config_version`.

If the ESP currently has:

```json
{"config_version": 1}
```

then send:

```json
{"config_version": 2}
```

The firmware rejects config with a lower version as stale. Equal versions are
accepted by the current prototype, but operationally you should still treat
`config_version` as monotonic and increment it.

## Delivery paths

There are two ways to deliver config.

### 1. USB Serial

Use this when:

- first provisioning a brand-new ESP
- Wi-Fi credentials are wrong
- the ESP cannot reach MQTT
- you are physically debugging the device

Open Serial:

```bash
./.venv-pio/bin/pio device monitor -b 115200 -p /dev/cu.usbserial-1140
```

Send one line:

```text
config {"type":"config","config_version":1,"turret_id":"turret_5","pose":{"x_cm":-170,"y_cm":190,"z_cm":134.5,"default_target_z_cm":70},"wifi":{"ssid":"YOUR_WIFI_SSID","password":"YOUR_WIFI_PASSWORD"},"mqtt":{"host":"10.2.80.92","port":1883,"root":"battlebang"}}
```

Verify:

```text
show-config
net-status
tcp-probe
```

Serial config immediately calls Wi-Fi reconnect and MQTT reconfigure.

### 2. MQTT

Use this when:

- the ESP is already online
- you want to change pose/calibration/turret ID remotely
- you want to stage a Wi-Fi/broker migration before rebooting the ESP

The ESP subscribes to both device-specific and turret-specific config topics:

```text
battlebang/devices/{device_id}/config
battlebang/turrets/{turret_id}/config
```

Use device topic for physical-device settings:

```text
battlebang/devices/esp32-1c4ec0319d48/config
```

Use turret topic for logical turret placement/config:

```text
battlebang/turrets/turret_5/config
```

Publish from this repo with the helper script:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/turrets/turret_5/config \
  --manifest /tmp/turret5-config.json
```

The helper script name says `manifest`, but it publishes any JSON file as an
MQTT payload.

## Check current device state

Serial commands:

```text
show-config
net-status
wifi-status
mqtt-status
tcp-probe
```

Example output:

```text
[fleet][wifi] serial status=CONNECTED code=3 ssid=Kong Studios ip=10.2.80.105 gateway=10.2.80.1 subnet=255.255.254.0 dns=10.2.150.5 rssi=-79
[fleet][mqtt] serial connected=yes state=0 host=10.2.80.92:1883 root=battlebang subscriptions_dirty=no
[fleet][tcp] result=ok elapsed_ms=59
```

MQTT status topics:

```text
battlebang/devices/{device_id}/status
battlebang/turrets/{turret_id}/status
```

Status payloads include `reason`, `device_id`, `turret_id`, firmware version,
`config_version`, mode, Wi-Fi state, IP, RSSI, and uptime.

## Change X/Y/Z position

Create a patch:

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

Publish:

```bash
../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/turrets/turret_5/config \
  --manifest /tmp/turret5-pose-v2.json
```

Expected ESP status:

```json
{
  "type": "status",
  "reason": "config_applied",
  "turret_id": "turret_5",
  "config_version": 2
}
```

No firmware rebuild is needed.

## Change calibration offsets

```bash
cat > /tmp/turret5-calibration-v3.json <<'JSON'
{
  "type": "config",
  "config_version": 3,
  "calibration": {
    "yaw_offset_deg": 1.2,
    "pitch_offset_deg": -0.5
  }
}
JSON

../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/turrets/turret_5/config \
  --manifest /tmp/turret5-calibration-v3.json
```

## Change `turret_id`

Changing `turret_id` changes the logical topic namespace. Prefer the device
topic so the command is addressed to the physical ESP even before the logical ID
changes.

```bash
cat > /tmp/device-change-turret-id-v4.json <<'JSON'
{
  "type": "config",
  "config_version": 4,
  "turret_id": "turret_6"
}
JSON

../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/devices/esp32-1c4ec0319d48/config \
  --manifest /tmp/device-change-turret-id-v4.json
```

After changing `turret_id`, use the new topic:

```text
battlebang/turrets/turret_6/command
battlebang/turrets/turret_6/config
battlebang/turrets/turret_6/status
```

Current prototype subscribes to new topics after a config update, but a restart
is still recommended after changing `turret_id` or `mqtt.root` so stale
subscriptions from the old logical ID are cleared.

## Change MQTT broker host/port

If only the broker IP changes but Wi-Fi stays the same, send the new MQTT config
to the current broker while the ESP is still connected.

```bash
cat > /tmp/device-mqtt-v5.json <<'JSON'
{
  "type": "config",
  "config_version": 5,
  "mqtt": {
    "host": "10.2.80.50",
    "port": 1883,
    "root": "battlebang"
  }
}
JSON

../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/devices/esp32-1c4ec0319d48/config \
  --manifest /tmp/device-mqtt-v5.json
```

Then verify the ESP reported `config_applied`.

Current prototype note:

- MQTT-delivered broker host/port changes are saved to NVS.
- For the cleanest handoff to the new broker, restart/power-cycle the ESP after
  `config_applied`.
- Serial-delivered broker changes reconfigure MQTT immediately.

After reboot, check:

```text
net-status
tcp-probe
```

## Change Wi-Fi

Wi-Fi changes are the most dangerous config change because if the new SSID or
password is wrong, the ESP can no longer receive remote commands.

Safe migration flow:

1. Keep the ESP online on the current Wi-Fi.
2. Prepare the new broker/network if needed.
3. Publish the new Wi-Fi config to the device topic on the current broker.
4. Wait for `config_applied` status.
5. Restart/power-cycle the ESP so it boots with the new Wi-Fi.
6. Confirm it appears on the new broker/status topic.
7. If it does not reconnect, recover with USB Serial.

Example:

```bash
cat > /tmp/device-wifi-v6.json <<'JSON'
{
  "type": "config",
  "config_version": 6,
  "wifi": {
    "ssid": "NEW_WIFI_SSID",
    "password": "NEW_WIFI_PASSWORD"
  },
  "mqtt": {
    "host": "NEW_BROKER_IP",
    "port": 1883,
    "root": "battlebang"
  }
}
JSON

../battlebang/.venv/bin/python scripts/turret_fleet/publish_mqtt_manifest.py \
  --host 10.2.80.92 \
  --topic battlebang/devices/esp32-1c4ec0319d48/config \
  --manifest /tmp/device-wifi-v6.json
```

Current prototype note:

- MQTT-delivered Wi-Fi changes are saved to NVS.
- They should be treated as taking effect after ESP restart/power-cycle.
- Serial-delivered Wi-Fi changes call Wi-Fi reconnect immediately.

## Full config replacement

For first provisioning or a full replacement, send all fields:

```json
{
  "type": "config",
  "schema": 1,
  "config_version": 10,
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
    "ssid": "YOUR_WIFI_SSID",
    "password": "YOUR_WIFI_PASSWORD"
  },
  "mqtt": {
    "host": "10.2.80.92",
    "port": 1883,
    "root": "battlebang"
  }
}
```

## Retained MQTT messages

`--retain` makes the broker remember the last payload for a topic.

Good candidates:

- OTA manifest
- non-secret pose/calibration config

Avoid retain for payloads that contain:

- Wi-Fi password
- MQTT password
- other secrets

The current local broker allows anonymous clients, so retained secret payloads
would be visible to any client that can connect to the broker.

## Recovery

If a config mistake makes the ESP unreachable:

1. Plug in USB.
2. Open Serial monitor.
3. Run `show-config` and `net-status`.
4. Send a corrected `config {...}` line.
5. Verify `tcp-probe` and `mqtt-status`.

If needed, clear all runtime config:

```text
clear-config
```

Then provision again over Serial.
