# BattleBang ESP firmware workspace

Current deployable product code lives under `firmware/`; reusable mechanisms are selectively composed from
`lib/bb_esp_*`. PlatformIO env/source filters, not folder names, decide what compiles. See the [root setup guide](../README.md).

## Find the owning product

| Product path | Explicit env / variant | Current documentation |
| --- | --- | --- |
| `firmware/go2/` | `esp32dev_go2` | [Go2 hit/HP/LED, provisioning and OTA](go2/README.md) |
| `firmware/go2_nixo/` | `esp32dev_go2_nixo[_1ch/_2ch]` | [Integrated newline UART and relay variants](go2_nixo/README.md) |
| `firmware/go2_nixo_framed_packet_uart/` | `esp32dev_go2_nixo_framed_packet_uart[_1ch/_2ch]`, `esp32dev_go2_nixo_framed_packet_usb_1ch/_2ch` | [Framed protocol and UART/USB transport](go2_nixo_framed_packet_uart/README.md); reuses selected Go2/Nixo sources |
| `firmware/boss_target/` | `esp32dev_boss_target` | [Boss Target](boss_target/README.md) |
| `firmware/heavy_blaster/` | `esp32dev_heavy_blaster` | [Heavy Blaster](heavy_blaster/README.md) |
| `firmware/station/` | `esp32dev_station` | [Target Station](station/README.md) |
| `firmware/turret_fleet/` | `esp32dev_turret_fleet` | [Turret Fleet](turret_fleet/README.md), [operator usage](turret_fleet/docs/usage.md) |

Bracket/shorthand env notation above describes alternatives, not literal shell arguments. Use the full name from
`platformio.ini`. The root default is still legacy `esp32dev_turret_5`; never rely on an unqualified `pio run`.
Runtime `robot_id`, `station_id` or `turret_id` belongs in NVS, not a numbered per-device build env.
Go2/Nixo relay pin/polarity/channel count stay in the reviewed build/hardware profile. Turret Fleet already supports
reviewed NVS-backed relay profiles/per-channel polarity; preserve that existing contract and safe-OFF validation.
Do not generalize one product's runtime or build-only hardware policy to every family.

## Development and composition

- Coding-agent entry: [workflow, existing skills and role handoffs](../.agents/README.md),
  [C++ ownership/timing/SRP criteria](../.agents/references/engineering.md).
- Product rules: [firmware/AGENTS.md](AGENTS.md); mechanisms: [lib/AGENTS.md](../lib/AGENTS.md);
  automation: [.github/workflows/AGENTS.md](../.github/workflows/AGENTS.md); checks: [tests/AGENTS.md](../tests/AGENTS.md).
- New family shape and composition: [firmware template](_template/README.md).
- [Migration decision record](MIGRATION_PLAN.md) is a short retained compatibility record, not an unfinished task list.

| Capability library | Owns | Product retains |
| --- | --- | --- |
| `lib/bb_esp_core/` | Common identity/config structs, JSON/string/topic helpers | Product identity and command semantics |
| `lib/bb_esp_nvs/` | Key-mapped Preferences persistence | Namespace/keys, domain validation and config policy |
| `lib/bb_esp_net/` | Wi-Fi retry/connect mechanism | When networking is safe and local timing priority |
| `lib/bb_esp_ota/` | Manifest/transfer/hash/reboot markers | App/hardware/channel and safe-to-update admission |
| `lib/bb_esp_hw/` | Parameterized safe GPIO attach/off | Polarity/profile, relay sequence, cooldown/interlocks |

Use thin product adapters and concrete composition. Select only libraries actually used; do not copy shared implementations
or create an empty MQTT bus/framework. Preserve NVS key maps, errors, calibrated limits, local safe-off and wire contracts.
No universal C++ standard, file-size gate or new RTOS task is implied by this layout.

## OTA release ownership

The unified [Firmware OTA Releases workflow](../.github/workflows/firmware-ota.yml) builds/packages/publishes the active
family/variant matrix. It is not proof of native/Python test execution or successful device boot.
Each variant owns a stable polling pointer and versioned audit artifacts; never use repo-wide `/releases/latest/download/...`.

