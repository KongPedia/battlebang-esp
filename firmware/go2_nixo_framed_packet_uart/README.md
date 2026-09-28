# Go2/Nixo framed packet UART firmware

This firmware is the refactored Jetson UART implementation. It reuses the
hardware, display, MQTT, NVS, and relay modules under `firmware/go2_nixo`, but
has its own `main.cpp` and framed UART codec/runtime.

- App: `battlebang-go2-nixo-framed-packet-uart`
- Version/build: injected by the main/release workflow; not hardcoded on feature branches
- NVS provisioning template: `.env.go2_nixo_framed_packet_uart.example`
- Automatic OTA polling remains disabled by the shared runtime default.

Build environments:

```bash
pio run -e esp32dev_go2_nixo_framed_packet_uart_1ch
pio run -e esp32dev_go2_nixo_framed_packet_uart_2ch
```

Provision the shared NVS schema with the framed firmware defaults:

```bash
python scripts/go2_nixo/provision.py \
  --env-file firmware/go2_nixo_framed_packet_uart/.env.go2_nixo_framed_packet_uart.example \
  --serial-port /dev/cu.usbserial-XXXX
```

The existing `esp32dev_go2_nixo_1ch/2ch` environments continue to build the
single-character forced-newline firmware from `firmware/go2_nixo/main.cpp`.
The two firmware families never auto-detect or mix UART formats.
The optional piezo-channel mask and qualification behavior described below
apply to this **framed** firmware, not the legacy single-character firmware.

## Piezo channel availability

`piezo_channel_enable_mask` is NVS-backed: left=1, right=2, front=4. The
default is `3` (left and right; front remains implemented but is not required).
When only the right sensor is physically connected, set the mask to `2` via a
Bluetooth SPP line command such as
`config {"config_version":<current version + 1>,"hit":{"piezo_channel_enable_mask":2}}`.
Read the current version with `show-config` first; the config command rejects
missing or decreasing versions. The same line commands work over USB debug
serial on UART2 builds.
Set it back to `3` only after both sensors are connected; use `7` if the front
sensor is restored. `status` reports each channel's enabled/qualified/raw
state. A channel must see a quiet interval before it can produce hits.

GPIO34/35 have no internal pull resistors. A disconnected, floating input
cannot be reliably distinguished in firmware from a quiet connected sensor;
disable absent channels explicitly. This is a configuration mitigation, not a
substitute for electrical bias and a physical false-hit test.

## HP bar row/column wiring check

The HP bar is 28 logical columns × 3 LEDs (84 physical LEDs). If the three
rows do not align, do not guess a new index mapping from the all-solid boot
animation: that animation bypasses the column mapping. With the robot safely
stationary, send these **Bluetooth SPP line commands** to the ESP. USB debug
serial also accepts them in the UART2 transport build, but is reserved for
framed Jetson packets in the USB transport build; never inject text there:

```text
led-test 1         # only logical HP column 1: three white LEDs, 10 seconds
led-test 14        # middle column
led-test 28        # final column
led-test pixel 1   # only raw LED index 1, 10 seconds
led-test pixel 56  # trace the physical row wiring
led-test off       # return immediately to the HP display
```

The diagnostic expires automatically after 10 seconds and does not alter HP.
Record which physical (row, column) lights for logical groups 1/14/28 and
individual pixels around each row boundary (1, 28, 29, 56, 57, 84). Use that
evidence to correct the harness mapping; the current formula is inherited from
the original reference harness and cannot prove this robot's wiring order.
