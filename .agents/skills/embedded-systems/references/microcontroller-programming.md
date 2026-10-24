# Microcontroller Programming

## ESP32 compatibility and authority

Read the [ESP32 boundary and SDK references](../SKILL.md#esp32-compatibility-and-authority) first.
Use the owning firmware's selected Arduino/ESP-IDF APIs, hardware profile and existing driver lifetime.
STM32 registers, CMSIS/NVIC, ARM IRQ intrinsics and hard-coded addresses are not ESP32 implementation guidance.
The criteria below do not authorize new drivers, watchdogs, flags, dependencies, builds or hardware access.

## GPIO Configuration (STM32)

The upstream topic used STM32 GPIO registers; do not transplant them. For this repository:

- Resolve pins, channel roles and active-HIGH/LOW polarity from the selected hardware profile.
- Establish safe OFF before enabling outputs. Do not blanket-reconfigure GPIO banks or assume output-low means OFF.
- Preserve pull configuration, feedback inputs, calibration and shared-pin ownership. Read/modify/write is not a
  cross-core synchronization mechanism; use the installed SDK and existing owner.

## Timer Configuration

- Preserve the product's timing owner, units, local safety priority and wrap-safe elapsed/deadline conventions.
- Bound intervals and work per tick; account for missed deadlines and callback latency before changing scheduling.
- PWM/timer changes can affect actuators. Validate the permitted duty/range, frequency, safe attach/off sequence and
  selected channels; generic duty-cycle values are not calibration or permission to drive hardware.

## External Interrupt (EXTI)

- Use the selected ESP SDK's interrupt installation and ISR-safe APIs, not STM32 EXTI/NVIC setup.
- Clear/acknowledge events according to that driver. Keep handlers bounded; defer parsing, logging, allocation,
  networking and persistence to the established foreground/task owner.
- Establish buffer ownership and synchronization across ISR/task/cores. `volatile` is not atomicity or mutual exclusion;
  masking one core's interrupts does not exclude another core.

## ADC Configuration

- Preserve channel mapping, attenuation, calibration, valid range and filtering from the owning product.
- Use the installed ADC driver and handle timeout/status before accepting a sample; never spin indefinitely waiting for
  a conversion or infer readiness from an arbitrary delay loop.
- For DMA, verify the driver's buffer alignment/capability, length and lifetime constraints. Do not read/reuse a buffer
  before completion or confirmed abort; continuous acquisition needs bounded consumption and overflow reporting.

## UART Communication

- Reuse the owning transport and validate length/CRC/version before effects. Check partial transfers, overflow and timeout.
- Keep local safety work ahead of connection/retry processing and prevent unbounded byte-at-a-time polling.
- Production USB-framed Go2/Nixo is binary-only; do not add strings, serial-monitor diagnostics or provisioning text to
  that application stream. See [communication guidance](communication-protocols.md) for existing bounded queue pointers.

## System Clock Configuration

The framework owns clock setup. Do not copy PLL, flash-latency, prescaler or register values from another MCU.
Any explicitly scoped clock change must account for peripheral timing, SDK constraints, timer behavior and local safety
budgets; changing a reported frequency variable does not configure hardware.

## Watchdog Timer

Preserve the current watchdog policy. Do not add a generic watchdog/reset loop or feed a watchdog to hide blocked work.
Investigate the shared timing/ownership cause and preserve safe-off/recovery behavior. Watchdog configuration and reset
behavior are application changes requiring their own scope and review, not instructions from a generic reference.

## Low-Power Modes

- Use the selected ESP SDK's supported modes and wake sources only after reviewing product eligibility and state retention.
- Sleep/reset can interrupt fire leases, relay control, communications and recovery. Preserve safe OFF and no saved-fire resume.
- Follow [power guidance](power-optimization.md); STM32 stop/standby and ARM sleep instructions are not ESP recipes.

## Best Practices

- Keep one peripheral/state owner and explicit bounds, timeout and failure handling.
- Match actual chip/toolchain/API versions; do not change dependencies or firmware configuration to fit a sample.
- Prefer existing offline production-code seams before separately authorized target builds or device work.
- Builds, ACKs and simulation do not establish GPIO polarity, electrical timing, calibration or physical safety.
