# Integrated Go2/Nixo fallback build / upload / provision flow

`firmware/go2_nixo` uses a generic firmware image plus NVS runtime config. PlatformIO envs choose only the relay hardware variant.

## 1. Build variants

```bash
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_1ch
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch
```

The unsuffixed `esp32dev_go2_nixo` env selects 2ch, not 1ch. Use explicit
`_1ch`/`_2ch` envs. The checked-in 2ch profile is GPIO22/GPIO23, active-HIGH
(ON=`1`, OFF=`0`), before optional build-time environment overrides. This is not
physical validation; do not deploy to an active-LOW board without a reviewed
matching profile and verified safe-OFF/output isolation. See the
[hardware and historical per-device notes](../README.md#device-relay-polarity).

## 2. Upload

```bash
./.venv-pio/bin/pio run -e esp32dev_go2_nixo_2ch -t upload --upload-port /dev/cu.usbserial-xxxx
```

## 3. Provision runtime config

```bash
cp firmware/go2_nixo/.env.go2_nixo.example firmware/go2_nixo/.env.go2_nixo
# Edit GO2_NIXO_ROBOT_ID, GO2_NIXO_NIXO_ID, GO2_NIXO_STAGE_ID, Wi-Fi/MQTT/OTA/tuning.
./.venv-pio/bin/python scripts/go2_nixo/provision.py --no-serial --print-json
./.venv-pio/bin/python scripts/go2_nixo/provision.py --serial-port /dev/cu.usbserial-xxxx
```

Runtime config owns identity/stage/network/MQTT/OTA, hit/display tuning including local HP count/flash, and Nixo fire timing/envelope. Hardware profiles own GPIO pins, relay polarity/channel count, and LED physical capacity.
