# Firmware agent workflow

Start with the latest request and applicable [AGENTS.md](../AGENTS.md), then this guide and only the relevant selected
plugin procedure.
Product/operator docs stay in [firmware/README.md](../firmware/README.md); C++ review criteria live in
[references/engineering.md](references/engineering.md). The root [docs index](../docs/README.md) is navigation, not
a second product/operator source of truth.
For folders/files/types/functions, use the [naming conventions](references/engineering.md#naming-conventions): case alone
does not make a good name; keep it concise in its owning context without dropping units or safety meaning.

## Scope first

Record outcome, owning family/env/variant, affected consumers, exclusions and completion evidence. Inspect source,
PlatformIO filters/build scripts, callers and existing tests before editing. Do not treat every configured env as deployed.

| Task | Workflow | Evidence |
| --- | --- | --- |
| Research/review | Read source and relevant official/upstream material | Locations, confidence and limits; no unrequested edits |
| Docs/prompts/skills | Edit requested guidance; inspect metadata, references and conflicts | Static parsing/hash/link/diff evidence; no firmware execution |
| Behavior fix | Expected behavior → failing host assertion → minimal repair → focused checks | Production-code seam where possible, relevant contracts and target build |
| Refactor | Characterize → extract one responsibility → compare behavior/resources | Preserve ordering, errors, timing, safe-off and consumers |
| Protocol/NVS/shared safety | Small spec → integration owner/consumer map → failure cases → independent review | All variants/consumers, software evidence and explicit physical limits |

PlatformIO/dependencies, runtime config and OTA workflows are application changes even when represented as text.
Skills do not authorize installations, uploads, serial access, MQTT, CI, publishing, commits or repeated approval interviews.
Native tools/subagents suffice; launch an OMX runtime only when requested. Do not change global plugins/hooks/credentials.

## SDD-lite, TDD and selective DDD

- **Specification-driven development (SDD):** for meaningful firmware changes state env/relay variant, command/state owner,
  expected outputs, timing/failure bounds, wire/NVS compatibility and acceptance first. An issue/PR is sufficient for small
  changes; use the [change template](templates/change-spec.md) when cross-family detail merits a separate spec.
  Do not install a spec framework or reverse-specify the whole repository. SDD here is not the similarly abbreviated
  subagent-driven development workflow.
- **TDD:** test pure policy and existing production-code host seams, then the smallest implementation. Framed UART's
  `native_go2_nixo_uart` exercises actual parser/runtime; Python contracts check layout/config and some host-stub syntax.
  Missing fixtures/imports are not behavioral RED. Use characterization for extractions and static review for prose.
  Do not require hardware to obtain RED, or mislabel mocked/stub compilation as target behavior. Preserve existing
  production code, valid characterization, shared helpers and others' edits; do not delete them for a test-first ritual.
- **DDD:** use firmware vocabulary to clarify invariants: fire lease/inhibit, HP preparation/lock, frame/pose, config
  version and OTA eligibility. Each product owns policy; `bb_esp_*` owns reusable mechanisms. Command Center orchestration
  does not replace local interlocks. No mandatory entity/repository layers, CQRS, event sourcing or domain-framework rewrite.

Specs and docs must distinguish current, proposed and historical behavior. Synchronize changed contracts after implementation;
removing a plan does not prove synchronization. Temporary task plans/review notes go in ignored `.omx/plans`; Git/PR history
holds completed execution logs. Existing test-consumed plan paths are retained as short compatibility records, not live tasks.

## Selected plugins and the changed firmware boundary

The project selects `common`, `cpp-firmware` and `robotics` from `konglabs-agent-skills`; the optional
`session-guidance` hook bundle is off. [Plugin setup](plugin-setup.md) describes host-owned provisioning/refresh;
project activation is not installation, trust, discovery or correct model behavior. Reusable bodies/resources are
plugin-delivered, not copied under `.agents/skills`. Repository policy and the actual toolchain override examples.

For substantive implementation or review, **read the relevant local criteria and selected plugin entrypoints before
editing or reaching a conclusion**. Verify the selected plugin identity in the actual session inventory rather than
silently choosing a same-named personal skill or a cache path. Missing or ambiguous discovery is reported, not fixed by
unrequested installation. Direct reading proves inspection only; do not claim automatic fresh-session discovery.

Choose by the actual boundary; ordinary requests suffice and users need not supply a skill name. Load detailed
references only when needed. Briefly name the rule affecting the decision (local safe-off before reconnect, or borrowed
transmit-buffer lifetime), then report the decision and supporting checks. Pass plugin identity/procedure, local
reference and env/variant constraints into delegated work. Review actual decisions/evidence, not a list of skill names.
A typo/link correction needs only local context and static checks; no mandatory pipeline, form or all-skills load.

| Need | Plugin procedure | Local prerequisite / adaptation |
| --- | --- | --- |
| C++ ownership/RAII/buffers | `cpp-firmware`: `memory-safety-patterns` | [Firmware ownership](references/firmware-ownership.md) and [C++ criteria](references/engineering.md#c-lifetime-ownership-and-memory); existing owners, valid borrowed views, no assumed heap/exceptions/C++20 |
| Timing/peripherals/RTOS/power | `cpp-firmware`: `embedded-systems` | [Timing/platform criteria](references/engineering.md#time-callbacks-and-concurrency); ESP32/Arduino API/version, existing foreground/task owner, no transplanted STM32/ARM/vanilla RTOS recipe |
| SRP/policy/capability boundaries | `common`: `architecture-patterns` | [Product boundaries](references/engineering.md#product-boundaries-and-dependency-direction); concrete composition, selective DDD, no mandatory interface/framework/layers |
| Behavioral RED/GREEN | `common`: `test-driven-development` | Offline meaningful production-code assertions, characterization for preserved behavior and static-only guidance |
| Causal diagnosis | `common`: `systematic-debugging` | Compare callers/variants/contracts; never disable a guard to obtain green |
| Independent review | `common`: `code-review-excellence` | [Engineering review](references/engineering.md#verification-and-review); location, trigger, evidence, severity and smallest repair |
| Completion claims | `common`: `verification-before-completion` | Check the scoped claim; build/ACK/hash is not physical or successful-boot evidence |
| Documentation/guidance | `common`: `docs-sync`, `writing-for-agents` for instruction design | Owning firmware/library READMEs, preserved calibration and [static maintenance](#source-maintenance-and-verification) |
| Device command/sensor/I/O contracts | `robotics`: `robotics-software` | Real representation/freshness/authority/reconnect/shutdown boundary; product interlocks remain local, no hardware permission |

The plugin memory examples are **not equivalent** to this project's focused reference: desktop/throwing/heap/POSIX,
C/Rust, GCC-cleanup and C++20/desktop synchronization skeletons remain outside ESP implementation recipes. Preserve the
local exclusion and existing production seams rather than reinstating removed recipes under warnings. Generic
`cpp-pro`, Python/frontend tooling, CMake, new compiler standards or dependencies are not firmware defaults.

ESP-IDF task stack units are bytes for the documented APIs; scheduler startup is framework-managed. Verify exact
API/version and static-buffer units. `volatile` is not synchronization; ESP32 SMP needs the matching port primitives.
For an in-scope ISR, keep it bounded and defer to the **existing foreground/task owner** with SDK primitives. Generic
"defer to tasks" wording never requires a new task. Do not add tasks, watchdogs, driver frameworks, flags or measurement
tools solely because a procedure says "must". Optional cross-skill mentions do not install a bundle. Never execute
plugin scripts/templates automatically: debugging's npm-specific `find-polluter.sh` ignores failing test exits and is
not an ESP test gate.

## Host roles and handoff

Claude's selected Common plugin exports `architect`, `executor`, `code-reviewer` and `verifier` roles. Codex plugins do
not register custom agents: use Codex built-in or otherwise available host agents with the selected Common procedures
and this project's root/engineering/ownership criteria. This checkout intentionally contains no `.codex/agents` TOMLs,
role-prompt copies, exports or symlinks. Plugin export/configuration presence alone does not prove host role loading.

| Responsibility | Handoff | Deliverable |
| --- | --- | --- |
| Scope/integration | Leader; available analyst/planner when useful | Env/variant, consumers, safety exclusions, acceptance and evidence |
| Firmware/monorepo architecture | Read-only architecture review | State/resource owners, timing paths, wire/NVS compatibility and smallest boundary |
| C++/firmware/helper implementation | Scoped implementation owner + selected procedure | Owned files, causal fix, meaningful regression/build evidence and omissions |
| Engineering/code review | Independent read-only reviewer | Prioritized findings with location, trigger, evidence and smallest repair; no self-authored approval |
| Evidence review | Independent verifier | Claim → observed result → missing layer, risk and rollback; no unauthorized execution |

Keep domain policy in [engineering criteria](references/engineering.md), not duplicated role prompts. All handoffs
include applicable instructions, env/variant/consumer ownership and permitted evidence. Use the smallest team with
independent outcomes; tell delegates others are present, never revert their work and keep shared edits sequential.
One integration owner covers wire/NVS contracts and every consumer. Children report blockers, not recursive orchestration.
Reviewers do not author the fixes they approve or publish comments without scope. Preserve the user's task and session
model; disclose unavailable-role fallbacks, use available host agents, and never fabricate native role approval or
change model/effort implicitly. No new local specialization is implied.

## Representative use cases

| Request | Scope | Acceptance |
| --- | --- | --- |
| Fix fire-hold expiry | Framed `main.cpp`, shared `NixoFireClient` and affected variants, debugging/TDD | Regression reaches actual expiry/stop policy; safe-off and affected target builds; no live fire |
| Extract NVS mechanism | Product key maps + shared helper + dependent envs | Keys/defaults/error paths preserved; no erase; builds/size impact, independent review |
| Add firmware family | Existing template, explicit env, runtime IDs, unique OTA identity | Product policy local; selective libs, scripts/contracts/matrix updated together |
| Fix reconnect timing | Local tick before network, bounded work and cancellation/expiry | Offline failure cases; no lease extension/stale command replay or added task by default |
| Consolidate guidance | Catalog/owner docs/test-read paths and source provenance | No stale current claims or missing resources; no firmware builds/tests/CI/hardware |
| Approved deployment | Exact board/variant/build/hash/NVS/rollback plan | Upload/boot evidence separate from actuation; protocol-correct USB evidence and human safety review |

The current `native_go2_nixo_uart` target compiles the codec/runtime, not framed `main.cpp`'s fire-hold expiry. Its HP
damage-guard lease assertion is not a fire-hold regression. A behavioral repair must first reach the real policy through
a minimal production seam or suitable offline adapter; do not credit a nearby passing test as that evidence.
For wire changes, follow the [framed host/fixture handoff](../firmware/go2_nixo_framed_packet_uart/README.md#protocol-consumer-handoff)
and record consumer repository/ref plus fixture evidence, or explicitly report external compatibility pending.

Task briefs can stay in ordinary language: identify the affected env/variant and consumers, the selected local paths,
the contract that must survive, and permitted evidence. For example, a fire-hold repair must preserve local expiry and
safe-off while reaching the actual product policy in an offline regression; a nearby parser test is not a substitute.
Do not turn this into another form to fill out or a reason to request permission again for already authorized work.

## Source maintenance and verification

Reusable procedure bodies/resources and their current payload provenance belong to the selected plugins.
[skill-sources.json](skill-sources.json) retains source/license attribution for surviving project adaptations and hashes
only current retained files. The former copied imports' exact revisions, paths, modifications and hashes remain in the
immutable pre-adoption manifest at `0037fccf6742a1539fc362028c37a229418a71e7:.agents/skill-sources.json`; those skill
paths are historical, not current checkout files. The four retained license notices still apply to derived guidance.
Do not mirror the provider catalog/cache, blindly overwrite local adaptations or restore upstream hooks/installers.

Documentation uses owning firmware/library READMEs and relevant product docs in their existing language; the root docs
index is navigation, not a replacement human-operations tree. Follow canonical links through templates/owner docs and
compare command arguments/defaults to their real parsers/guards **without executing operational helpers**. Compare
pins/polarity/default envs to selected hardware profiles/build definitions; historical per-unit calibration is not a
factory default and electrical settings are not changed merely to match prose.

For protocol changes, record external producer/consumer/fixture owners and repository/ref evidence; local strings or
fixtures do not prove another repository synchronized. Report external compatibility pending when unobserved.
Review selected reference algorithms as well as entrypoints: bounds, ownership, timeout/failure paths and actual platform
assumptions. Matching metadata/hashes/paths do not establish source trust, safe examples or usable instructions. Remove
unsafe or irrelevant copy recipes rather than keeping them behind warnings; retain source/license/adaptation evidence.

Guidance-only work uses one-off JSON/TOML/YAML/frontmatter parsing as relevant, source/local hashes, relative resources,
local links/test-consumed paths, instruction-conflict review and `git diff --check`, including untracked additions.
Use existing parser tools without installing application environments. No permanent checker or checker tests are needed.
Review representative routing and report automatic discovery/model adherence as unverified unless observed fresh.

Application work follows root/owner checks: focused offline host/contracts and explicit target builds; shared changes cover
all consumers/variants. Prevent inherited hardware-test flags and do not run operational helper scripts as smoke tests.
Uploads/NVS writes/OTA/reboot and actuation have separate approval boundaries. Boot/reset can itself move hardware; obtain
boot-motion approval or verify physical output isolation before a deployment/serial reset that could actuate.
Release dispatch can publish; inspect inputs
and path filters before any authorized remote action. No secrets in logs, remote-provider uploads or issue comments without
authorization. Report proposed/implemented/static/software/deployed/physical evidence separately with rollback and limits.

Host provisioning and packaging references are linked from [plugin setup](plugin-setup.md). They document formats and
commands, not proof that a fresh session loaded the selected plugins or registered an agent.
