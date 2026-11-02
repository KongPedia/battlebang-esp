"""Transaction and compatibility contracts for Go2 Full NVS migration."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, field
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "firmware/go2_full/config/runtime_config.cpp").read_text(encoding="utf-8")


def function_body(signature: str, next_signature: str) -> str:
    return SOURCE.split(signature, 1)[1].split(next_signature, 1)[0]


@dataclass
class Store:
    old_config: dict = field(default_factory=dict)
    old_hardware: dict = field(default_factory=dict)
    new_config: dict = field(default_factory=dict)
    new_hardware: dict = field(default_factory=dict)


def migrate(store: Store, fail_at: str | None = None) -> bool:
    if store.new_hardware.get("migration_done"):
        return True
    imported = deepcopy(store.old_config)
    imported.update(deepcopy(store.old_hardware))
    imported.update(
        ota_channel="go2-full-usb-2ch",
        ota_manifest="new-manifest",
        ota_mirror="",
        ota_desired_build=0,
        ota_auto_check=False,
    )
    store.new_config.clear()
    store.new_hardware.clear()
    if fail_at == "config":
        return False
    store.new_config.update(imported)
    if fail_at == "hardware":
        return False
    store.new_hardware.update(
        piezo_mask=imported.get("piezo_mask", 3),
        bar_groups=imported.get("bar_groups", 28),
        bar_color=imported.get("bar_color", 0),
    )
    if fail_at == "readback" or store.new_config != imported:
        return False
    if fail_at == "marker":
        return False
    store.new_hardware["migration_done"] = True
    return True


def clear_new_config(store: Store) -> None:
    calibration = {
        key: store.new_hardware[key]
        for key in ("piezo_mask", "bar_groups", "bar_color")
        if key in store.new_hardware
    }
    store.new_config.clear()
    store.new_hardware.update(calibration)
    store.new_hardware["migration_done"] = True


def test_marker_is_committed_after_config_hardware_and_readback() -> None:
    save = function_body("bool saveRuntimeConfigToNvs", "bool clearRuntimeConfigNvs")
    config_write = save.index("saveCommonRuntimeConfig")
    hardware_write = save.index('putUChar("piezo_mask"')
    readback = save.index("runtimeConfigToJson(verified, true)")
    marker = save.index("putBool(kMigrationCompleteKey, true)")
    assert config_write < hardware_write < readback < marker


def test_partial_destination_is_cleared_and_rebuilt_from_legacy_source() -> None:
    store = Store(
        old_config={"robot_id": "go2_06", "blaster_id": "nixo_go2_06", "ota_channel": "old"},
        old_hardware={"piezo_mask": 2, "bar_groups": 27},
        new_config={"robot_id": "partial"},
        new_hardware={"bar_groups": 1},
    )
    assert migrate(store)
    assert store.new_config["robot_id"] == "go2_06"
    assert store.new_config["blaster_id"] == "nixo_go2_06"
    assert store.new_hardware == {
        "piezo_mask": 2,
        "bar_groups": 27,
        "bar_color": 0,
        "migration_done": True,
    }
    assert store.old_config["ota_channel"] == "old"


def test_each_failed_migration_stage_leaves_marker_absent_and_retryable() -> None:
    for stage in ("config", "hardware", "readback", "marker"):
        store = Store(old_config={"robot_id": "go2_03"}, old_hardware={"piezo_mask": 3})
        assert not migrate(store, stage)
        assert not store.new_hardware.get("migration_done", False)
        assert store.old_config == {"robot_id": "go2_03"}
        assert migrate(store)
        assert store.new_hardware["migration_done"] is True


def test_migration_resets_old_ota_identity_but_preserves_device_tuning() -> None:
    store = Store(
        old_config={
            "robot_id": "go2_04",
            "fire_def_ms": 4200,
            "ota_channel": "go2-nixo-2ch",
            "ota_manifest": "old-manifest",
            "ota_mirror": "old-mirror",
            "ota_desired_build": 123,
            "ota_auto_check": True,
        }
    )
    assert migrate(store)
    assert store.new_config["fire_def_ms"] == 4200
    assert store.new_config["ota_channel"] == "go2-full-usb-2ch"
    assert store.new_config["ota_manifest"] == "new-manifest"
    assert store.new_config["ota_mirror"] == ""
    assert store.new_config["ota_desired_build"] == 0
    assert store.new_config["ota_auto_check"] is False


def test_clear_preserves_marker_and_hardware_without_resurrecting_old_config() -> None:
    store = Store(
        old_config={"robot_id": "legacy"},
        new_config={"robot_id": "canonical"},
        new_hardware={"piezo_mask": 2, "bar_groups": 27, "bar_color": 1, "migration_done": True},
    )
    clear_new_config(store)
    assert store.new_config == {}
    assert store.new_hardware == {
        "piezo_mask": 2,
        "bar_groups": 27,
        "bar_color": 1,
        "migration_done": True,
    }
    assert migrate(store)
    assert store.new_config == {}


def test_source_reads_legacy_only_before_marker_and_never_erases_it() -> None:
    load = function_body("bool loadRuntimeConfigFromNvs", "bool saveRuntimeConfigToNvs")
    assert load.index("canonicalStorageReady()") < load.index("kPreviousConfigNamespace")
    assert "clearNamespace(kPreviousConfigNamespace)" not in SOURCE
    assert "clearNamespace(kPreviousHardwareNamespace)" not in SOURCE
    assert load.index("kPreviousConfigNamespace") < load.index("clearNamespace(kConfigNamespace)")


def test_json_outputs_blaster_only_and_accepts_nixo_as_input_alias() -> None:
    apply_json = function_body("bool applyRuntimeConfigJson", "String runtimeConfigToJson")
    write_json = function_body("void writeBlasterRuntimeConfigJson", "}  // namespace")
    assert 'mutableRoot["nixo"]' in apply_json
    assert 'mutableRoot["blaster"]' in apply_json
    assert "Any canonical root or nested spelling overrides every previous spelling" in apply_json
    assert 'createNestedObject("blaster")' in write_json
    assert 'createNestedObject("nixo")' not in write_json
    assert 'root["blaster_id"]' in write_json
    assert 'root["nixo_id"]' not in write_json
    assert 'error = "config JSON exceeds capacity"' in apply_json


def test_serializer_overflow_returns_empty_and_cannot_commit_migration_marker() -> None:
    serializer = function_body("String runtimeConfigToJson", "}  // namespace go2")
    save = function_body("bool saveRuntimeConfigToNvs", "bool clearRuntimeConfigNvs")
    assert "if (doc.overflowed()) return String();" in serializer
    assert "expectedJson.length() == 0" in save
    assert "verifiedJson.length() == 0" in save
    assert save.index("expectedJson.length() == 0") < save.index(
        "putBool(kMigrationCompleteKey, true)"
    )
