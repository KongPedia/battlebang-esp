from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_heavy_blaster_uses_hyphenated_product_surface_and_safe_identifiers() -> None:
    assert (ROOT / "src/heavy-blaster").is_dir()
    assert (ROOT / "scripts/heavy-blaster").is_dir()
    assert (ROOT / "src/heavy-blaster/.env.heavy-blaster.example").is_file()

    pio = read("platformio.ini")
    assert "[env:esp32dev_heavy_blaster]" in pio
    assert "+<heavy-blaster/**>" in pio
    assert "-<boss_target/**>" in pio
    assert "-<turret_fleet/**>" in pio

    main = read("src/heavy-blaster/main.cpp")
    assert "namespace heavy_blaster" in read("src/heavy-blaster/config/runtime_config.h")
    assert '#include "heavy-blaster/' in main
    assert "src/heavy_blaster" not in pio


def test_heavy_blaster_defaults_match_prototype_pins_and_effects() -> None:
    header = read("src/heavy-blaster/build_config.h")
    config = json.loads(read("src/heavy-blaster/config.json"))
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
    topics = read("src/heavy-blaster/mqtt/topics.cpp")
    topics_h = read("src/heavy-blaster/mqtt/topics.h")
    bus = read("src/heavy-blaster/mqtt/mqtt_bus.cpp")
    mqtt_cli = read("scripts/heavy-blaster/mqtt_command.py")

    assert 'root + "/devices/" + config.deviceId + "/status"' in topics
    assert 'root + "/heavy-blasters/all/ota"' in topics
    assert 'root + "/heavy-blasters/" + config.blasterId' in topics
    assert 'topics.blasterCommand = base + "/command";' in topics
    assert "blasterStatus" in topics_h
    assert "buildSubscriptionTopics" in topics_h
    assert "publishStatus(\"heartbeat\")" in bus
    assert "publishStatus(\"state_changed\")" in bus
    assert "heavy-blasters" in mqtt_cli
    assert "heavy_blasters" not in topics


def test_heavy_blaster_commands_status_and_relay_safety_contract() -> None:
    controller = read("src/heavy-blaster/blaster/heavy_blaster_controller.cpp")
    controller_h = read("src/heavy-blaster/blaster/heavy_blaster_controller.h")
    bus = read("src/heavy-blaster/mqtt/mqtt_bus.cpp")
    runtime = read("src/heavy-blaster/config/runtime_config.cpp")
    readme = read("src/heavy-blaster/README.md")

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
    env_example = read("src/heavy-blaster/.env.heavy-blaster.example")
    provision = read("scripts/heavy-blaster/provision.py")
    mqtt_command = read("scripts/heavy-blaster/mqtt_command.py")
    gitignore = read(".gitignore")
    readme = read("src/heavy-blaster/README.md")

    assert "HEAVY_BLASTER_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in env_example
    assert "HEAVY_BLASTER_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS" in env_example
    assert "HEAVY_BLASTER_BLASTER_ID=" in env_example
    assert "src/*/.env.*" in gitignore
    assert "DEFAULT_ENV_FILE = PROJECT_ROOT / \"src\" / \"heavy-blaster\" / \".env.heavy-blaster\"" in provision
    assert '"type": "provision"' in provision
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
