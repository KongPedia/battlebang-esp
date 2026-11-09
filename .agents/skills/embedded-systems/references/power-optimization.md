# Power Optimization

## ESP32 compatibility and authority

Read the [ESP32 boundary and SDK references](../SKILL.md#esp32-compatibility-and-authority) first.
Use the selected ESP chip/SDK, board profile and product safe-state policy. This guide retains power-design concepts,
not STM32 register recipes, uncalibrated current tables or universal sleep/battery thresholds.
Do not change clocks, watchdogs, dependencies, flags or firmware configuration solely to follow this reference.

## Sleep Mode Strategy

- Compare idle duration, entry/exit cost, retained state and required wake latency before choosing a supported SDK mode.
- Preserve the product's local interlocks, fire lease timing, safe OFF and no automatic fire resume. Sleep eligibility is
  product policy; a long idle interval is not sufficient evidence.
- Do not disable scheduler ticks or call ARM sleep instructions directly. Account for communications, timer deadlines,
  wake/reset behavior and recovery before proposing a sleep change.

## Dynamic Clock Scaling

Use existing SDK power-management behavior. Do not replace clock configuration with raw PLL writes or a guessed load
counter. An explicitly scoped change must preserve timer/peripheral behavior, watchdog expectations and worst-case
local safety latency; review the installed SDK's supported transitions and locks first.

## Peripheral Power Management

- Disable a peripheral only through its established owner after confirming no active transfer, DMA, callback or consumer
  still owns it. Elapsed inactivity alone does not prove it is unused.
- Define restoration, failed-wakeup and safe-output behavior before gating a clock or supply. Shared buses, USB and
  communications may be required for recovery even when application traffic is idle.
- Keep reconnect and power transitions bounded so local relay/lease/HP processing remains timely.

## GPIO Power Optimization

- Change only pins proven unused by the selected board/profile and boot/recovery path.
- Preserve output polarity and safe OFF, wake sources, feedback and calibration. Analog/input/output-low choices depend
  on circuitry; no whole-bank rewrite is safe by default.
- Review held/retained state and restoration across every supported transition, not just the normal wake path.

## ADC Power Optimization

Use the selected SDK's ADC lifecycle and calibration. Bound conversion waits and reject failed/out-of-range readings;
an arbitrary delay is not a readiness check. Duty-cycle sampling only if required feedback and safety detection latency
remain valid, and preserve DMA/consumer ownership during shutdown and restart.

## Battery Monitoring

- Use the actual board divider, ADC calibration, battery chemistry and product thresholds, not STM32 calibration addresses
  or a generic cell-voltage table.
- Validate readings before conversion/division and define invalid-sensor behavior. Voltage alone does not establish a
  reliable state-of-charge estimate under varying load/temperature.
- Product policy owns low-battery behavior; reducing sampling, clock speed or features must not disable local interlocks
  or hide a failing power supply. Preserve brownout/recovery handling.

## RTC Wakeup

Use the installed ESP SDK's supported wake source and time units. Confirm retained state and whether waking resumes or
reboots the selected mode. Keep ISR work bounded and defer application work; do not copy STM32 RTC/EXTI/NVIC setup or
perform arbitrary periodic work directly in a wake interrupt.

## Power Measurement

- Keep estimates separate from observed current/energy. Include board, supply, active peripherals, wireless state,
  duty cycle, duration and peak behavior; CPU frequency alone is not a system power model.
- Reuse existing build/runtime evidence where available. Do not add measurement tooling, run device helpers, alter
  firmware or claim measured savings during documentation maintenance.
- Physical measurement needs separately authorized target-specific work and a safe electrical setup. Boot/reset may
  actuate outputs; preserve the repository's approval/isolation rules.

## Best Practices

- Optimize the measured dominant load while preserving local safety and recovery behavior.
- Batch work only within protocol freshness, lease and response-time requirements.
- Treat DMA, sleep, clock scaling and peripheral gating as trade-offs, not unconditional improvements.
- Static review/builds do not prove power savings, calibrated limits, wake reliability or physical safety.
