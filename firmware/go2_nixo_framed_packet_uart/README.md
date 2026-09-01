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
