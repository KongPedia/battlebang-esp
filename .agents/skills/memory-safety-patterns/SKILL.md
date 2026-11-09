---
name: memory-safety-patterns
description: Review C++ firmware resource ownership, RAII, borrowed buffer lifetimes, bounds and shared state when changing ESP32/Arduino code; not a language migration or blanket heap/smart-pointer conversion.
---

# Memory Safety Patterns

> Adapted from the recorded upstream skill for this firmware repository. Cross-language safety rankings,
> desktop/nightly tool commands and unrelated implementation recipes are intentionally omitted.

Use this guidance for the resource or buffer boundary actually changing. Read the owning firmware/library instructions
and selected PlatformIO environment first. Preserve the current compiler, allocation/error conventions and safe-off
behavior; an ownership improvement must not introduce a new failure mode or move product interlocks into a helper.

## What matters here

- **A concrete owner and release path.** Reuse existing scoped owners such as `ScopedPreferences`; decide who closes
  a resource after failed acquisition as well as normal use. Raw non-owning pointers/references are valid when their
  lifetime and bounds are clear. A statically owned driver does not need a heap-backed smart-pointer wrapper.
- **Borrowed data remains borrowed.** Trace callback and queue storage before retaining a pointer or handing it to
  another execution context. An address/length pair or iterator does not prove ownership or checked access.
- **Bounds and failure remain explicit.** Validate external lengths before indexing/copying/allocation and preserve
  the existing rejection/overflow result. Cleanup must not silently turn a failed operation into success.
- **Synchronization follows the real execution path.** Identify the foreground/task/ISR owner and use the target SDK
  primitive where sharing actually occurs. `volatile`, a smart pointer or a desktop mutex recipe is not proof of
  ESP32 cross-core/ISR safety.

The canonical [C++ ownership criteria](../../references/engineering.md#c-lifetime-ownership-and-memory) and
[timing/concurrency criteria](../../references/engineering.md#time-callbacks-and-concurrency) define project policy.
Read [the focused reference](references/details.md) when tracing a concrete owner, borrowed buffer or failure path;
it points at existing production seams rather than providing another driver or allocator template.

## Verification boundary

For authorized firmware changes, use the existing offline seam and affected env/variant checks that prove the changed
contract. Neither C++17 host compilation nor a nearby passing parser test proves target language support, product
timing, stack headroom or physical safe-off. Report those limits according to the owning workflow.

This skill does not authorize tool installation, sanitizer/nightly/Valgrind commands, build-flag changes, exceptions,
C++20 APIs, new tasks or hardware access. Guidance-only work stays with static metadata/reference/hash/diff checks.
