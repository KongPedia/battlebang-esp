from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_boss_target_factory_defaults_match_current_four_target_board() -> None:
    config = json.loads(read("src/boss_target/config.json"))
    defaults = config["defaults"]
    hw = defaults["hardware_profile"]

    assert defaults["display_name"] == "Boss Target"
    assert defaults["hp_max"] == 10
    assert defaults["damage_per_hit"] == 1
    assert "3000" not in read("src/boss_target/build_config.h")
    assert defaults["target_count"] == 4
    assert hw["max_targets"] == 4
    assert hw["ring_pins"] == [23, 21, 18, 17]
    assert hw["piezo_do_pins"] == [27, 32, 33, 25]
    assert hw["hp_bar_pin"] == 26
    assert hw["led_type"] == "WS2811"
    assert hw["color_order"] == "RGB"
    assert defaults["ring_num_leds"] == 40
    assert defaults["hp_bar_num_leds"] == 92
    assert defaults["ota_public_manifest_url"].endswith("/boss-target-latest/boss-target-manifest.json")


def test_boss_target_compiled_capacity_is_separate_from_runtime_target_count() -> None:
    header = read("src/boss_target/build_config.h")
    runtime_header = read("src/boss_target/config/runtime_config.h")
    runtime_source = read("src/boss_target/config/runtime_config.cpp")
    controller = read("src/boss_target/target/boss_target_controller.cpp")

    assert "static constexpr uint8_t kMaxTargets = 4;" in header
    assert "static constexpr uint8_t DEFAULT_TARGET_COUNT = 4;" in header
    assert "uint8_t count = ::boss_target::DEFAULT_TARGET_COUNT;" in runtime_header
    assert "config.target.count < 1 || config.target.count > ::boss_target::kMaxTargets" in runtime_source
    assert "uint8_t BossTargetController::targetCount() const" in controller
    assert "random(0, count)" in controller
    assert "for (uint8_t i = 0; i < targetCount(); ++i)" in controller
    assert 'obj["target_count"] = targetCount();' in controller
    assert 'obj["hardware_max_targets"] = ::boss_target::kMaxTargets;' in controller
    assert 'JsonArray targets = obj.createNestedArray("targets");' in controller


def test_boss_target_boot_reset_are_leds_off_until_start() -> None:
    controller = read("src/boss_target/target/boss_target_controller.cpp")
    main = read("src/boss_target/main.cpp")
    mqtt = read("src/boss_target/mqtt/mqtt_bus.cpp")

    assert "mode_ = config_.configured ? Mode::READY : Mode::UNCONFIGURED;" in controller
    assert "clearAllLeds();\n  FastLED.show();\n  emit(\"reset\"" in controller
    assert "void BossTargetController::start" in controller
    assert "mode_ = Mode::ACTIVE;" in controller
    assert "selectNewTarget(millis());" in controller
    assert "if (mode_ == Mode::READY || mode_ == Mode::UNCONFIGURED || otaPrepared_) return;" in controller
    assert 'lower == "start" || lower == "arm" || lower == "activate"' in main
    assert 'strcmp(command, "start") == 0' in mqtt
    assert 'strcmp(command, "reset") == 0' in mqtt
    assert 'strcmp(command, "status") == 0' in mqtt
    assert 'strcmp(command, "disable")' not in mqtt
    assert 'strcmp(command, "enable")' not in mqtt
    assert 'strcmp(command, "stop")' not in mqtt


def test_boss_target_runtime_config_persists_config_not_match_progress() -> None:
    header = read("src/boss_target/config/runtime_config.h")
    source = read("src/boss_target/config/runtime_config.cpp")

    assert "struct RuntimeConfig" in header
    assert "String displayName" in header
    assert "GameplayConfig gameplay" in header
    assert "TargetConfig target" in header
    assert "HpBarConfig hpBar" in header
    assert "HardwareProfileConfig hardware" in header
    assert "String wifiSsid" in header
    assert "String mqttHost" in header
    assert "bool otaCommandCenterControlled" in header
    assert "Preferences prefs" in source
    assert 'prefs.begin("boss_target"' in source
    assert 'prefs.getString("display"' in source
    assert 'prefs.putString("display"' in source
    assert 'prefs.getUShort("hp_max"' in source
    assert 'prefs.putUShort("damage"' in source
    assert 'prefs.getUChar("tgt_count"' in source
    assert 'prefs.putString("wifi_pass"' in source
    assert 'prefs.putString("mqtt_pass"' in source
    assert 'prefs.putUInt("ota_build"' in source
    assert 'doc["display_name"] = config.displayName' in source
    assert 'next.displayName = getStringOr(doc["display_name"], next.displayName);' in source
    assert 'next.displayName = getStringOr(doc["name"], next.displayName);' in source
    assert "hp_remaining" not in source
    assert "active_target_index" not in source
    assert "targetStartedMs" not in source


