# BTB-782 standalone ESP UART HP sketch

Standalone ESP32 sketch for the Go2 hit board. It does **not** modify or depend on `firmware/go2` or `firmware/go2_nixo`.

Behavior:

- Reads piezo AO on `D34`/`GPIO34`.
- Drives HP LED bar on `D18`/`GPIO18`, `84` WS2812B LEDs.
- Owns local HP, default `14`.
- Sends newline-delimited `hp_remaining` to Jetson over UART2 every `100 ms`.
- Accepts reset/status commands over USB serial and Jetson UART.

## Wiring

ESP expansion board S/V/G headers:

```text
ESP D17 S  -> Jetson pin 10 RX
ESP D16 S  <- Jetson pin 8  TX
ESP G      -> Jetson pin 6  GND
ESP V      -> do not connect
```

For HP receive-only testing, `D17 S -> Jetson pin 10` plus `G -> Jetson pin 6` is enough.

## Upload

From `battlebang-esp` root:

```bash
scripts/btb782_esp_uart_hp_standalone/upload.sh /dev/cu.usbserial-0001
```

If only one ESP serial device is connected, the script can auto-select it:

```bash
scripts/btb782_esp_uart_hp_standalone/upload.sh
```

## Monitor

```bash
scripts/btb782_esp_uart_hp_standalone/monitor.sh /dev/cu.usbserial-0001
```

USB/Jetson commands:

- `s` / `status` / `show-status`: print JSON status.
- `h` / `hit`: simulate one hit.
- `2` / `r` / `reset`: reset HP to full.

## Jetson UART check

After upload, Jetson should read numeric HP lines on `/dev/ttyTHS1` at `115200` baud, e.g.:

```text
14
14
13
...
0
```
