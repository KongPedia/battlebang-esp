# Turret fleet runtime profiles

These files are non-secret per-turret runtime profiles. They are safe to commit
because Wi-Fi credentials and MQTT broker credentials still come from
`src/turret_fleet/.env.turret_fleet` during USB provisioning, or remain unchanged
when a profile is published over MQTT.

Why `profiles/` instead of `configs/`?

- `src/turret_fleet/config/` is C++ firmware code for `RuntimeConfig` and NVS.
- `src/turret_fleet/profiles/` is operator data: one JSON profile per physical turret.

## Current boss-room layout assumption

The four-turret layout is derived from the verified `turret_2` pose and the
operator layout:

```text
turret_1            turret_2


turret_3            turret_4
```

- `turret_2` remains the reference at `x=-300cm, y=200cm, z=134.5cm`.
- Left/right spacing is the world `y` axis, 2m between columns.
- Top/bottom spacing is the world `z` axis, 1m between rows.
- `x` is held constant because no front/back spacing was specified.

Resulting poses:

| Turret | Row | Column | x_cm | y_cm | z_cm |
| --- | --- | --- | ---: | ---: | ---: |
| `turret_1` | upper | left | -300.0 | 0.0 | 134.5 |
| `turret_2` | upper | right | -300.0 | 200.0 | 134.5 |
| `turret_3` | lower | left | -300.0 | 0.0 | 34.5 |
| `turret_4` | lower | right | -300.0 | 200.0 | 34.5 |

Each file uses the same verified safe motion envelope currently tuned on
`turret_2`: local yaw `-55..35`, pitch `-45..70`, home `0,0`, dead pitch `65`.
Adjust these via MQTT config patches only after physical calibration evidence.

## Relay polarity profiles

The same `esp32dev_turret_fleet` firmware supports the current mixed relay
hardware through NVS-backed fire config.  USB provisioning or MQTT config saves
an explicit `fire.relay_profile` plus channel polarity fields per turret:

- one-channel fire relay on `GPIO23` / CH3: `relay_profile=single_channel_ch3_active_high`, active-HIGH on CH3 (`relay_ch3_active_low=false`).
- two-channel relay: `relay_profile=two_channel_active_low`, active-LOW on the flywheel/channel path (`GPIO22` / CH2) and chain/fire path (`GPIO23` / CH3).

Keep CH2 active-LOW for the BLDC/flywheel path.  Only invert CH3 on the
one-channel relay turrets; otherwise the fire motor can remain energized after
the fire sequence completes.
