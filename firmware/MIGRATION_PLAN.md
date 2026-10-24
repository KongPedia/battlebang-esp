# Firmware composition decision record

This former migration-plan path is retained because existing contract tests and links read it. The active family index
is [README.md](README.md), onboarding shape is [_template/README.md](_template/README.md), and agent engineering rules
are [C++ criteria](../.agents/references/engineering.md). This is not an instruction to repeat completed migrations.

## Decision and current scope

Use capability-scoped `bb_esp_*` private libraries plus thin product adapters, not copied NVS/Wi-Fi/OTA stacks, a mega
firmware or a polymorphic framework. Product state machines, hardware profiles, commands and safe-state decisions stay local.

The historical scope lists remain compatibility inputs to existing tests; framed UART/USB is also an active composition
of the integrated Go2/Nixo family (see the current index).

```python
KEPT_FIRMWARE = ["go2", "go2_nixo", "boss_target", "heavy_blaster", "station", "turret_fleet"]
RETIRED_FIRMWARE = ["hit_target", "feeder", "nIxo", "turret"]
```

The folder moves were performed one firmware at a time; these statements refer to repository paths, not physical devices:

- Go2 physical migration is complete: `firmware/go2/`.
- Go2-Nixo physical migration is complete: `firmware/go2_nixo/`.
- Boss Target physical migration is complete: `firmware/boss_target/`.
- Heavy Blaster physical migration is complete: `firmware/heavy_blaster/`.
- Station physical migration is complete: `firmware/station/`.
- Turret Fleet physical migration is complete: `firmware/turret_fleet/`.

Retained `src/` envs are legacy/compatibility, not permission to delete source, calibration or electrical notes.
The root PlatformIO default remains a legacy turret env; select an explicit active env.

## Preserved boundaries

- **NVS-backed common runtime config bridge:** shared typed fields, config-version rejection, topic validation and secret
  masking; products retain namespace/key maps, domain validation, defaults and failure reporting. Multiple writes are not
  an established atomic transaction. Preserve compatibility; do not erase NVS to repair a failed update.
- **host-side provisioning/status scripts:** product-local ignored env input, runtime identity and `stage_id`; no numbered
  device-specific firmware images or credentials in tracked files/logs.
- **real bb_esp_ota OTA:** shared manifest/transfer/hash/reboot-marker mechanism, product app/hardware/build/channel/safe-state
  admission. Firmware-specific stable pointers and versioned artifacts remain separate; a build/hash is not a successful boot.
- **relay pins/polarity/channel count out of runtime config:** this Go2/Nixo boundary keeps electrical defaults in its
  reviewed build/hardware profile. Turret Fleet's existing validated NVS relay-profile/per-channel-polarity settings are a
  separate product contract, not forbidden by that rule. Products own sequencing, safe-off, cooldown and inhibit;
  shared `bb_esp_hw` provides safe attach/off primitives.
- **Nixo fire timing/envelope NVS tuning:** only supported validated runtime fields may change; preserve existing safety
  bounds and relay-variant semantics. OTA/network work must not starve local relay timing.
- Share topic/JSON mechanisms, not domain collection names or fire/HP policy. Stage metadata does not establish wildcard
  subscription behavior. Preserve `src/common/relay_pin_utils.h` compatibility include users until separately migrated.
- Add `bb_esp_mqtt` only when a repeated PubSubClient JSON/device bus pattern is ready; do not add an empty abstraction.
- Keep only required capability libraries linked; review every env/variant consumer and RAM/flash impact for shared changes.

## Change and evidence policy

See [root AGENTS.md](../AGENTS.md) for scoped checks. Behavior changes need relevant offline regression/contracts and
explicit target builds. Prose-only maintenance needs static metadata/reference/provenance/diff review, not application runs.
Previous bench uploads do not establish current board identity or physical behavior. Deploy/provision/reboot and actuation
require separate authorization, calibration preservation and a target-specific rollback path. Historical execution phases,
branch checkpoints and local port/ID tables have been removed rather than kept as current instructions.