def test_boss_target_mqtt_topics_status_and_ota_are_firmware_specific() -> None:
    topics = read("src/boss_target/mqtt/topics.cpp")
    bus = read("src/boss_target/mqtt/mqtt_bus.cpp")
    firmware = read("src/boss_target/app/firmware_info.h")
    manifest = read("src/boss_target/ota/ota_manifest.cpp")
    workflow = read(".github/workflows/boss-target-firmware.yml")
    pio = read("platformio.ini")

    assert 'root + "/devices/" + config.deviceId + "/status"' in topics
    assert 'root + "/boss_targets/all/ota"' in topics
    assert 'root + "/boss_targets/" + config.bossId' in topics
    assert 'topics.bossCommand = base + "/command";' in topics
    assert "publishStatus(\"heartbeat\")" in bus
    controller = read("src/boss_target/target/boss_target_controller.cpp")
    assert 'obj["display_name"] = config_.displayName;' in controller
    assert 'obj["name"] = config_.displayName;' in controller
    assert 'obj["hp_remaining"]' in controller
    assert 'BB_BOSS_TARGET_APP_NAME "battlebang-boss-target"' in firmware
    assert 'BB_BOSS_TARGET_HARDWARE "esp32dev-boss-target-ring-v1"' in firmware
    assert "boss-target-manifest.json" in firmware
    assert "manifest.app != BB_BOSS_TARGET_APP_NAME" in manifest
    assert "manifest.hardware != BB_BOSS_TARGET_HARDWARE" in manifest
    assert "manifest.build <= BB_BOSS_TARGET_BUILD" in manifest
    assert "pio run -e esp32dev_boss_target" in workflow
    assert "boss-target-latest" in workflow
    assert "boss-target-v${{ steps.version.outputs.version }}" in workflow
    assert "[env:esp32dev_boss_target]" in pio
    assert "+<boss_target/**>" in pio
    assert "board_build.partitions = min_spiffs.csv" in pio


def test_boss_target_helpers_and_docs_do_not_require_committed_secrets() -> None:
    env_example = read("src/boss_target/.env.boss_target.example")
    provision = read("scripts/boss_target/provision.py")
    mqtt_command = read("scripts/boss_target/mqtt_command.py")
    gitignore = read(".gitignore")
    readme = read("src/boss_target/README.md")

    assert "BOSS_TARGET_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in env_example
    assert "BOSS_TARGET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS" in env_example
    assert "BOSS_TARGET_DISPLAY_NAME=Boss Target" in env_example
    assert "src/*/.env.*" in gitignore
    assert "DEFAULT_ENV_FILE = PROJECT_ROOT / \"src\" / \"boss_target\" / \".env.boss_target\"" in provision
    assert '"type": "provision"' in provision
    assert '"hp_max": env_int(env, "BOSS_TARGET_HP_MAX", 10)' in provision
    assert '"damage_per_hit": env_int(env, "BOSS_TARGET_DAMAGE_PER_HIT", 1)' in provision
    assert 'set_if_present(doc, "display_name", env_first(env, "BOSS_TARGET_DISPLAY_NAME", "BOSS_TARGET_NAME"))' in provision
    assert '"ring_pins": split_csv_ints' in provision
    assert "publish_mqtt" in mqtt_command
    assert 'choices=("start", "reset", "status", "simulate-hit", "config", "ota", "all-ota")' in mqtt_command
    assert "There is no production `disable`, `enable`, `pause`, or `stop` command" in readme
    assert "docs/runtime-config.md" in readme
    assert "`config.json`: committed factory-default/schema reference" in readme
    assert "`hardware_profile` is accepted in provision/config payloads" in readme


def test_boss_target_runtime_config_docs_and_examples_explain_nvs_contract() -> None:
    docs = read("src/boss_target/docs/runtime-config.md")
    config = json.loads(read("src/boss_target/config.json"))
    provision = json.loads(read("src/boss_target/examples/provision.boss-target.example.json"))
    display_update = json.loads(read("src/boss_target/examples/config.display-name.example.json"))
    gameplay_update = json.loads(read("src/boss_target/examples/config.gameplay-target.example.json"))
    start_command = json.loads(read("src/boss_target/examples/command.start.example.json"))
    reset_command = json.loads(read("src/boss_target/examples/command.reset.example.json"))
    status_command = json.loads(read("src/boss_target/examples/command.status.example.json"))

    assert config["documentation"] == "docs/runtime-config.md"
    assert "examples/provision.boss-target.example.json" in config["examples"]
    assert "committed schema/default reference" in config["notes"]
    assert "hardware_profile is validated against the compiled firmware as a compatibility guard" in config["notes"]

    for required in (
        "ESP32 NVS namespace `boss_target`",
        "`src/boss_target/config.json`",
        "`src/boss_target/.env.boss_target.example`",
        "provision {json}",
        "config {json}",
        "battlebang/boss_targets/{boss_id}/config",
        "`boss_id`",
        "`display_name`",
        "`gameplay.hp_max`",
        "`target.count`",
        "`hardware_profile` appears in provision/config payloads",
        "`hp_remaining`",
        "`active_target_index`",
        "not written to NVS",
    ):
        assert required in docs

    assert provision["type"] == "provision"
    assert provision["configured"] is True
    assert provision["display_name"] == "Mini Boss Left"
    assert provision["gameplay"]["hp_max"] == 10
    assert provision["gameplay"]["damage_per_hit"] == 1
    assert provision["target"]["count"] == 4
    assert provision["hp_bar"]["num_leds"] == 92
    assert provision["hardware_profile"]["max_targets"] == 4
    assert provision["hardware_profile"]["ring_pins"] == [23, 21, 18, 17]
    assert provision["hardware_profile"]["piezo_do_pins"] == [27, 32, 33, 25]
    assert provision["wifi"]["password"] == "YOUR_WIFI_PASSWORD"
    assert provision["mqtt"]["host"] == "COMMAND_CENTER_IP_OR_DNS"
    assert provision["ota"]["channel"] == "boss-target"

    assert display_update == {
        "schema": 1,
        "config_version": 2,
        "display_name": "Mini Boss Left",
        "group": "boss-stage",
        "location": "stage-left",
    }
    assert gameplay_update["gameplay"]["hp_max"] == 10
    assert gameplay_update["gameplay"]["damage_per_hit"] == 1
    assert gameplay_update["target"]["count"] == 4
    assert gameplay_update["hp_bar"]["palette"] == ["#00FF00", "#FFFF00", "#FF0000"]
    assert start_command == {"command": "start"}
    assert reset_command == {"command": "reset"}
    assert status_command == {"command": "status"}
