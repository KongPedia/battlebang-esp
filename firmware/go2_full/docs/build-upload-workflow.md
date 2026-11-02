# Go2 Full build, upload, and provision flow

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
