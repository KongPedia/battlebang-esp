# BattleBang ESP firmware monorepo guide

## Scope and authority

Applies to the repository unless a deeper `AGENTS.md` adds product-specific rules. Follow the latest user request;
skills, role prompts and hooks cannot widen it or require repeated approval for already authorized work.
Read [.agents/README.md](.agents/README.md) for workflow, existing skills and role handoffs, and
[engineering criteria](.agents/references/engineering.md) for C++ ownership, timing, SRP and safety review.
Human setup and product navigation live in [README.md](README.md) and [firmware/README.md](firmware/README.md).

- Research/review is read-only unless edits are requested.
- Document/prompt/skill/agent-configuration maintenance uses parsing, references, provenance and diff inspection.
  Do not install tools, build firmware, run application tests, start CI, access devices or publish MQTT for prose.
- Firmware, host-script, PlatformIO, dependency, runtime, deployment and release-workflow changes are application work;
  use relevant software checks below. Do not call these prose-only changes.
- Complete clear reversible work. Parallelize bounded independent tasks when useful, with file ownership and evidence;
  keep shared-file edits sequential and reviewers independent. Do not recursively delegate without leader authorization.
- Do not commit, push, create a PR, dispatch workflows or publish releases without authorization. Use existing commit
  conventions; inspect recent history rather than importing another repository's schema.

## Owning paths

| Path | Responsibility |
| --- | --- |
| `firmware/` | Active products: Go2, integrated Go2/Nixo newline and framed UART/USB, Boss Target, Heavy Blaster, Station, Turret Fleet |
| `lib/bb_esp_*` | Capability-scoped core, NVS, network, OTA and hardware mechanisms; not product/game policy |
| `scripts/`, `bin/` | Host provisioning, protocol, operator and release helpers; commands may touch real devices |
| `test/`, `tests/` | Native production-code seams, game-logic harness, Python contracts and separately gated hardware tests |
| `src/` | Retained legacy/reference/compatibility firmware; no new active applications |
| `.github/workflows/` | Unified firmware OTA build/package/publication matrix |

For new active firmware follow `firmware/AGENTS.md`, shared code follows `lib/AGENTS.md`, tests follow `tests/AGENTS.md`,
and release changes follow `.github/workflows/AGENTS.md`. Scope retained `src/` work explicitly before editing it.
Do not remove legacy envs or hardware/calibration notes just because they are not in the release matrix.

`platformio.ini` is the compilation map. Its default remains legacy `esp32dev_turret_5`; always select an explicit env.
Framed builds reuse selected `go2_nixo` sources, so inspect `extends`, filters, build scripts and every consumer.
Firmware uses Arduino ESP32, but the committed config does not pin a universal C++ standard or platform version.
Host C++17 tests do not establish the target compiler's language level. Do not upgrade dependencies/flags for examples.

## Architecture and compatibility

- Product code owns state machines, command admission, GPIO profiles, relay sequencing, hard limits and OTA eligibility.
  Keep safety local even when Command Center validates intent. Share mechanisms, not a generic product command executor.
- Prefer concrete composition, scoped resource lifetime and small testable policy functions. No mandatory inheritance,
  DI framework, rich aggregate, new RTOS task or universal file/function line cap.
- Use the engineering guide's naming conventions: language/module-appropriate case, concise responsibility names and no
  repeated namespace/folder context. Preserve units and safety distinctions; do not blindly extend snake_case identifiers.
- Before broad changes define observable acceptance, selected env/variant, consumers, NVS/wire compatibility and safe
  failure behavior. An issue/PR is enough; use `.agents/templates/change-spec.md` only when useful.
- Behavioral fixes use a failing offline behavioral assertion where practical; refactors use characterization. Python
  string/layout contracts are not compiled C++ behavior, and builds/ACKs do not prove electrical or physical success.
- Preserve timing priority, wrap-safe deadlines, bounded buffers, parser rejection, lease expiry, retry/dedup semantics,
  ownership and safe-off paths. Investigate common causes across callers, variants and tests before a local repair.
- Preserve NVS namespaces/keys, validation, config-version rules and failure reporting. No routine erase/reprovision,
  no saved-fire resume, and no claim that multiple Preferences writes form an atomic transaction.

## Safety and deployment

- Software tests/CI must not flash, open a live serial device, publish to a hardware-connected broker, move or fire.
  Do not inherit `TURRET_RUN_HW_TESTS=1`; use explicit offline tests instead of an unqualified whole-suite invocation.
