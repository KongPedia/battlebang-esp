# Go2-Nixo integrated fallback document index

- `build-upload-workflow.md`: generic image build/upload and NVS provisioning flow
- `mqtt-hit-contract.md`: Command Center hit/display MQTT contract
- `nixo-fire.md`: integrated Nixo/game blaster MQTT fire contract and relay variant notes

Key code locations:

```text
firmware/go2_nixo/
├─ main.cpp                         # setup/loop runtime orchestration
├─ build_config.h                   # build fallback constants, no per-robot identity
├─ hardware_profile.json            # hardware fallback profile
├─ variants/                        # relay hardware variants
├─ .env.go2_nixo.example            # NVS provisioning template
├─ config/                          # runtime config + NVS bridge
├─ display/                         # HP bar + Nixo fire-state ring
├─ mqtt/                            # hit/display/config/OTA MQTT bridge
└─ nixo/                            # MQTT Nixo fire command / relay sequence
```

Identity, stage, Wi-Fi, MQTT, OTA, piezo threshold/rearm/capture/debug, hit cooldown, offline queue, local HP count/flash, HP brightness, ring brightness, and Nixo fire duration/prefire/relay-delay timing are NVS runtime config; ESP cooldown is disabled. The 3ch piezo GPIO pins stay in the hardware profile; relay pins, polarity, and channel count stay build variants.
