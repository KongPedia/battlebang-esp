# Go2 Nixo relay_2ch variant

BTB-766 integrated fallback profile matching standalone `src/nIxo/variants/relay_2ch`.

- Channel 1 / first on: `GPIO22` — flywheel
- Channel 2 / second on: `GPIO23` — chain
- Inter-channel delay: `150ms`
- Relay polarity: active-LOW (`LOW` = on/fire, `HIGH` = off)
- Normal shutdown order: chain off first, flywheel off last.
