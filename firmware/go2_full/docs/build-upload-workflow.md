# Go2 Full build, upload, and provision flow

For routine application-only USB updates on a provisioned device, use the
[standalone release uploader](../../../docs/usb-release-upload.md): no clone,
Python, virtualenv or PlatformIO is required on the Jetson. The steps below are
for explicitly authorized development or initial provisioning, not that field path.

## 1. Select a build

```bash
./.venv-pio/bin/pio run -e esp32dev_go2_full_uart_2ch
./.venv-pio/bin/pio run -e esp32dev_go2_full_usb_2ch
```

Choose UART2 or USB serial for the Jetson packet link. Both images use the
fixed GPIO22/GPIO23 active-HIGH two-channel relay profile.

## 2. Upload only with device authorization

```bash
./.venv-pio/bin/pio run -e esp32dev_go2_full_uart_2ch \
  -t upload --upload-port /dev/cu.usbserial-XXXX
```

The command above is an example, not permission to flash or actuate hardware.

## 3. Provision runtime config

```bash
cp firmware/go2_full/.env.go2_full.example firmware/go2_full/.env.go2_full
# Edit GO2_FULL_ROBOT_ID, transport, relay variant, stage, Wi-Fi, MQTT, OTA, and tuning.
./.venv-pio/bin/python scripts/go2_full/provision.py \
  --env-file firmware/go2_full/.env.go2_full --no-serial --print-json
./.venv-pio/bin/python scripts/go2_full/provision.py \
  --env-file firmware/go2_full/.env.go2_full \
  --serial-port /dev/cu.usbserial-XXXX
```

USB-framed images reserve USB serial for binary Jetson packets. Manage those
images through Bluetooth SPP, or provision while a UART image is installed and
then flash the final USB image. Keep `GO2_FULL_JETSON_TRANSPORT` and the selected
build environment aligned so OTA metadata describes the installed image.

When provisioning a UART2 service image for a final USB image, pass both
`--jetson-transport usb_serial --management-transport uart2_usb_debug` to the
helper so NVS stores the final USB OTA identity. The maintenance flag declares
that the UART2 service image is already installed; it does not enable ASCII
commands on a production binary USB link. Keep OTA auto-check off until the
selected stable pointer exists. Preserve NVS/calibration and do not power the ESP
5 V rail from both USB and the regulator without verified isolation.
