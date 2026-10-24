# Firmware change specification

Use only when an issue/PR cannot hold the necessary cross-family detail. Remove inapplicable sections; this is not a
mandatory artifact for every small fix. Temporary execution plans belong in ignored `.omx/plans`.

## Outcome and scope
- User-visible outcome and observable acceptance:
- Owning family, explicit PlatformIO env, relay/transport variant and consumers:
- Exclusions (hardware, releases, dependencies, refactors):

## Contract and ownership
- Command/state/resource owner and dependency direction:
- Wire/NVS schema, keys, units/frame, version/replay compatibility:
- External producer/consumer owners, repository/ref and canonical fixtures; pending compatibility evidence:
- Buffer/work/time/resource bounds and failure/overload behavior:
- Safe boot/OFF, lease expiry, inhibit, reset/power-loss and recovery invariants:

## Smallest change
- Existing mechanism/seam reused and files owned:
- Alternatives rejected and compatibility/rollout/migration impact:

## Evidence and rollback
- Failing behavioral regression or characterization and focused offline tests:
- Every affected target env/variant and RAM/flash/timing checks where relevant:
- Docs/contracts/release-filter consumers to synchronize:
- Authorized deployment target/build/hash/config backup and rollback, if in scope:
- Separate human actuation approval and physical acceptance, if in scope:
- Omitted checks, remaining risks and independent reviewer:
