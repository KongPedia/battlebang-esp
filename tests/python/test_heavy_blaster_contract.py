from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_heavy_blaster_uses_hyphenated_product_surface_and_safe_identifiers() -> None:
    assert (ROOT / "firmware/heavy_blaster").is_dir()
    assert (ROOT / "scripts/heavy_blaster").is_dir()
    assert (ROOT / "firmware/heavy_blaster/.env.heavy-blaster.example").is_file()

    pio = read("platformio.ini")
    assert "[env:esp32dev_heavy_blaster]" in pio
    assert "+<../firmware/heavy_blaster/**>" in pio
    assert "-<../firmware/boss_target/**>" in pio
    assert "-<../firmware/turret_fleet/**>" in pio

    main = read("firmware/heavy_blaster/main.cpp")
    assert "namespace heavy_blaster" in read("firmware/heavy_blaster/config/runtime_config.h")
    assert '#include "heavy_blaster/' in main
    assert "src/heavy_blaster" not in pio


def test_heavy_blaster_defaults_match_prototype_pins_and_effects() -> None:
    header = read("firmware/heavy_blaster/build_config.h")
    config = json.loads(read("firmware/heavy_blaster/config.json"))
    defaults = config["defaults"]
    hw = defaults["hardware_profile"]
    unlock = defaults["unlock"]
    led = defaults["led"]

    assert "TARGET_ID_PREFIX = \"heavy-blaster\"" in header
    assert "FIRMWARE_NAME = \"battlebang-heavy-blaster\"" in header
    assert "static constexpr int RELAY_PIN = 26;" in header
    assert "static constexpr int MATRIX_PINS[kSlotCount] = {23, 22, 21, 19};" in header
    assert "static constexpr bool RELAY_ACTIVE_LOW = false;" in header
    assert "static constexpr uint16_t MATRIX_NUM_LEDS = 64;" in header
    assert "static constexpr uint32_t PRE_UNLOCK_EFFECT_MS = 10000;" in header
    assert "static constexpr uint32_t FADE_OUT_MS = 4000;" in header
    assert "static constexpr uint16_t BLINK_BPM = 40;" in header
    assert "static constexpr uint8_t RAINBOW_SPEED = 8;" in header

    assert hw["matrix_pins"] == [23, 22, 21, 19]
    assert hw["relay_pin"] == 26
    assert hw["relay_active_low"] is False
    assert hw["relay_profile"] == "single_channel_active_high"
    assert led["slot_count"] == 4
    assert led["matrix_width"] == 8
    assert led["matrix_height"] == 8
    assert led["matrix_num_leds"] == 64
    assert led["led_type"] == "WS2812B"
    assert led["color_order"] == "GRB"
    assert unlock["required_slots"] == 4
    assert unlock["pre_effect_ms"] == 10000
    assert unlock["fade_out_ms"] == 4000
    assert unlock["blink_bpm"] == 40


def test_heavy_blaster_mqtt_topics_use_hyphenated_collection() -> None:
    topics = read("firmware/heavy_blaster/mqtt/topics.cpp")
    topics_h = read("firmware/heavy_blaster/mqtt/topics.h")
    bus = read("firmware/heavy_blaster/mqtt/mqtt_bus.cpp")
    main = read("firmware/heavy_blaster/main.cpp")
    mqtt_cli = read("scripts/heavy_blaster/mqtt_command.py")

    assert "#include <bb_esp_core/mqtt/device_topics.h>" in topics
    assert "normalizeRootOrDefault(config.mqttRoot)" in topics
    assert 'makeDeviceTopicsChecked(root, "heavy_blaster", config.deviceId' in topics
    assert 'makeAllOtaTopicChecked(root, "heavy-blasters"' in topics
    assert 'makeEntityTopicsChecked(\n            root, "heavy-blasters", config.blasterId' in topics
    assert "topics.blasterCommand = entityTopics.command;" in topics
    assert "blasterStatus" in topics_h
    assert "buildSubscriptionTopics" in topics_h
    assert "publishStatus(\"heartbeat\")" in bus
    assert "publishStatus(\"state_changed\")" in bus
    assert "#include <bb_esp_ota/http_ota.h>" in bus
    assert "#include <bb_esp_ota/ota_manifest.h>" in bus
    assert "#include <bb_esp_ota/reboot_marker.h>" in bus
    assert "parseManifestJson(payload, manifest, error)" in bus
    assert "shouldApplyManifest(manifest, firmwareIdentity(), reason)" in bus
    assert "runHttpOta(manifest)" in bus
    assert "writeRebootMarker(kOtaRebootNamespace, kOtaRebootKey, true)" in bus
    assert "ESP.restart();" in bus
    assert "not wired" not in bus
    assert "ota_unsupported" not in bus
    assert "#include <bb_esp_ota/http_ota.h>" in main
    assert "#include <bb_esp_ota/ota_manifest.h>" in main
    assert "#include <bb_esp_ota/reboot_marker.h>" in main
    assert 'doc["ota_supported"] = true;' in main
    assert "check-ota [manifest-url]" in main
    assert "pollConfiguredOta" in main
    assert "checkOtaManifestUrlWithPolicy" in main
    assert "commandCenterApprovesPolledOta" in main
    assert "writeRebootMarker(OTA_REBOOT_NAMESPACE, OTA_REBOOT_KEY, true)" in main
    assert "ESP.restart();" in main
    assert "heavy-blasters" in mqtt_cli
    assert "heavy_blasters" not in topics