| Firmware / variant | Stable tag / manifest | Versioned tag prefix |
| --- | --- | --- |
| Go2 | `go2-latest/go2-manifest.json` | `go2-v` |
| Go2/Nixo newline 1ch | `go2-nixo-1ch-latest/go2-nixo-1ch-manifest.json` | `go2-nixo-1ch-v` |
| Go2/Nixo newline 2ch | `go2-nixo-2ch-latest/go2-nixo-2ch-manifest.json` | `go2-nixo-2ch-v` |
| Framed UART 1ch | `go2-nixo-framed-packet-uart-1ch-latest/go2-nixo-framed-packet-uart-1ch-manifest.json` | `go2-nixo-framed-packet-uart-1ch-v` |
| Framed UART 2ch | `go2-nixo-framed-packet-uart-2ch-latest/go2-nixo-framed-packet-uart-2ch-manifest.json` | `go2-nixo-framed-packet-uart-2ch-v` |
| Framed USB 1ch | `go2-nixo-framed-packet-usb-1ch-latest/go2-nixo-framed-packet-usb-1ch-manifest.json` | `go2-nixo-framed-packet-usb-1ch-v` |
| Framed USB 2ch | `go2-nixo-framed-packet-usb-2ch-latest/go2-nixo-framed-packet-usb-2ch-manifest.json` | `go2-nixo-framed-packet-usb-2ch-v` |
| Boss Target | `boss-target-latest/boss-target-manifest.json` | `boss-target-v` |
| Heavy Blaster | `heavy-blaster-latest/heavy-blaster-manifest.json` | `heavy-blaster-v` |
| Station | `station-latest/station-manifest.json` | `station-v` |
| Turret Fleet | `turret-fleet-latest/manifest.json` | `turret-fleet-v` |

The URL prefix is `https://github.com/KongPedia/battlebang-esp/releases/download/`.
Check the committed matrix and product identity header before release work; this table is not a remote-release availability check.
On `main`, product paths and their actual shared-library dependencies select affected rows. Build-affecting scripts and
shared build/release configuration may fan out. Host-only provisioning/MQTT helper changes do not select a release row.
Manual dispatch builds the full matrix and can publish by default. Firmware-folder prose also matches current main-push
filters: inspect that impact before an authorized merge. No dispatch/publish or CI-policy change is authorized by prose cleanup.

## Device configuration and hardware evidence

Ignored `.env.<product>` files are local provisioning inputs; only examples are tracked. Identity, `stage_id`, network,
OTA policy and supported tuning are NVS-backed. Use the owning product's helpers without printing secrets. Do not reuse
old bench port/MAC/ID/ADC/PWM records as current values, and do not erase NVS or calibration as routine troubleshooting.

Flashing several images sequentially on one ESP can demonstrate only those recorded plumbing checks, not simultaneous
fleet behavior or unconnected piezo/LED/relay/servo fixtures. No current physical deployment is asserted by this index.
Upload/NVS/OTA/reboot and actual motion/fire/recovery are separate authorized stages with target-specific evidence.
Boot/reset may itself actuate (for example turret probe/HOME); obtain boot-motion approval or verify output isolation first.
Production USB-framed firmware is binary-only; provision/service it via its documented path, not ASCII on the packet stream.
Hardware assembly, power and wiring references remain in [HardwareKit](../BattleBang-HardwareKit-v1.0.md).

## Legacy compatibility

[The legacy index](../src/README.md) covers `src/hit_target/`, `src/feeder/`, `src/nIxo/`, `src/turret/` and loose sketches.
They are not new active/template targets, but retained envs and electrical/calibration notes are compatibility surfaces.
`hit_target` is unused in the current active firmware scope; its configured env is retained for compatibility only.
Do not remove or revive them without explicit scope and consumer review. Current family docs above are the starting point;
completed execution history belongs in Git/PR history, not additional dated handoff files.
