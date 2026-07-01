# BTB-782 standalone ESP UART HP sketch

Standalone ESP32 sketch for the Go2 hit board. It does **not** modify or depend on `firmware/go2` or `firmware/go2_nixo`.

Behavior:

- Reads 3 piezo AO channels: left `D34`/`GPIO34`, right `D35`/`GPIO35`, front `D32`/`GPIO32`.
- Drives HP LED bar on `D18`/`GPIO18`, `84` WS2815 LEDs with the legacy Go2 `RGB` color order.
- Owns local HP, default `14`. LED rendering follows the legacy Go2 `BarDisplay`: 28 vertical HP groups × 3 LEDs (`1,56,57`, `2,55,58`, …), healthy green, damaged red, low HP orange, down red blink.
- Sends newline-delimited `hp_remaining` to Jetson over UART2 every `100 ms`.
- Accepts reset/status/fire commands over Jetson UART. USB serial accepts non-fire lines for bench debug only.
- Fires local Nixo relay CH1 on `D23`/`GPIO23` with the existing 1ch Nixo timing defaults.

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

## Jetson UART check

After upload, Jetson should read numeric HP lines on `/dev/ttyTHS1` at `115200` baud, e.g.:

```text
14
14
13
...
0
```

## Commands

Live fire is accepted only from Jetson UART.

- `s` / `status`: print status.
- `h` / `hit`: simulate a local hit.
- `f` / `fire`: fire this robot's local Nixo.
- `x` / `stop-fire` / `fire off`: stop fire.
- `2` / `r` / `reset`: reset HP.
