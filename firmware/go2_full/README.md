# Go2 Full firmware

`go2_full` combines the Go2 hit/HP display, Blaster blaster relay control, and the
PR #59 framed Jetson protocol in one ESP32 firmware. There is one runtime main:
the previous single-character Jetson protocol is not included.

`ota_supported=true`. The separate `firmware/go2/` firmware remains unchanged;
this directory is only the integrated Go2 Full packet runtime.

## Module map

```text
go2_full/
  piezo/             # Three-channel ADC, qualification, peak capture, rearm
  led/               # HP bar and Blaster status LEDs
  blaster/           # Flywheel/chain two-channel relay controller
  pull_down_button/  # GPIO27 pull-down button and 50 ms debounce
  config/            # Runtime settings, NVS storage and migration
  uart/              # Jetson packet codec/runtime
  mqtt/              # Existing device/hit MQTT contract
  main.cpp           # Initialization, module coordination, HP/network/OTA policy
```

No sensor implementation is imported from `firmware/go2/`. The ADC algorithm
is local to `piezo/`; its pure core accepts samples for host regression tests.
The physical pull-down resistor is not an MCU: `pull_down_button` owns the GPIO27
button input, while main connects its toggle to piezo detection and the LEDs.

## Build variants

| Environment | Jetson link | Relay hardware | OTA channel |
| --- | --- | --- | --- |
| `esp32dev_go2_full_uart_2ch` | UART2 | GPIO22 + GPIO23 | `go2-full-uart-2ch` |
| `esp32dev_go2_full_usb_2ch` | USB serial | GPIO22 + GPIO23 | `go2-full-usb-2ch` |

The generic `esp32dev_go2_full` environment is USB serial with the two-channel
relay profile.

```bash
pio run -e esp32dev_go2_full_uart_2ch
pio run -e esp32dev_go2_full_usb_2ch
```

In a USB build, the USB port is a binary framed-packet link to Jetson. Do not
send line-oriented provisioning or debug commands over that link. Use Bluetooth
SPP for management, or flash a UART build temporarily and use its USB debug
port. UART2 builds use UART2 for Jetson packets and retain USB debug/management.

## Provisioning

Copy the example, fill the robot-specific values, then provision NVS:

```bash
cp firmware/go2_full/.env.go2_full.example firmware/go2_full/.env.go2_full
python scripts/go2_full/provision.py \
  --env-file firmware/go2_full/.env.go2_full \
  --serial-port /dev/cu.usbserial-XXXX
```

Use `GO2_FULL_JETSON_TRANSPORT=uart2` or `usb_serial` to match the final image.
The environment uses `GO2_FULL_*` names. `GO2_FULL_GROUP=go2_nixo` is
intentional: it is the existing MQTT routing group, not the NVS namespace.

The existing gameplay wire contracts remain unchanged:

- MQTT device type: `go2_nixo`
- hit topics: `battlebang/hit/{robot_id}/...`
- Blaster command topic: `battlebang/nixo/{nixo_id}/command`
- firmware role: `integrated_hit_led_nixo`

## Hardware

Shared pins and limits are defined in `hardware_profile.json`.

- HP bar: GPIO18, up to 87 WS2812B addresses
- Blaster state ring: GPIO4, 40 LEDs
- piezo ADC: left GPIO34, right GPIO35, optional front GPIO32
- piezo enable button: GPIO27, active HIGH, `INPUT_PULLDOWN`
- relay: GPIO23 flywheel + GPIO22 chain, active HIGH

The two-channel relay pins and active-HIGH polarity are fixed build-time safety
data in `variants/relay_2ch`. Identity, stage, network, MQTT,
OTA policy, piezo tuning, HP bar tuning, and fire timing are NVS-backed.
`local_secrets.h` is not read by default; use the NVS provisioning path above.

### Piezo enable button

Piezo ADC detection starts OFF after every boot or OTA restart. Wire the button
module as **S -> GPIO27, V -> 3.3 V, G -> GND**. An external pull-down belongs
between S and GND. A press toggles detection after 50 ms debounce; a button held
at boot must be released and pressed again. Toggling clears a pending ADC peak
and requires fresh quiet qualification, while UART HP/fire commands remain
available. The enabled state is deliberately not persisted or remotely armed.

GPIO34 and GPIO35 have no internal pull resistor. Disable absent piezo channels
with `GO2_FULL_PIEZO_CHANNEL_ENABLE_MASK` and use suitable external bias and
protection; the GPIO27 pull-down does not stabilize the ADC inputs.

## Jetson packet protocol

Both Jetson transports carry the same version-2 framed protocol implemented in
`uart/protocol.*` and `uart/runtime.*`. It covers capabilities, fire hold/stop,
HP damage/reset/snapshot, hit events, HP damage guard, match HP maximum config,
link metrics, acknowledgements, and diagnostics. The wire namespace and packet
bytes remain compatible with the PR #59 Go2/Blaster protocol.

The physical button gate affects only piezo ADC hit detection. It does not alter
packet HP damage, relay/fire commands, MQTT, identity, or stored NVS settings.
OTA is deferred while the Blaster relay is firing and resumes only in a safe state.

## OTA identity

Release builds inject the version/build/SHA into `app/version_autogen.h`.
Local fallback version is `0.3.0-local`. Each transport has its own app,
channel, stable tag, and manifest:

- app: `battlebang-go2-full-{uart|usb}`
- hardware: `esp32dev-go2-full-button-relay-2ch-v1`
- channel/tag/manifest prefix: `go2-full-{uart|usb}-2ch`

The default constants in `app/firmware_info.h` represent USB 2-channel. The
UART build environment overrides the transport-specific values. Old app/hardware IDs
are intentionally not accepted as the new firmware identity.

## Blaster settings and NVS migration

Canonical configuration uses `blaster`, `blaster_id` and `blaster_*` keys.
The host helper uses `--blaster-id` / `GO2_FULL_BLASTER_ID`.
`BlasterRuntimeConfig`, `BLASTER_*` build constants and `blaster_*` hardware
profile keys follow the same name. New output and saves do not emit Nixo
configuration keys. Previous `nixo` JSON inputs are accepted only as read-only
migration aliases; canonical Blaster fields win conflicts.

- Runtime NVS: `go2_full`, keys `blaster_id` and `blaster_topic`.
- Hardware calibration NVS: `go2_full_hw`.
- OTA reboot marker: `bb_go2_full`.

On first boot, existing `go2_nixo` / `go2_nixo_hw` settings are imported. Identity,
network, tuning and calibration are preserved. Old OTA destinations are NOT
copied: the new image's channel/manifest is selected, automatic polling is off,
desired build is zero and the old mirror URL is cleared. Confirm the final USB
or UART image's OTA settings before enabling rollout.

The migration marker is written only after both destination stores pass
read-back verification. Interrupted imports retry from the untouched old store.
`clear-config` keeps hardware calibration and this marker, so old settings do
not reappear on reboot. Old namespaces are not automatically erased.

## Verification

```bash
python -m pytest -q tests/python/test_go2_full_contract.py \
  tests/python/test_go2_full_protocol.py
pio run -e esp32dev_go2_full_uart_2ch
pio run -e esp32dev_go2_full_usb_2ch
git diff --check
```

These checks do not flash a device. Hardware upload and physical actuation need
a separate, explicitly authorized validation step.
