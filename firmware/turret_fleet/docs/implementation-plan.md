# Turret Fleet implementation decision record

This former BTB-721 execution-plan path is retained for existing links/contract readers. The fleet controller and pattern
compiler are implemented; do not delete/recreate them or repeat the old scaffold/rebuild phases.

Current canonical documents:

- [Architecture and local safety ownership](architecture.md)
- [MQTT/HTTP config, command, status and OTA contract](mqtt-http-contract.md)
- [Operator setup, calibration and separately approved operations](usage.md)
- [Release matrix and publication boundaries](github-actions.md)
- [Historical per-unit hardware observations](context.md)
- [Readable pattern decision record](btb-726-readable-patterns-plan.md)

The durable decisions are one generic image with runtime NVS identity/pose/calibration, concrete capability adapters,
local motion/fire admission, a shared frame/unit solver and deterministic primitive pattern execution. The old larger
catalog (including `sweep_vertical`) is not the player-facing pattern allowlist; consult the current wire contract.

OTA identity remains `battlebang-turret-fleet` / `esp32dev-turret-v2`. Two-stage availability/approval is distinct from
applying an update, and download/build/hash is distinct from boot or physical success. Post-OTA boot intentionally stayed
in `WAIT_COMMAND` in the historical observation; this is not a fresh verification claim.

Unique relay/ESC order, coordinate/calibration, NVS and safe-update boundaries from the old plan are consolidated in
architecture/wire/usage docs. Dated implementation phases, branch stop conditions, duplicate config payloads and automatic
upload/fire checklists have been removed. Temporary execution plans belong in ignored `.omx/plans`, not this file.
