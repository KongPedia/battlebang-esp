# `scripts/turret_fleet/`

Helper scripts for fleet provisioning, MQTT commands, and release artifacts.

Use the repository venvs:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py -q
```

Helpers:

- `provision.py`: builds/provisions runtime config over USB serial into ESP NVS.
- `mqtt_command.py`: publishes command/config payloads without external MQTT tools.
- `make_release_manifest.py`: generates `manifest.json` for GitHub Releases.
- `publish_mqtt_manifest.py`: publishes a manifest to an OTA MQTT topic without external MQTT tools.
