# Nixo relay_2ch variant

BTB-766 two-channel relay profile.

- Channel 1 / first on: `GPIO22` — flywheel
- Channel 2 / second on: `GPIO23` — chain
- Inter-channel delay: `150ms`
- Relay polarity: active-LOW (`LOW` = on/fire, `HIGH` = off)

Field observation showed the previous mapping fired the physical chain before the flywheel. The firing order is therefore GPIO22 flywheel first, then GPIO23 chain after a 0.15s spin-up delay. On normal completion, the chain turns off first and the flywheel turns off last.
