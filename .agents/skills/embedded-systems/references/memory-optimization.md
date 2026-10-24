# Memory Optimization

## ESP32 compatibility and authority

Read the [ESP32 boundary and SDK references](../SKILL.md#esp32-compatibility-and-authority) first.
Use the actual selected toolchain and SDK. These review criteria replace platform-specific allocation, raw-flash,
linker and live-stack manipulation recipes; they are not measured budgets or drop-in implementations.
Do not add compiler flags, linker layouts, dependencies or diagnostic hooks from this guide.

## Code Size Optimization

- Inspect the existing selected image/map before changing code. Remove unused work and reuse existing mechanisms first.
- Use read-only data and narrow scope where appropriate; confirm actual placement from the target build rather than
  assuming `const` always removes RAM usage.
- Choose integer widths for validated ranges, units and overflow behavior, not simply the smallest type.
- Do not use packed structs or implementation-defined bit-field layout as a wire/NVS format. Preserve explicit encoding,
  alignment and access requirements. Inlining and optimization flags are not automatically size improvements.

## RAM Optimization

- Prefer existing bounded buffers with an explicit owner and exhaustion policy. Static allocation trades heap/stack
  usage for persistent RAM; it does not make shared access safe.
- Reuse storage only when lifetimes cannot overlap, including callbacks, ISR/task access and reentrancy.
- Large stack locals and unchecked heap allocation both need a measured budget. Do not label one universally safer.
- Do not add a custom pool by copying unchecked allocation/free code. Pointer comparisons against unrelated objects
  do not validate ownership; any pool needs valid-handle checks, synchronization and observable exhaustion/double-free policy.

## Flash Memory Management

- Reuse the owning product's persistence path and
  [ScopedPreferences](../../../../lib/bb_esp_nvs/src/bb_esp_nvs/common_runtime_config_store.h), whose
  [implementation](../../../../lib/bb_esp_nvs/src/bb_esp_nvs/common_runtime_config_store.cpp) manages the open/close lifetime.
  Check open and save results using the existing failure policy; scoped cleanup does not make several writes atomic.
- Preserve namespaces, keys, schema/version validation, defaults and failed-save reporting. Review partial-save and
  power-loss behavior before promising durability or transactional updates.
- Do not write/erase hard-coded flash pages, emulate wear leveling with recursive erase/retry, or erase NVS to repair an
  unexplained failure. Avoid persistence in timing-critical/high-frequency paths without wear and latency analysis.

## Stack Optimization

- Use the installed SDK's supported `uxTaskGetStackHighWaterMark()` diagnostics for task stack headroom when available.
  ESP-IDF reports bytes for this API, unlike vanilla FreeRTOS words; confirm the selected API/version and task lifetime.
- A high-water mark describes observed use, not an upper bound for unvisited paths. Include error/reconnect paths and
  callbacks in separately authorized measurements before reducing a stack budget.
- Prefer bounded iteration over unbounded recursion. Moving a local buffer to static storage requires a lifetime and
  concurrency review, not just a stack-size comparison.

## Data Structure Optimization

- Keep protocol/storage layouts explicit; inspect `sizeof`/alignment in the actual compiler rather than assuming padding.
- A ring buffer needs capacity checks, distinct full/empty state, overflow behavior and an access owner. Index wraparound
  alone does not provide any of these guarantees.
- Reuse the bounded [FrameTxQueue implementation](../../../../firmware/go2_nixo_framed_packet_uart/uart/runtime.cpp)
  for its existing transport responsibility. Its [API](../../../../firmware/go2_nixo_framed_packet_uart/uart/runtime.h)
  exposes admission failure and pending bytes; it is not a thread-safe general-purpose queue.
- Shared flag updates need the same synchronization discipline as other shared state; bit operations are not inherently atomic.

## Memory Monitoring

- Use existing SDK heap/stack diagnostics and selected build/map evidence. Record what was measured and under which load;
  free heap alone does not establish fragmentation, peak stack use or future allocation success.
- Never paint or scan a running stack, infer a task's stack from a linker symbol, or assume a fixed one-kilobyte range.
  Do not overwrite live frames/return state or read outside SDK-owned task boundaries for diagnostics.
- Logging and measurement can change timing and stack use. Keep diagnostics out of ISR and local safety-critical paths.

## Compile-Time Memory Analysis

- Use the selected compiler's supported size assertions for actual fixed-capacity contracts.
- Compile-time layout checks do not validate runtime indices, lengths, ownership or concurrency; retain those boundary checks.
- Host assertions do not establish target ABI, alignment, packing or the firmware's compiler language level.

## Optimization Techniques Summary

1. Identify the actual constrained resource and owning path.
2. Reuse bounded storage and remove unnecessary work before adding allocation machinery.
3. Preserve lifetime, alignment, error handling and wire/NVS compatibility.
4. Compare relevant build artifacts and separately authorized runtime measurements; report the layer not observed.

## Linker Script Customization

Keep the selected ESP partition/linker configuration. Do not copy ARM memory addresses, section layouts or arbitrary
heap reservations. A linker/partition change is application work requiring explicit scope and review of boot, OTA,
persistent storage and every consuming environment; prose maintenance does not authorize it.

## Best Practices

- Optimize from evidence, not generic memory percentages or universal stack sizes.
- Preserve bounds, failure reporting and ownership when reducing allocations or sharing storage.
- Do not change build flags/dependencies or add measurement tooling solely to follow this reference.
- Static review/build size is not runtime headroom or physical safety evidence. Hardware measurements and NVS/device
  operations need separate authorization; this reference grants none.
