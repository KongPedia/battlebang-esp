# Firmware agent workflow

Start with the latest request and applicable [AGENTS.md](../AGENTS.md), then this guide and only the relevant local skill.
Product/operator docs stay in [firmware/README.md](../firmware/README.md); C++ review criteria live in
[references/engineering.md](references/engineering.md). No second human `docs/` tree is needed.
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
  Do not require hardware to obtain RED, or mislabel mocked/stub compilation as target behavior.
- **DDD:** use firmware vocabulary to clarify invariants: fire lease/inhibit, HP preparation/lock, frame/pose, config
  version and OTA eligibility. Each product owns policy; `bb_esp_*` owns reusable mechanisms. Command Center orchestration
  does not replace local interlocks. No mandatory entity/repository layers, CQRS, event sourcing or domain-framework rewrite.

Specs and docs must distinguish current, proposed and historical behavior. Synchronize changed contracts after implementation;
removing a plan does not prove synchronization. Temporary task plans/review notes go in ignored `.omx/plans`; Git/PR history
holds completed execution logs. Existing test-consumed plan paths are retained as short compatibility records, not live tasks.

## Required skill use for the changed firmware boundary

Use these exact project paths when personal/plugin skills have the same name; do not load duplicates. Codex supports
project `.agents/skills` discovery, but this session's inventory/fresh-session behavior must establish what actually loads.
Direct reading proves inspection only. Repository policy and the actual toolchain override generic source examples.

For substantive implementation or review, **read the relevant entrypoints before editing or reaching a conclusion**.
Choose by the actual boundary: a relay deadline or ISR uses embedded timing guidance; a buffer/Preferences lifetime
uses memory guidance; splitting product policy from a shared mechanism uses architecture guidance. Ordinary requests
are sufficient; users do not need to supply `$skill-name`. Load detailed references only when that boundary needs them.

Keep application visible without ceremony: briefly name the rule that affects the work (for example, preserving local
safe-off before reconnect, or the lifetime of a borrowed transmit buffer), then report the decision and checks that
support the result. Pass the exact skill paths and relevant env/variant constraints into delegated work. Reviewers and
verifiers check those decisions and available read/check evidence, not a claimed list of skill names. Missing selected
guidance is disclosed, not silently replaced by tool installation or a claim of full coverage.

A typo or link correction still needs only its local context and static checks. There is no mandatory multi-agent
pipeline, new planning document, fixed report layout or requirement to read all eight skills. This is a project
instruction and review obligation, not an execution hook or a guarantee that a model will always select correctly.

| Need | Entrypoint | Firmware adaptation |
| --- | --- | --- |
| C++ ownership/RAII | [memory-safety-patterns](skills/memory-safety-patterns/SKILL.md) | Scoped owners, valid non-owning pointers, deterministic cleanup; no assumed heap/exceptions/C++20 |
| Embedded timing/peripherals/RTOS | [embedded-systems](skills/embedded-systems/SKILL.md) | ESP32/Arduino compatibility; do not transplant STM32, vanilla FreeRTOS or ARM examples |
| SRP/layer boundaries | [architecture-patterns](skills/architecture-patterns/SKILL.md) | Concrete product adapters and capability libraries; server/Python examples do not require an ESP framework |
| Behavioral RED/GREEN | [test-driven-development](skills/test-driven-development/SKILL.md) | Offline meaningful assertions, characterization and guidance-only exception above |
| Causal diagnosis | [systematic-debugging](skills/systematic-debugging/SKILL.md) | Compare callers/variants/contracts before patching; never "fix" a guard by disabling it |
| Independent review | [code-review-excellence](skills/code-review-excellence/SKILL.md) | Severity, location, failure condition, evidence and smallest repair |
| Completion claims | [verification-before-completion](skills/verification-before-completion/SKILL.md) | Check the scoped claim; build/ACK/hash is not physical or successful-boot evidence |
| Documentation cleanup | [docs-sync](skills/docs-sync/SKILL.md) | Source-aligned owner docs, preserved calibration, static-only guidance verification |

Domain skills have recorded local corrections for unsafe examples/platform assumptions. Read their compatibility notes,
not only their names. Generic modern-C++ `cpp-pro` was considered but not imported: mandatory C++20/CMake/desktop tools
and problematic ownership examples impose unnecessary adaptation here. Python/frontend bundles are not firmware defaults.
The existing reuse, small-change and SRP criteria already cover the useful simplification discipline; do not add a
separate role or general skill merely to repeat those rules.

ESP-IDF task stack units are bytes for the documented APIs; scheduler startup is managed by the framework. Verify the
installed version/API before using examples. `volatile` is not synchronization; ESP32 SMP needs the correct port primitives.
Do not add tasks, watchdogs, driver frameworks, flags or measurement tooling solely because a skill says "must".
Optional cross-skill mentions are not instructions to install an entire bundle. Do not execute imported scripts/templates
automatically: debugging's `find-polluter.sh` is npm-specific, ignores failing test exits and is not a valid ESP test gate.

## Small role set and handoff

| Responsibility | Existing role | Deliverable |
| --- | --- | --- |
| Scope/integration | Leader; analyst/planner when useful | Env/variant, consumers, safety exclusions, acceptance and evidence |
| Senior firmware/monorepo architecture | [architect](../.codex/agents/architect.toml) | State/resource owners, timing paths and smallest boundary; read-only |
| C++/firmware and helper implementation | [executor](../.codex/agents/executor.toml) + selected skill | Owned files, causal fix, regression/build results and limits |
| Senior engineering/code review | [code-reviewer](../.codex/agents/code-reviewer.toml) | Independent prioritized findings, no self-authored approval |
| Evidence review | [verifier](../.codex/agents/verifier.toml) | Claim → observed result → missing layer; no unauthorized execution |

Use the smallest team with independent outcomes. Give workers ownership, acceptance, exclusions and evidence; tell them
others are present, never revert others' changes, and keep shared edits sequential. One integration owner covers wire/NVS
contracts and every consumer. Children report blockers rather than recursively orchestrating.
Roles reuse responsibilities without fixed models; availability and TOML presence do not prove session registration.
If a role is unavailable, pass its instructions to a default agent inheriting the session model and disclose material
fallbacks. Do not invent a separate "senior" agent for each job title or change model/effort implicitly.

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

[skill-sources.json](skill-sources.json) records full upstream commit revisions, source paths, retained licenses, upstream
and local SHA-256, and explicit modifications. These are selected Git-managed public imports, not plugin cache copies.
For updates compare old upstream → new upstream → local adaptation; preserve referenced resources and license notices.
Resolve annotated tags to commits and verify source paths at that commit. Hashes show content agreement, not source trust
or safe examples. Do not blindly overwrite adaptations or restore upstream hooks/installers.
Review selected reference resources as well as `SKILL.md`: check algorithm bounds, ownership, timeout/failure paths and
actual platform assumptions. Delete unsafe or irrelevant copy recipes instead of retaining them behind a platform warning.
Follow canonical links through templates/owner docs and compare command arguments/defaults to their real parsers/guards;
valid paths and matching hashes alone do not establish safe or usable instructions.

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

Official format references: [local skill discovery](https://learn.chatgpt.com/docs/build-skills) and
[custom agent TOMLs](https://learn.chatgpt.com/docs/agent-configuration/subagents). These document supported formats,
not proof that a fresh session has loaded this checkout.
