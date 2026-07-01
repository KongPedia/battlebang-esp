# Go2 ESP document index

Go2 hit/LED ESP docs live under `firmware/go2/docs/`.

- `build-upload-workflow.md`: generic image build/upload and NVS provisioning flow
- `mqtt-hit-contract.md`: Command Center MQTT topic/payload contract

Key code locations:

```text
firmware/go2/
├─ main.cpp                         # setup/loop runtime orchestration
├─ build_config.h                   # build fallback constants, no per-robot identity
├─ hardware_profile.json            # non-secret hardware fallback profile
├─ .env.go2.example                 # NVS provisioning template
├─ config/                          # runtime config + NVS bridge
├─ display/                         # HP bar renderer
└─ mqtt/                            # hit candidate / heartbeat / display/config/OTA topics
```

Piezo threshold/rearm/capture/debug, hit cooldown, offline queue, LED brightness, identity, stage, Wi-Fi, MQTT and OTA policy are runtime NVS config. The 3ch piezo GPIO pins and LED physical capacity stay build-time hardware profile values.