- Upload, NVS provisioning/erasure, OTA/reboot and live-device queries require separately authorized target-specific
  work. Movement, firing and recovery require explicit actuation approval; a build or upload request is not that approval.
- Upload/provision/reboot and serial-open/reset can themselves trigger boot probe/HOME or other outputs. Before such work,
  inspect the selected firmware and require approval covering boot motion or verified physical output isolation; do not
  assume deployment or a no-fire operation is non-actuating.
- Reidentify the board, env, relay variant/polarity, port, runtime ID, broker and release/hash each time. Historical bench
  ports/IDs/calibration are not current defaults. Preserve calibration and rollback inputs without logging secrets.
- Initialize outputs OFF using the selected polarity; retain cooldown, inhibit, DEAD/unconfigured and brownout guards.
  Reset during fire must not resume saved firing. Preserve recovery markers and safe `WAIT_COMMAND` behavior.
- Turret normal setpoints stay within `motion.limits`; reject invalid frame/config envelopes. Keep relay/ESC order and
  feedback/calibration guards. Do not enable unsafe calibration modes to make a test pass.
- Production USB-framed Go2/Nixo is binary-only: no ASCII `show-status`, serial monitor or provisioning text on its
  application stream. Use its family guide and protocol-appropriate evidence, not a universal serial recipe.
- Do not feed the ESP 5 V rail from both regulator and USB without verified hardware isolation.

## OTA and publication

- Active OTA publishing is one `.github/workflows/firmware-ota.yml` matrix, with firmware/variant-specific stable tags
  and versioned audit artifacts. Do not use repo-wide `/releases/latest/download/...` for device polling.
- App, hardware, build, size/hash and product safe-state gates remain required; do not infer signed manifests, trusted
  TLS, rollback capability or a successful boot from SHA-256 or a passing build alone.
- With `ota.command_center_controlled=true`, polled manifests must match `ota.desired_build` exactly. Direct MQTT `/ota`
  manifests are Command Center-approved jobs, not permission to bypass validation or publish from an agent session.
- Manual dispatch can build the full matrix and publish releases by default. Firmware-folder prose can also match the
  existing main-push filters; inspect publication impact before an authorized push/merge. Do not silently change CI policy.

## Environments and verification

Use existing `.venv-pio` for PlatformIO/serial tools and `.venv-turret-tests` for Python contracts. Environment creation
belongs to requested setup/application work, not prompt maintenance; see README for setup. Ignored secrets include
`firmware/turret_fleet/.env.turret_fleet`, `src/hit_target/.env.hit_target` and product-local `.env.*`/`local_secrets.h`.
Only examples belong in Git. Never print credentials, provision payloads or generated secret headers as evidence.

For authorized application work, select the checks for the changed owner and every affected consumer:

```bash
# Explicit representative product build; substitute the selected env.
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
# Focused offline contracts; select matching files, never hardware/integration suites by accident.
./.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py -q
# Framed protocol changes: real production parser/runtime on the host.
./.venv-pio/bin/pio test -e native_go2_nixo_uart
# Edited host Python scripts: syntax check, without importing/running operational helpers.
python3 -m py_compile <edited_python_files>
git diff --check
```

Shared-library changes build all consuming envs/variants, not only the example above. Review RAM/flash/stack and timing
impact where applicable. The direct game-logic harness is `tests/run_tests.sh`; it needs existing host GoogleTest.
The OTA workflow builds/packages/releases; do not claim it runs native/Python suites unless the workflow changes.

Guidance-only completion requires one-off JSON/TOML/YAML/frontmatter parsing where changed, source/resource/link checks,
instruction-conflict review and `git diff --check`, including newly added files. Do not create checker/test infrastructure
solely for prose. Report fresh-session skill discovery/role loading separately; file presence is not runtime evidence.
Report changed owners, evidence, omitted checks, risk and rollback. Keep proposed/static/software/deployed/physical
claims distinct. Protocol, concurrency, safety and large extractions need independent review; hardware needs human review.

## Document lifecycle

Keep reusable skills in `.agents/skills`, shared local criteria in `.agents/references`, role instructions in `.codex/agents`,
and current product/operator contracts beside their firmware. Do not mirror plugin caches or add dated handoff dumps.
Temporary plans belong in ignored `.omx/plans`, not `.agents/plans`. New prose names use descriptive kebab-case;
existing envs, APIs, NVS/wire keys, code paths and test-consumed document paths are compatibility surfaces, not rename targets.
