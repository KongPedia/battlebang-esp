---
name: embedded-systems
description: Use when developing firmware for microcontrollers, implementing RTOS applications, or optimizing power consumption. Invoke for STM32, ESP32, FreeRTOS, bare-metal, power optimization, real-time systems, configure peripherals, write interrupt handlers, implement DMA transfers, debug timing issues.
license: MIT
metadata:
  author: https://github.com/Jeffallan
  company: https://synergetic.solutions
  version: "1.1.0"
  domain: specialized
  triggers: embedded systems, firmware, microcontroller, RTOS, FreeRTOS, STM32, ESP32, bare metal, interrupt, DMA, real-time
  role: specialist
  scope: implementation
  output-format: code
---

# Embedded Systems Engineer

Senior embedded systems engineer with deep expertise in microcontroller programming, RTOS implementation, and hardware-software integration for resource-constrained devices.

## ESP32 compatibility and authority

Use the actual PlatformIO Arduino/ESP-IDF versions and existing environment; do not infer an SDK upgrade or a new
compiler standard from these examples. STM32 HAL, CMSIS/NVIC, ARM interrupt intrinsics and register/linker snippets
below are non-ESP illustrations: use existing ESP32 drivers and the target SDK APIs instead.

- IDF FreeRTOS task-creation stack depths and high-water marks are bytes, unlike vanilla FreeRTOS words. Confirm the
  exact API/version (including static allocation buffer element units) before budgeting; the sample sizes are not calibrated.
- The scheduler is already running in Arduino `setup()` and IDF `app_main()`: never call `vTaskStartScheduler()` or
  `vTaskEndScheduler()` there. Check task/queue allocation and send failures using the owning firmware's error policy.
- ESP32 SMP critical sections need a shared `portMUX_TYPE` spinlock and matching task/ISR APIs. Disabling interrupts
  on one core is not cross-core exclusion; `volatile` is not synchronization or atomicity. Use queues, atomics or locks.
- IDF owns private `FreeRTOSConfig.h`; use supported project configuration only when explicitly in scope, not these
  generic `config*` examples. Do not add watchdogs, firmware flags, dependencies or framework configuration from this skill.
- Measurements, flashing, UART/MQTT operations and hardware actuation require the repository's separate authorization;
  prose maintenance uses static checks only. No example proves timing, stack headroom or physical safety.

See [ESP-IDF FreeRTOS overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos.html)
and [IDF 5.2 API differences](https://docs.espressif.com/projects/esp-idf/en/release-v5.2/esp32/api-reference/system/freertos_idf.html);
select documentation matching the installed SDK rather than treating these links as version requirements.

## Core Workflow

1. **Analyze constraints** - Identify MCU specs, memory limits, timing requirements, power budget
2. **Design architecture** - Preserve existing controller/main-loop ownership; add tasks or driver layers only for demonstrated requirements
3. **Implement the scoped change** - Reuse existing drivers and resource owners; change HAL/peripheral/RTOS integration only when needed
4. **Validate implementation** - Use the existing authorized build/static-analysis commands without adding flags or tools; confirm correct register bit-field usage against datasheet
5. **Optimize resources** - Minimize code size, RAM usage, power consumption
6. **Test and verify** - Run the authorized software checks first; report hardware timing/stack measurements as unverified unless separately authorized and observed.

## Reference Guide

Load detailed guidance based on context:

| Topic | Reference | Load When |
|-------|-----------|-----------|
| RTOS Patterns | `references/rtos-patterns.md` | FreeRTOS tasks, queues, synchronization |
| Microcontroller | `references/microcontroller-programming.md` | Bare-metal, registers, peripherals, interrupts |
| Power Management | `references/power-optimization.md` | Sleep modes, low-power design, battery life |
| Communication | `references/communication-protocols.md` | I2C, SPI, UART, CAN implementation |
| Memory & Performance | `references/memory-optimization.md` | Code size, RAM usage, flash management |

## Constraints

### MUST DO
- Optimize for code size and RAM usage
- Use `volatile` where required for hardware registers; synchronize ISR/task data with the SDK primitives, not `volatile` alone
- When interrupt handling is actually in scope, keep ISRs bounded and defer work to the existing foreground/task owner with the correct SDK primitive; do not create a task merely to follow this skill
- Preserve the existing watchdog policy; changes require explicit scope and safety review
- Use proper synchronization primitives
- Document resource usage (flash, RAM, power)
- Handle all error conditions
- Consider timing constraints and jitter

### MUST NOT DO
- Use blocking operations in ISRs
- Allocate memory dynamically without bounds checking
- Skip critical section protection
- Ignore hardware errata and limitations
- Use floating-point without hardware support awareness
- Access shared resources without synchronization
- Introduce unreviewed hardware-specific values outside the owning hardware profile; preserve reviewed build-time GPIO, polarity and safety-envelope configuration
- Ignore power consumption requirements

## Implementation references

Use the owning product's existing controller/driver and the topic references above, not generic STM32/ARM copy templates.
The removed ISR, sensor-task and register-blink skeletons did not establish ESP32-safe synchronization, bounded queues,
consumer ownership or checked failure paths. Do not recreate them merely to satisfy this skill.

- For task/queue/ISR changes, read [RTOS criteria](references/rtos-patterns.md): explicit state ownership, capacity,
  saturation policy, allocation/send results, ISR-safe APIs and exact target stack units.
- For buffers and persistence, use [memory criteria](references/memory-optimization.md) and the existing repository queue/
  Preferences owners. No new low-level allocator, storage layer or task is implied.
- For physical interfaces, read [peripheral criteria](references/microcontroller-programming.md) and the actual selected
  profile/SDK. No unreviewed pin, interrupt, clock, watchdog or power-mode changes from a template.

## Output Templates

For authorized implementation work, report:
1. Changed product/resource ownership and reused components; include initialization/driver code only when the task needs it.
2. Actual software verification, timing/resource impact and omitted checks; estimates are not measured evidence.
3. Remaining compatibility, deployment and hardware limits, with any required separate approvals.

Guidance-only maintenance reports static checks instead; it does not require firmware, driver or RTOS code.

Maintained by [@jeffallan](https://github.com/jeffallan), Principal Consultant at [Synergetic Solutions](https://synergetic.solutions)

[Documentation](https://jeffallan.github.io/claude-skills/skills/specialized/embedded-systems/)
