# C++ firmware engineering criteria

Read the owning source and applicable `AGENTS.md` first. These are review criteria, not a claim every existing path
already complies. Preserve actual hardware behavior and compatibility before improving shape.

## Product boundaries and dependency direction

| Owner | Responsibility | Not its responsibility |
| --- | --- | --- |
| Product `firmware/<family>` | Identity, profiles, domain config, commands, controller/state machine, local safety/OTA admission | Reimplementing shared transport/storage mechanisms |
| `bb_esp_core` | Common structs, JSON/string/topic helpers | Product commands, pins or gameplay state |
| `bb_esp_nvs` | Key-mapped Preferences persistence | Config acceptance policy or automatic schema/identity reset |
| `bb_esp_net` | Connection/retry mechanism | Scheduling priority over local relay/HP safety |
| `bb_esp_ota` | Manifest/transfer/hash/reboot-marker mechanisms | Product's safe-to-update decision or rollout approval |
| `bb_esp_hw` | Parameterized safe attach/off primitives | Flywheel/chain sequence, cooldown or hard envelope policy |
| Host/Command Center | Provisioning, desired state and orchestration | Sole authority for device-local interlocks |

Dependencies point from product adapters to capabilities. Share after a real repeated mechanism is demonstrated; do not
create empty `bb_esp_mqtt`, a mega common library or runtime device-type switch. Concrete composition/free functions are
valid. An interface is justified by a replaceable boundary/test seam, not by a folder template or the word "clean".
Header helpers must not accidentally pull unrelated OTA/Wi-Fi `.cpp`/dependencies into every binary.
Inspect selected env inheritance, source filters, includes, build scripts and release path filters for all consumers.
Framed UART/USB shares Go2/Nixo sources; changing a shared file can affect more than the folder's apparent product.

## C++ lifetime, ownership and memory

- Match the actual selected compiler/framework/dependency versions and adjacent style. The firmware config contains no
  universal explicit `-std=`; host C++17 does not license target C++20 concepts, `span`, coroutines or new toolchains.
- Prefer scoped objects/RAII for resources that have a meaningful acquire/release lifetime. Destructors must not throw;
  report operational failures explicitly before cleanup. Preserve safe initialization and shutdown/error paths.
- Distinguish owners from non-owning pointers/references. Raw non-owning pointers are valid with documented lifetime;
  no gratuitous `shared_ptr`, heap wrapper or `unique_ptr` for a statically owned driver. Resource owners must not be
  accidentally copyable; use the existing lifecycle/API rather than creating a parallel owner.
- Do not assume exceptions/RTTI are enabled or mandate blanket bans/upgrades without checking the target flags.
  Use existing explicit status/error conventions and preserve failures; no catch-all "success" fallback.
- Prefer fixed capacities and explicit overflow/parse rejection at protocol boundaries. Validate lengths before casts,
  indexing/copies and JSON access; respect null termination and ownership of callback buffers.
- Current code uses Arduino `String`/JSON allocation. Avoid new unbounded/per-tick allocation, fragmentation and large
  stack locals; do not rewrite the whole repository to "zero allocation" without measurement and scope.
- For resource-affecting changes compare image RAM/flash and relevant stack/heap headroom; static build size alone does
  not establish runtime peak usage, timing or electrical stability. No invented universal percentage budget.

## Time, callbacks and concurrency

- Preserve local safety/protocol work before reconnect/networking: framed main-loop ordering and `NixoFireClient::tickLocal()`
  deliberately service local work first. Do not insert blocking Wi-Fi/MQTT/HTTP/NVS/delays into a lease/relay critical path.
- Define bounded work, ownership, expiry and safe-off on every return/error path. Existing synchronous gated OTA callbacks
  are evidence to inspect, not proof that every callback is already nonblocking or permission for a broad rewrite.
- Use wrap-safe unsigned elapsed-time/deadline arithmetic consistent with the actual clock and maximum interval. Test
  rollover, zero/maximum lengths, duplicate/conflicting frames, lost ACKs/retries and sender restart/epoch where relevant.
- Add a task/queue only for demonstrated timing/concurrency need. Document one state/resource owner, queue capacity,
  overload policy, shutdown and synchronization. `volatile` does not provide atomicity, mutual exclusion or cross-core order.
- If an ISR is actually compiled/wired, keep it bounded with ISR-safe primitives; no blocking, heap, logging or network/NVS.
  A source file's existence is not proof it is selected by a particular env (Go2's sensor directory is filtered).
