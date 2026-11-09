# RTOS Patterns

## ESP32 compatibility and authority

Read the [ESP32 boundary and SDK references](../SKILL.md#esp32-compatibility-and-authority) first.
Use the selected Arduino/ESP-IDF API/version and existing ownership/scheduling model. Arduino `setup()` and IDF
`app_main()` already run under the scheduler; do not start or end it there. IDF owns private `FreeRTOSConfig.h`.
This guide describes review criteria, not drop-in tasks, hooks or permission to change watchdogs/configuration.

## Task Creation and Management

- Add a task only for a demonstrated timing/concurrency requirement; preserve the owning product's foreground safety work.
- Identify the state/resource owner, lifetime, priority, shutdown and failure policy. Check task creation/allocation status
  before using handles; a partial initialization must not enable outputs or pretend readiness.
- ESP-IDF task-creation stack depths are bytes, unlike vanilla FreeRTOS words. Confirm the exact API/version and static
  buffer element units; choose stack sizes from evidence, not generic examples.
- Use the existing periodic scheduling convention (`vTaskDelayUntil()` where appropriate), with explicit missed-deadline
  behavior and bounded work. Periodic wakeups alone do not prove deadlines are met.

## Queue Communication

- Define item ownership/copy semantics, capacity and full/empty behavior. Check creation and every send/receive result.
- Keep admission failure observable; dropping oldest/newest, retrying or rejecting is product policy, not a generic default.
- Bound waiting where local safety/protocol progress depends on the caller. An indefinite receive is appropriate only
  when that task has no outstanding timed responsibility or required shutdown work.
- Reuse the existing transport/queue before adding another layer; see [communication guidance](communication-protocols.md).

## Mutex and Critical Sections

- Keep one resource owner where possible. Check mutex creation/acquisition and release on every acquired path.
- Use the installed SDK's matching task/ISR primitives. Mutexes are not ISR primitives, and peripheral/network waits
  must not occur inside a spinlock-protected critical section.
- ESP32 SMP critical sections require a shared `portMUX_TYPE` and matching entry/exit APIs across all accessors.
  Single-core interrupt masking and `volatile` do not protect cross-core shared state.
- Preserve ordering and bounded lock hold times; inspect deadlock and priority-inversion risks before adding locks.

## Binary Semaphores (Signaling)

- Create a valid synchronization object before enabling its interrupt producer. Use only the installed port's ISR-safe
  APIs and yield convention; do not transplant an STM32 callback or vanilla port macro signature.
- A binary signal can coalesce repeated events. Use an existing queue/counting mechanism when every event matters,
  and define overload/error handling rather than assuming each interrupt produces one task action.

## Software Timers

- Preserve the current watchdog/reset policy; do not introduce a generic reset callback from this reference.
- Check timer creation/start/stop results and lifetime. Distinguish one-shot from auto-reload behavior explicitly.
- Keep callbacks bounded and nonblocking because they share the timer service context. Defer substantial work to its
  owner and account for timer-command queue failure and delayed callbacks.

## Event Groups

- Create the group before use and distinguish attempted initialization from validated readiness.
- Set readiness bits only after successful initialization/calibration. Define failure, clearing/reinitialization and timeout
  behavior; do not permanently latch readiness across a failed/restarted subsystem.
- Confirm which event APIs are legal in the calling context and handle their status/returned bits.

## Memory Management

- Use installed SDK allocation/diagnostic APIs and existing failure policy, not copied private `config*` definitions or
  generic malloc/stack-overflow hooks. Do not log/allocate from an overflow path whose resources may already be invalid.
- Check creation failures and clean up partially acquired resources using the established owner.
- Use supported heap diagnostics and `uxTaskGetStackHighWaterMark()` where available. ESP-IDF high-water marks use bytes;
  confirm API/version and task lifetime. Observed headroom does not cover unvisited worst-case paths.
- Follow [memory guidance](memory-optimization.md); never paint or scan live stacks to estimate usage.

## Task Notifications (Lightweight Alternative)

- Use a notification only when its single-receiver ownership and count/bit/value semantics match the requirement.
- Establish a valid task handle before enabling the producer. Check operation results where provided; define overflow,
  coalescing, clearing and task-restart behavior.
- Use the installed port's ISR API/yield convention; notifications do not replace payload ownership or synchronization
  of unrelated shared data.

## Best Practices

- Keep ISRs short and foreground safety work timely; do not add tasks, timers or watchdogs without scoped need.
- Preserve bounded work, capacity, shutdown and observable failure handling across all consumers.
- Reuse supported SDK diagnostics, not arbitrary stack thresholds or private framework configuration.
- Offline tests/builds do not establish runtime scheduling, stack headroom or physical safety. Device operations and
  hardware measurements require separate authorization; documentation maintenance does not grant it.
