# Go2 Full documentation

- `build-upload-workflow.md`: select, build, and provision a transport/relay variant
- `mqtt-hit-contract.md`: existing Command Center MQTT wire contract
- `blaster-fire.md`: Blaster relay mapping and fire command contract

The active implementation is a single tree under `firmware/go2_full`. Jetson
communication is always the version-2 framed packet protocol over UART2 or USB
serial. Runtime NVS uses `go2_full` / `go2_full_hw` and Blaster configuration
keys. Previous stores are imported once; MQTT topic/device identifiers keep
their deployed wire values. See the parent README for migration behavior.
