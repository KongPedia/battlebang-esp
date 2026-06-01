# `app/`

Firmware identity constants for `turret_fleet`.

- `firmware_info.h` defines app/hardware/version/build/Git SHA defaults.
- CI may generate `version_autogen.h` before build; local builds fall back to `0.1.0-dev`.
- Default public release repo: `KongPedia/battlebang-esp`.
- Default latest OTA manifest URL: `https://github.com/KongPedia/battlebang-esp/releases/latest/download/manifest.json`.
- `identity.*` builds the ESP device id from the MAC address.
