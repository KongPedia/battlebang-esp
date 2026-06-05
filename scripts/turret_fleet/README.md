# `scripts/turret_fleet/`

Helper scripts for fleet provisioning, MQTT commands, and release artifacts.

Use the repository venvs:

```bash
./.venv-pio/bin/pio run -e esp32dev_turret_fleet
.venv-turret-tests/bin/python -m pytest tests/python/test_turret_fleet_contract.py -q
```

Helpers:

- `provision.py`: builds/provisions runtime config over USB serial into ESP NVS; by default it overlays `src/turret_fleet/profiles/<turret>.json` and `src/turret_fleet/pattern_presets/<turret>.json` onto dotenv-provided Wi-Fi/MQTT secrets.
- `mqtt_command.py`: publishes command/config payloads without external MQTT tools; use `config --profile-file <json> --patterns-file <json>` to save runtime layout/pattern updates to NVS, and `update --desired-build N` to approve OTA polling against the stable GitHub latest manifest URL.
- `e2e_mqtt_test.py`: runs the live MQTT E2E sequence for `target`, `fire`, `idle`, `dead`, `hold`, and the three readable pattern presets. It skips relay/ESC fire unless `--allow-live-fire` is present and sends a final `hold`.
- `make_release_manifest.py`: generates `manifest.json` for GitHub Releases.
- `publish_mqtt_manifest.py`: publishes a manifest to an OTA MQTT topic without external MQTT tools.

Live E2E example:

```bash
./bin/turret fleet-e2e turret_2 \
  --host "$MQTT_BROKER_HOST" \
  --allow-live-fire \
  --json-report .omx/logs/turret_2-e2e.json
```

Without `--allow-live-fire`, the same harness still checks config, `hold`,
`target`, `idle`, and `dead`, then leaves the turret in `WAIT_COMMAND`.
