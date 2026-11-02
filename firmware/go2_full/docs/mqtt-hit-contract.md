# Go2 Full ESP to Command Center MQTT contract

The rename to `go2_full` changes the firmware identity shown in payloads, but
not deployed MQTT routes or the MQTT device type.

```text
ESP -> Command Center
battlebang/hit/{robot_id}/events
{mqtt_root}/devices/go2_nixo/{device_id}/status

Command Center -> ESP reset/debug
battlebang/hit/{robot_id}/ring_display/command

Command Center -> Blaster relay
battlebang/nixo/{nixo_id}/command
```

Accepted piezo hits are scored on the ESP, decrement local HP, update the HP
bar, and publish `hit_event`. A representative identity fragment is:

```json
{
  "schema_version": 2,
  "event": "hit_event",
  "robot_id": "go2_05",
  "accepted": true,
  "hp_remaining": 11,
  "max_hits": 14,
  "down": false,
  "firmware": "go2_full",
  "firmware_role": "integrated_hit_led_nixo",
  "metadata": {
    "decision_owner": "esp_local",
    "display_owner": "esp_local",
    "hp_current": 11,
    "hp_max": 14
  }
}
```

Queued retransmits remain already-accepted ESP-local hits. After `down=true`,
additional piezo triggers are not accepted until reset; status uses reason
`local_hit_ignored_down` for those samples. Combat state is not
persisted across reboot; configuration and the match HP maximum record use NVS.

The ring display reset/debug compatibility route retains its historic topic name:
`ring_display/command`. Normal per-hit display
updates are ESP-owned; Command Center uses this topic only for an explicit
`reset_hit_state=true` or maintenance/debug override.

Jetson HP/fire control does not use MQTT or newline ASCII tokens. UART2 and USB
serial builds carry the same framed version-2 binary protocol. USB-framed builds
must not receive text commands on USB.
