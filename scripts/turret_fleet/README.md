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
- `repeat_lane_sweep_live.py`: repeatedly publishes live `lane_sweep` commands. Add `--boss-id <boss_target_id>` to act like a simple Command Center opening: publish boss `reset`, wait for READY, publish `home` to each configured turret and wait for them to settle, publish boss `start`, wait the 5-second neon-rainbow orbit intro, then begin turret lane sweeps. While running with `--boss-id`, it also watches boss status and publishes `dead` to each turret once boss HP reaches 0.

Live E2E example:

```bash
./bin/turret fleet-e2e turret_2 \
  --host "$MQTT_BROKER_HOST" \
  --allow-live-fire \
  --json-report .omx/logs/turret_2-e2e.json
```

Without `--allow-live-fire`, the same harness still checks config, `hold`,
`target`, `idle`, and `dead`, then leaves the turret in `WAIT_COMMAND`.

Boss-stage lane sweep example:

```bash
./.venv-pio/bin/python scripts/turret_fleet/repeat_lane_sweep_live.py \
  --host "$MQTT_BROKER_HOST" \
  --boss-id boss_target_6809477249D0
```

Opening order with `--boss-id` is: boss HP reset -> turret `home` -> boss neon intro -> lane_sweep. Use `--no-turret-home-first` only when the turrets are already homed and you intentionally want to skip that reset step.
