from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = ROOT / "src" / "turret" / "turrets.json"


def _deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    merged = deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = _deep_merge(merged[key], value)
        else:
            merged[key] = value
    return merged


def _effective_motion(config: dict[str, Any], entry: dict[str, Any]) -> dict[str, Any]:
    return _deep_merge(config.get("motion_defaults", {}), entry.get("motion", {}))


def _configured_entries(config: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        turret_id: entry
        for turret_id, entry in config.get("turrets", {}).items()
        if entry.get("configured", False)
    }


def test_configured_turrets_have_effective_motion_profiles() -> None:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))

    assert _configured_entries(config), "at least one turret should be configured"

    required_axis_keys = {
        "adc_low_cut",
        "adc_high_cut",
        "cmd_min_deg",
        "cmd_max_deg",
        "idle_min_deg",
        "idle_max_deg",
        "idle_speed_deg_per_sec",
        "invert_motor",
        "min_drive_us",
    }

    for turret_id, entry in _configured_entries(config).items():
        motion = _effective_motion(config, entry)
        for axis in ("yaw", "pitch"):
            assert axis in motion, f"{turret_id} missing {axis} motion"
            missing = required_axis_keys - set(motion[axis])
            assert not missing, f"{turret_id} {axis} motion missing {sorted(missing)}"

        assert "dead_deg" in motion["pitch"], f"{turret_id} pitch motion missing dead_deg"


def test_idle_and_dead_targets_stay_inside_command_range() -> None:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))

    for turret_id, entry in _configured_entries(config).items():
        motion = _effective_motion(config, entry)
        for axis in ("yaw", "pitch"):
            axis_motion = motion[axis]
            cmd_min = float(axis_motion["cmd_min_deg"])
            cmd_max = float(axis_motion["cmd_max_deg"])
            idle_min = float(axis_motion["idle_min_deg"])
            idle_max = float(axis_motion["idle_max_deg"])

            assert cmd_min < cmd_max, f"{turret_id} {axis} command range is invalid"
            assert cmd_min <= idle_min <= cmd_max, f"{turret_id} {axis} idle_min outside command range"
            assert cmd_min <= idle_max <= cmd_max, f"{turret_id} {axis} idle_max outside command range"

        pitch = motion["pitch"]
        dead = float(pitch["dead_deg"])
        assert float(pitch["cmd_min_deg"]) <= dead <= float(pitch["cmd_max_deg"]), (
            f"{turret_id} dead_deg outside pitch command range"
        )


def test_fleet_default_dead_pitch_is_not_the_upper_stop() -> None:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    pitch = config["motion_defaults"]["pitch"]

    assert float(pitch["dead_deg"]) < float(pitch["cmd_max_deg"])


def test_per_turret_motion_override_merges_with_defaults() -> None:
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    entry = {
        "motion": {
            "pitch": {
                "dead_deg": 12.5,
                "invert_motor": True,
            }
        }
    }

    pitch = _effective_motion(config, entry)["pitch"]

    assert pitch["dead_deg"] == 12.5
    assert pitch["invert_motor"] is True
    assert pitch["cmd_min_deg"] == config["motion_defaults"]["pitch"]["cmd_min_deg"]
    assert pitch["cmd_max_deg"] == config["motion_defaults"]["pitch"]["cmd_max_deg"]