- ESP-IDF FreeRTOS differs from vanilla examples: documented task stack parameters use bytes, scheduler startup is already
  managed, and SMP critical sections use platform spinlock/port primitives. Verify exact installed API/version; no ARM IRQ
  intrinsics, parameterless critical sections, `FreeRTOSConfig.h` edits or `vTaskStartScheduler()` transplanted from examples.

## State, protocol and persistence

- Keep byte order, version/flags, CRC, buffer bounds, sender epoch, retries/dedup and ACK meaning explicit. An ACK is not
  proof of physical fire/hit/motion, and a timeout does not prove no side effect. Preserve safe command lease expiration.
- Validate topic segments, app/hardware identity, units, frame IDs, ranges, schema/version and stale commands before effects.
  Rejections stay observable. Preserve non-retained command/replay policy rather than inventing freshness guarantees.
- Domain vocabulary separates desired command, admitted state, configured hardware and observed feedback. Make invalid
  transitions explicit; use selective DDD for invariants, not mandatory aggregates/repositories/event sourcing.
- Preserve NVS namespaces/keys/default fallback, version rejection, masking and failed-save reporting. Shared key maps
  permit mechanism reuse without silently renaming stored fields. Do not erase NVS to cure an unknown failure.
- Multiple Preferences writes are not an established transaction. Review partial save/power-loss behavior and compatibility
  before promising atomic config updates. No new NVS writes on a high-frequency path without wear/timing analysis.
- OTA admission remains product-owned. App/hardware/build, byte count/hash, safe state, marker and failure/abort handling
  must survive extraction. Do not promise image signatures, verified TLS, rollback slots or successful boot unless proven.

## Hardware invariants

Maintain safe OFF before output enable, selected active-HIGH/LOW polarity, physical channel roles/order/delays, calibrated
neutral/deadband/ADC mappings, motion limits, fire inhibit/cooldown, brownout/recovery markers and no automatic fire resume.
Do not move these solely to a server or bypass them to make calibration/CI pass. Historical per-unit values are not defaults.
Turret `target` is aim-only; a pattern's explicit fire step is still an actuation request. No-fire calibration can still move.
Production USB-framed transport is binary-only, unlike UART debug builds. Follow its family provisioning/service path.
Deploy/provision/reboot and actual motion/fire/recovery require separate explicit target-specific authorization and human review.
Deployment/serial reset can itself run a boot probe or HOME. Inspect that behavior before acting and require explicit
boot-motion approval or verified physical output isolation; an upload is not inherently a non-actuating step.

## SRP and size

- A module/class has one coherent reason to change: policy, parsing, scheduling, persistence or hardware effects should not
  be inseparably mixed. Extract at an established boundary; no `Manager`, `Utils` or interface per function by reflex.
- As a review cue, a newly grown function around 50 lines or module around 400 lines merits a responsibility/cohesion check,
  not an automatic failure. Split when independent reasons to change or untestable effects are evident; do not mechanically
  fragment state transitions, calibrated sequences or generated tables to satisfy a count. No new LOC checker is required.
- Put why-not-obvious constraints beside the boundary; long implementation narration and dated task logs belong in Git/PR
  history. Current docs state current behavior; proposals/hardware observations identify their limits.

## Naming conventions

Use the shortest unambiguous name in its owning context, not the shortest spelling or the longest description.
Case is a language/module convention, not a substitute for meaning. These defaults follow active source examples;
respect established local exceptions rather than recasing existing APIs or introducing another style in the same module.

| Surface | Default for new names | Existing example |
| --- | --- | --- |
| Firmware/library folders and C++ files | Short `snake_case` product/capability or responsibility; let folders supply context | `config/runtime_config.h`, `uart/protocol.h`, `lib/bb_esp_ota` |
| C++ types (classes, structs, enums) | `PascalCase`, a domain noun or precise role | `RuntimeConfig`, `Frame`, `FrameTxQueue`, `NixoFireClient` |
| C++ functions/methods and variables | Existing `lowerCamelCase`; action verbs for operations, clear state nouns/predicates | `startFire`, `decodeFrame`, `durationMs`, `isFiring`; private `fireState_` |
| C++ namespaces | Existing short lowercase/`snake_case` domain/capability | `go2`, `battlebang::esp::ota` |
| C++ scoped constants / macros | Existing `kPascalCase` / `UPPER_SNAKE_CASE`; retain required macro prefixes | `kYawPotPin`, `HIT_COOLDOWN_MS` |
| Host Python | Modules/functions/variables `snake_case`, types `PascalCase`, constants `UPPER_SNAKE_CASE` | `parse_dotenv`, `ProvisioningError`, `PROJECT_ROOT` |
| New human/agent prose | Descriptive `kebab-case`; keep standard entrypoint names | `change-spec.md`, `README.md`, `AGENTS.md`, `SKILL.md` |