def test_heavy_blaster_commands_status_and_relay_safety_contract() -> None:
    controller = read("firmware/heavy_blaster/blaster/heavy_blaster_controller.cpp")
    controller_h = read("firmware/heavy_blaster/blaster/heavy_blaster_controller.h")
    bus = read("firmware/heavy_blaster/mqtt/mqtt_bus.cpp")
    runtime = read("firmware/heavy_blaster/config/runtime_config.cpp")
    readme = read("firmware/heavy_blaster/README.md")

    assert "void HeavyBlasterController::forceSafeOff" in controller
    assert "relayWrite(false);" in controller
    assert "relayOn_ = false;" in controller
    assert "config_.hardware.relayActiveLow ? LOW : HIGH" in controller
    assert "config_.hardware.relayActiveLow ? HIGH : LOW" in controller
    assert "allRequiredSlotsActive()" in controller
    assert "if (shouldUnlock && !relayOn_)" in controller
    assert "if (!shouldUnlock && relayOn_)" in controller
    assert "setSlot" in controller_h
    assert "setSlots" in controller_h
    assert "reset(const char* source)" in controller_h

    for command in ("status", "reset", "lock", "set_slot", "set_slots", "unlock", "activate"):
        assert f'"{command}"' in bus
    assert "lastRequestId_ == requestId" in bus
    assert "command_rejected" in bus
    assert "relay_active_low" in runtime
    assert "hardware_profile does not match compiled heavy-blaster board profile" in runtime
    assert "#include <bb_esp_core/config/runtime_config_json.h>" in runtime
    assert 'validateOtaManifestUrl(\n          config.otaPublicManifestUrl, "ota.public_manifest_url", error)' in runtime
    assert 'validateOtaManifestUrl(\n          config.otaLocalMirrorUrl, "ota.local_mirror_url", error)' in runtime
    assert "dest.deviceId = source.deviceId;" in runtime
    assert "dest.blasterId = source.blasterId;" in runtime
    assert "relay is forced OFF at boot/reset" in readme

    for field in (
        'obj["blaster_id"]',
        'obj["slot_count"]',
        'obj["slots_active"]',
        'obj["active_slot_count"]',
        'obj["unlock_ready"]',
        'obj["relay_on"]',
        'obj["mode"]',
    ):
        assert field in controller


def test_heavy_blaster_helpers_and_docs_do_not_require_committed_secrets() -> None:
    env_example = read("firmware/heavy_blaster/.env.heavy-blaster.example")
    provision = read("scripts/heavy_blaster/provision.py")
    mqtt_command = read("scripts/heavy_blaster/mqtt_command.py")
    gitignore = read(".gitignore")
    readme = read("firmware/heavy_blaster/README.md")

    assert "HEAVY_BLASTER_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in env_example
    assert "HEAVY_BLASTER_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS" in env_example
    assert "HEAVY_BLASTER_STAGE_ID=boss_stage_v1" in env_example
    assert "HEAVY_BLASTER_DEVICE_ID=" in env_example
    assert "HEAVY_BLASTER_BLASTER_ID=" in env_example
    assert "src/*/.env.*" in gitignore
    assert "DEFAULT_ENV_FILE = PROJECT_ROOT / \"firmware\" / \"heavy_blaster\" / \".env.heavy-blaster\"" in provision
    assert "help=\"default: firmware/heavy_blaster/.env.heavy-blaster\"" in mqtt_command
    assert '"type": "provision"' in provision
    assert '"device_id": env_first(env, "HEAVY_BLASTER_DEVICE_ID", default="")' in provision
    assert 'doc["device_id"] = doc.get("blaster_id") or ""' in provision
    assert "SERIAL_BOOT_SETTLE_S = 4.0" in provision
    assert "SERIAL_WRITE_CHUNK_BYTES = 96" in provision
    assert "SERIAL_WRITE_CHUNK_DELAY_S = 0.02" in provision
    assert '"matrix_pins": split_csv_ints' in provision
    assert '"relay_active_low": env_bool(env, "HEAVY_BLASTER_RELAY_ACTIVE_LOW", False)' in provision
    assert "publish_mqtt" in mqtt_command
    assert 'choices=("status", "reset", "set-slot", "set-slots", "unlock", "config", "ota", "all-ota")' in mqtt_command
    assert "Command Center publishes slot progress" in readme
    assert "Bluetooth prototype controls are not the production control path" in readme
    assert "## Bench command runbook" in readme
    assert "battlebang/heavy-blasters/heavy-blaster-489D31C0575C/command" in readme
    assert "set-slot --host \"$HOST\" --blaster-id \"$BLASTER_ID\"" in readme
    assert "`UNLOCK_PRE_EFFECT` then `UNLOCKED`" in readme
    assert '"relay_on": true' in readme
    assert '{"command":"reset"}' in readme
