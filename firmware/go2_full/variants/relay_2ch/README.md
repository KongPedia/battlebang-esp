# Go2 Full relay_2ch variant

- Channel 1: GPIO23, flywheel
- Channel 2: GPIO22, chain
- Inter-channel delay: 150 ms
- Relay polarity: active HIGH
- Normal shutdown: chain off, then flywheel off
- Canonical builds: `esp32dev_go2_full_uart_2ch`, `esp32dev_go2_full_usb_2ch`

These are committed defaults, not physical-board verification. Optional build
defines may change the effective profile; confirm the artifact, wiring and safe-OFF
before authorized deployment. NVS cannot change relay polarity.

## Historical service notes

The removed Go2/Nixo guide recorded the following per-device values. They are
historical service evidence, not a current device inventory, deployment target or
Go2 Full wiring map. The old table used `nixo_*` names; the current image uses
Blaster fields and the GPIO23 flywheel/GPIO22 chain mapping above. Do not flash
the active-HIGH image to an active-LOW board without a reviewed matching profile
and verified output isolation.

| Robot | Relay channels | `nixo_relay_on_level` | `nixo_relay_off_level` |
| --- | ---: | ---: | ---: |
| `go2_03` | 2ch | `0` | `1` |
| `go2_04` | 2ch | `1` | `0` |
| `go2_06` | 2ch | `1` | `0` |