UART DTOs/callbacks already contain names such as `remaining_ms` and `fire_stop`; their existence does not justify
converting every C++ identifier to snake_case, nor a style-only migration of those established fields to camelCase.

- Do not append the full product, transport, folder and class name to every child identifier. A namespace/type/folder
  should provide reusable context: `uart::Frame`, `FrameTxQueue::enqueue`, `TurretControl::loop` are already clear.
- Long underscore/camel-case chains are both a smell. Remove repeated context, filler (`data`, `object`, `info`, `manager`,
  `helper`) when it adds no distinction; if the name lists independent actions/roles, inspect responsibility before renaming.
  Do not hide that complexity behind an acronym, vague `process()` or a shorter but misleading name.
- No universal character/word-count gate. Review length in context and at call sites; legitimately explicit names such as
  `recoverBrownoutLockoutIfSafe` must retain the failure/safety condition rather than become a vague `recover()`.
- Keep dimensions and distinctions: `durationMs`, `yawTargetDeg`, `remaining_ms`, OFF polarity, UART versus USB and selected
  variant remain explicit wherever context/types do not already make them unambiguous. Use established ADC/GPIO/NVS/OTA/
  UART/CRC vocabulary rather than spelling out every acronym or inventing private abbreviations.
- Use one domain term consistently. Distinguish decoding, validation, persistence and application of config by actual
  behavior; a case conversion cannot fix an ambiguous name or a function whose effects do not match its name.
- Existing envs, paths/includes/namespaces, CLI aliases, JSON/MQTT/NVS keys, profiles and externally used names are
  compatibility surfaces. No blanket rename/recasing. An explicitly scoped rename maps every consumer/test/build/release
  reference and preserves storage/wire compatibility; renaming a persisted key requires a deliberate migration.

Illustrative shortening, **not** a pending source-rename list:

| Repeated context | Clear in the owning context |
| --- | --- |
| `Go2NixoFramedPacketUartFrame` | `uart::Frame` |
| `FrameTxQueue::enqueueFrameIntoFrameTxQueue` | `FrameTxQueue::enqueue` |
| `config/go2_nixo_firmware_runtime_configuration.h` | `config/runtime_config.h` |

## Verification and review

Use the smallest layer that proves the claim: static guidance review → pure production-code host regression → integration
contracts → explicit target builds/resource impact → separately authorized deployment → separately approved physical behavior.
Not every task uses every layer. Guidance-only work stops at static checks; do not invent RED/build/hardware gates for prose.
For fixes observe expected behavior fail, not just an import/fixture error. Characterize before behavior-preserving extraction.
Reuse `test/test_go2_nixo_uart`, `tests/run_tests.sh` and focused `tests/python` contracts as appropriate; do not assume whole
pytest/native commands are safe or equivalent. Hardware/integration flags and operational helpers need explicit inspection.
Shared changes cover all consuming envs/variants and release filters. The current OTA workflow is not a host-test suite.

Independent reviews report severity, file/location, triggering condition, observed evidence and smallest fix; label speculative
concerns as questions. Check reentrancy, expiry/rollover, partial parsing/save, safe boot/reset, offline operation and rollback.
Report exactly which checks ran and which did not. Native model/role/skill selection is not proven by TOML/frontmatter parsing.

## Official compatibility anchors

Use installed toolchain evidence first; these references do not establish this repository's exact framework version.

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines): RAII (R.1), non-owning pointers (R.3), scoped objects (R.5), concurrency/volatile (CP.200).
- [PlatformIO Espressif32](https://docs.platformio.org/en/latest/platforms/espressif32.html): framework/toolchain/version selection; no dependency upgrade is authorized here.
- [Arduino ESP32 migration](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html): core/IDF version changes can break peripheral APIs.
- [ESP-IDF 4.4.7 FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/system/freertos.html) and [SMP critical sections](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/freertos-smp.html#critical-sections): platform-specific stack/scheduler/synchronization rules, not a claim the project pins 4.4.7.
