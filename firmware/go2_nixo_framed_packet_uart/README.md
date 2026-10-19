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
pio run -e esp32dev_go2_nixo_framed_packet_usb_2ch
```

Provision the shared NVS schema with the framed firmware defaults:

```bash
python scripts/go2_nixo/provision.py \
  --env-file firmware/go2_nixo_framed_packet_uart/.env.go2_nixo_framed_packet_uart.example \
  --serial-port /dev/cu.usbserial-XXXX
```

That USB serial command applies only to a **UART2 transport image**, whose USB
port is a debug/management channel. On a USB-framed image, the same USB port
is Jetson's binary data channel and must not receive ASCII commands. To manage
an already-flashed USB-framed ESP, use a paired Bluetooth SPP serial port with
`--management-transport bluetooth_spp`, or temporarily flash the UART2
maintenance image and use `--management-transport uart2_usb_debug`, then flash
the USB-framed image again. Set `GO2_NIXO_JETSON_TRANSPORT=usb_serial` in the
robot's private env so provisioned OTA metadata matches its final image.

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

## Per-robot HP bar length and color order

The physical HP bar length is NVS-backed as `hp_bar_group_count` (columns per
row, three serpentine rows). Set `GO2_NIXO_HP_BAR_GROUP_COUNT` independently
in **each robot's private** `.env.go2_nixo` before provisioning. The shared
example defaults to 28 columns; the firmware supports up to **29 columns
(87 LED addresses)**. Set `GO2_NIXO_HP_BAR_GROUP_COUNT=29` only on the new
29-column bar. **go2_06 previously used 27 columns (81 lit LEDs)**,
verified by individual LED tests: left 1–27 bottom-to-top, middle 28–54
top-to-bottom, right 55–81 bottom-to-top; 82–84 did not illuminate. Do not
change the shared default for other robots without testing their hardware.
For go2_06 set `GO2_NIXO_HP_BAR_GROUP_COUNT=27` and, when only its right piezo
is connected, `GO2_NIXO_PIEZO_CHANNEL_ENABLE_MASK=2`. Reprovisioning then
persists both fields to ESP NVS; `status` reports the active column count.
They are also mirrored in a hardware NVS namespace so `clear-config` keeps
the sensor mask and bar length while clearing ordinary provisioning. The
legacy single-character firmware rejects nondefault values and disables
piezo detection if booted with a framed-only hardware profile.
With 27 columns and 14 HP, one HP step necessarily removes one column rather
than two; all other steps remove two columns, aligned across all three rows.

The new 12 V HP bar model swaps red and green relative to the existing RGB
model. Provision `GO2_NIXO_HP_BAR_COLOR_ORDER=1` **only for that bar**; the
default `0` preserves existing robots. This setting affects only the HP bar,
not the separate Nixo fire ring. Check startup red then blue, full-HP green,
hit flash red, and down red on the physical bar before deployment. Firmware
changes alone do not update a robot's saved NVS settings.

## HP bar row/column wiring check

The generic HP bar capacity is 29 logical columns × 3 LEDs (87 addresses),
but the active column count may be lower per robot. If the three rows do not
align, do not guess a new index mapping from the all-solid boot animation:
that animation bypasses the column mapping. With the robot safely
stationary, send these **Bluetooth SPP line commands** to the ESP. USB debug
serial also accepts them in the UART2 transport build, but is reserved for
framed Jetson packets in the USB transport build; never inject text there:

```text
led-test 1         # only logical HP column 1: three white LEDs, 10 seconds
led-test 14        # middle column
led-test 29        # final column only on a configured 29-column bar
led-test 28        # final column on a configured 28-column bar (27 on old go2_06)
led-test pixel 1   # only raw LED index 1, 10 seconds
led-test pixel 56  # trace the physical row wiring
led-test off       # return immediately to the HP display
```

The diagnostic expires automatically after 10 seconds and does not alter HP.
Record which physical (row, column) lights for logical groups and individual
pixels around each row boundary. On go2_06 the corrected mapping is group 1
→ raw LEDs 1/54/55 and group 27 → 27/28/81.
