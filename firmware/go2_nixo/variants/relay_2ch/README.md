# Go2 Nixo relay_2ch variant

BTB-766 integrated fallback profile. The checked-in config differs from the
legacy standalone `src/nIxo/variants/relay_2ch` polarity; do not interchange them.

- Channel 1 / first on: `GPIO22` — flywheel
- Channel 2 / second on: `GPIO23` — chain
- Inter-channel delay: `150ms`
- Checked-in relay polarity: active-HIGH (`HIGH`/`1` = on/fire, `LOW`/`0` = off)
- Normal shutdown order: chain off first, flywheel off last.

These are committed profile defaults, not physical-board verification. Optional
build-time environment defines can change the effective profile. Historical
active-LOW device notes do not match this generic image: do not deploy it to an
active-LOW board without a reviewed matching build-time profile and verified
safe-OFF/output isolation. NVS provisioning cannot change relay polarity.
