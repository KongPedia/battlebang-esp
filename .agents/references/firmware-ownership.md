# Firmware ownership and buffer boundaries

> Locally specialized from the recorded upstream reference; see [source attribution](../skill-sources.json) and
> the retained [license](../licenses/wshobson-LICENSE). Rust/C/desktop teaching examples, throwing/heap/POSIX
> recipes, compiler cleanup extensions and C++20/desktop synchronization skeletons were removed. These criteria guide
> inspection of the existing firmware; they are not compiled examples or a replacement C++ handbook.

## ESP example boundary

Read the selected `cpp-firmware` procedures with the local engineering criteria. Their throwing/heap/POSIX,
C/Rust, GCC cleanup-extension and C++20/desktop worked examples are not ESP implementation recipes. This project
intentionally removed those skeletons; do not restore them under a platform warning or upgrade the target to fit them.
Use the existing scoped owners and bounded storage below, not a language migration, smart-pointer/heap conversion,
nightly/sanitizer/Valgrind requirement, new flags, driver framework or scheduling policy.

## Scoped ownership and failed acquisition

Start at [ScopedPreferences](../../lib/bb_esp_nvs/src/bb_esp_nvs/common_runtime_config_store.h) and its
[implementation](../../lib/bb_esp_nvs/src/bb_esp_nvs/common_runtime_config_store.cpp): the owner is noncopyable,
`begin()` returns the actual open result, and `end()` is guarded by that state. This is an existing concrete RAII seam,
not a requirement to create another generic resource wrapper.

When changing a resource boundary, trace acquisition, partial failure, ordinary use and release. Do not duplicate
ownership, retain a view after its owner dies or add shared ownership just to conceal an unclear lifetime. Destructors
must not throw; operational failure reporting and product safe-off still need their explicit paths. Closing a
Preferences session does not make a sequence of NVS writes transactional or undo a partial save.

Follow the [canonical C++ criteria](engineering.md#c-lifetime-ownership-and-memory) for actual
toolchain/error conventions. No throwing `fstream`, heap-allocated descriptor or blanket no-heap policy is prescribed.

## Queue storage and borrowed views

The framed transport's [FrameTxQueue API](../../firmware/go2_nixo_framed_packet_uart/uart/runtime.h) and
[implementation](../../firmware/go2_nixo_framed_packet_uart/uart/runtime.cpp) own a fixed buffer, reject enqueue
overflow and expose a pointer/length through `peek()`. `consume()` advances the queue by the reported consumption.
Review callers before changing or retaining that view, especially when a writer accepts only part of a chunk.

A fixed capacity, iterator or span does not by itself establish checked access, pointer lifetime or thread safety.
Preserve length/overflow checks before subtraction, casts, indexing and copies; consider zero, boundary and oversized
inputs at the real parser/queue seam. Callback buffers require the same owner/lifetime check before deferred use.
Do not replace this bounded queue with an unbounded container or add a new allocator merely to follow a pattern.

## Shared state without new scheduling policy

Identify which foreground path, task or ISR can access the state. Keep ISR work bounded and use the exact SDK's
task/ISR synchronization APIs when sharing is necessary; disabling interrupts on one core does not protect the other.
Atomic access to one counter does not automatically publish a coherent multi-field frame, and `volatile` supplies
neither atomicity nor ordering.

Use the [repository's timing rules](engineering.md#time-callbacks-and-concurrency) and the selected `cpp-firmware`
plugin's `embedded-systems` RTOS reference, not a desktop `shared_mutex`/thread example.
Memory cleanup must preserve local expiry, inhibit and safe-off before reconnect/network work. A new queue/task is
justified by an actual timing/ownership requirement, not by this reference's presence.

## Evidence proportional to the changed contract

Reuse existing production-code seams and affected env/variant checks. Relevant cases include rejected oversized
frames, queue saturation and partial writes, resource-open failure, callback storage reuse or an interrupted save;
select the cases the actual change can affect rather than generating a full checklist for every task.

The native framed target covers its selected codec/runtime, not all product `main.cpp` safety policy. Builds and host
assertions do not establish physical output state, peak heap/stack use or target timing. Follow
[verification and review](engineering.md#verification-and-review) and report the missing layer.
Documentation-only changes need static checks, not new firmware tests, tool installation or device operations.
