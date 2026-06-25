from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOSS_HP_BAR_GROUP_COUNT = 100
BOSS_HP_BAR_LEDS_PER_GROUP = 3
BOSS_HP_BAR_LED_COUNT = BOSS_HP_BAR_GROUP_COUNT * BOSS_HP_BAR_LEDS_PER_GROUP


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def boss_hp_bar_group_indices(group_1_based: int) -> tuple[int, int, int]:
    """Mirror the boss HP bar's three-row serpentine column grouping."""
    return (
        group_1_based - 1,
        2 * BOSS_HP_BAR_GROUP_COUNT - group_1_based,
        2 * BOSS_HP_BAR_GROUP_COUNT - 1 + group_1_based,
    )


def test_boss_target_factory_defaults_match_current_four_target_board() -> None:
    config = json.loads(read("firmware/boss_target/config.json"))
    defaults = config["defaults"]
    hw = defaults["hardware_profile"]

    assert defaults["display_name"] == "Boss Target"
    assert defaults["hp_max"] == 10
    assert defaults["damage_per_hit"] == 1
    assert "static constexpr uint16_t HP_MAX = 3000;" not in read("firmware/boss_target/build_config.h")
    assert defaults["target_count"] == 4
    assert hw["max_targets"] == 4
    assert hw["ring_pins"] == [23, 21, 18, 17]
    assert hw["piezo_do_pins"] == [34, 27, 32, 33]
    assert hw["hp_bar_pin"] == 12
    assert hw["led_type"] == "WS2811"
    assert hw["color_order"] == "RGB"
    assert defaults["ring_num_leds"] == 120
    assert defaults["hp_bar_num_leds"] == 300
    assert defaults["target_active_color"] == "#FF0000"
    assert defaults["led_brightness"] == 255
    assert defaults["led_max_ma"] == 12000
    assert BOSS_HP_BAR_LED_COUNT == defaults["hp_bar_num_leds"]
    assert "100 vertical HP columns across 3 horizontal rows" in config["notes"]
    assert defaults["ota_public_manifest_url"].endswith("/boss-target-latest/boss-target-manifest.json")


def test_boss_target_compiled_capacity_is_separate_from_runtime_target_count() -> None:
    header = read("firmware/boss_target/build_config.h")
    runtime_header = read("firmware/boss_target/config/runtime_config.h")
    runtime_source = read("firmware/boss_target/config/runtime_config.cpp")
    controller = read("firmware/boss_target/target/boss_target_controller.cpp")

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


def test_boss_target_boot_reset_show_dim_white_hp_bar_until_start() -> None:
    controller = read("firmware/boss_target/target/boss_target_controller.cpp")
    main = read("firmware/boss_target/main.cpp")
    mqtt = read("firmware/boss_target/mqtt/mqtt_bus.cpp")

    assert "mode_ = config_.configured ? Mode::READY : Mode::UNCONFIGURED;" in controller
    assert 'clearAllLeds();\n  renderIdleHpBar();\n  FastLED.show();\n  emit("reset"' in controller
    assert "static constexpr uint8_t HP_BAR_IDLE_WHITE_PERCENT = 30;" in read("firmware/boss_target/build_config.h")
    assert "static constexpr uint8_t HP_BAR_IDLE_WHITE_VALUE = 77;" in read("firmware/boss_target/build_config.h")
    assert "void BossTargetController::renderIdleHpBar()" in controller
    assert "setHpBarAll(CRGB(::boss_target::HP_BAR_IDLE_WHITE_VALUE" in controller
    assert "void BossTargetController::ledTest" in controller
    assert "void BossTargetController::setHpBarLinear" in controller
    assert 'strcmp(command, "led_test") == 0' in mqtt
    assert 'obj["hp_bar_pin"] = config_.hardware.hpBarPin;' in controller
    assert 'obj["led_test_active"]' in controller
    assert "void BossTargetController::start" in controller
    assert "static constexpr uint32_t START_INTRO_MS = 5000;" in read("firmware/boss_target/build_config.h")
    assert "static constexpr uint32_t START_INTRO_HUE_STEP_MS = 3;" in read("firmware/boss_target/build_config.h")
    assert "mode_ = Mode::INTRO;" in controller
    assert "startIntroUntilMs_ = now + ::boss_target::START_INTRO_MS;" in controller
    assert "if (mode_ == Mode::INTRO && hpRemaining_ > 0" in controller
    assert "mode_ = Mode::ACTIVE;" in controller
    assert "selectNewTarget(now);" in controller
    assert "void BossTargetController::renderStartIntro" in controller
    assert 'obj["start_intro_active"] = mode_ == Mode::INTRO;' in controller
    assert "if (mode_ == Mode::INTRO || otaPrepared_) return;" in controller
    assert "if (mode_ == Mode::READY || mode_ == Mode::UNCONFIGURED)" in controller
    assert 'lower == "start" || lower == "arm" || lower == "activate"' in main
    assert 'strcmp(command, "start") == 0' in mqtt
    assert 'strcmp(command, "reset") == 0' in mqtt
    assert 'strcmp(command, "status") == 0' in mqtt
    assert 'strcmp(command, "disable")' not in mqtt
    assert 'strcmp(command, "enable")' not in mqtt
    assert 'strcmp(command, "stop")' not in mqtt


def test_boss_target_round_rings_are_targets_and_hp_bar_owns_hp_rendering() -> None:
    header = read("firmware/boss_target/build_config.h")
    controller = read("firmware/boss_target/target/boss_target_controller.cpp")
    controller_header = read("firmware/boss_target/target/boss_target_controller.h")
    readme = read("firmware/boss_target/README.md")

    assert "static constexpr uint16_t RING_NUM_LEDS = 120;" in header
    assert "static constexpr uint16_t HP_BAR_GROUP_COUNT = 100;" in header
    assert "static constexpr uint8_t HP_BAR_LEDS_PER_GROUP = 3;" in header
    assert "static constexpr uint16_t HP_BAR_NUM_LEDS = HP_BAR_GROUP_COUNT * HP_BAR_LEDS_PER_GROUP;" in header
    assert "static constexpr int HP_BAR_PIN = 12;" in header
    assert "HP bar LED count must match grouped bar layout" in header
    assert "static constexpr uint32_t HIT_FLASH_MS = 1000;" in header
    assert "static constexpr uint32_t HIT_FLASH_BLINK_MS = 125;" in header
    assert "FastLED.addLeds<WS2811, ::boss_target::HP_BAR_PIN, RGB>(hpBar_" in controller

    render_intro = controller[controller.index("void BossTargetController::renderStartIntro"):controller.index("void BossTargetController::renderTargets")]
    assert "neonOrbitValue(distance" in render_intro
    assert "START_INTRO_HUE_STEP_MS" in render_intro
    assert "setHpBarGroup(group, CHSV(hue, 255, neonOrbitValue(distance, 55)));" in render_intro
    assert "ring >= targetCount()" in render_intro

    # Round LED rings are only target indicators: black by default, red active target,
    # white hit flash. HP fill/count rendering belongs to hpBar_ only.
    render_targets = controller[controller.index("void BossTargetController::renderTargets"):controller.index("void BossTargetController::renderHpBar")]
    assert "fillRing(i, CRGB::Black)" in render_targets
    assert "config_.target.hitFlashColor" in render_targets
    assert "HIT_FLASH_BLINK_MS" in render_targets
    assert "} else if (!targetTransitionPending_ && i == activeTarget_) {" in render_targets
    assert "config_.target.activeColor" in render_targets
    assert "hpRemaining_" not in render_targets
    assert "hpLit" not in render_targets

    set_hp_group = controller[controller.index("void BossTargetController::setHpBarGroup"):controller.index("void BossTargetController::setHpBarAll")]
    assert "group 1  -> LEDs 1, 200, 201" in set_hp_group
    assert "group 100 -> LEDs 100, 101, 300" in set_hp_group
    assert "row1Index = group0Based" in set_hp_group
    assert "row2Index = 2 * groups - 1 - group0Based" in set_hp_group
    assert "row3Index = 2 * groups + group0Based" in set_hp_group

    render_hp = controller[controller.index("void BossTargetController::renderHpBar"):controller.index("void BossTargetController::clearAllLeds")]
    assert "fill_solid(hpBar_, ::boss_target::MAX_HP_BAR_NUM_LEDS, CRGB::Black)" in render_hp
    assert "setHpBarAll(colorFromRgb(::boss_target::HP_RED))" in render_hp
    assert "for (uint16_t group = 0; group < hpGroupCount(); ++group)" in render_hp
    assert "if (group < lit) setHpBarGroup(group, base);" in render_hp
    assert "hpBlinkMask_" not in render_hp
    assert "setHpBarAll(CRGB::White)" not in render_hp
    assert "fillRing" not in render_hp

    assert "clampedHp * hpGroupCount() + config_.gameplay.hpMax - 1" in controller
    assert "targetFlashUntilMs_[targetIndex] = now + ::boss_target::HIT_FLASH_MS;" in controller
    assert "targetTransitionPending_ = true;" in controller
    assert "nextTargetSelectionMs_ = now + ::boss_target::HIT_FLASH_MS;" in controller
    assert "if (mode_ == Mode::ACTIVE && hpRemaining_ > 0 && targetTransitionPending_" in controller
    assert "hpFlashUntilMs_" not in controller
    assert "hpBlinkMask_" not in controller_header
    assert "4 target LED rings + 4 piezo AO inputs + 1 HP bar" in readme
    assert "100 vertical HP columns × 3 horizontal rows" in readme


def test_boss_target_hp_bar_links_three_rows_by_vertical_column() -> None:
    assert boss_hp_bar_group_indices(1) == (0, 199, 200)  # LEDs 1, 200, 201
    assert boss_hp_bar_group_indices(2) == (1, 198, 201)  # LEDs 2, 199, 202
    assert boss_hp_bar_group_indices(100) == (99, 100, 299)  # LEDs 100, 101, 300

    all_indices = {
        idx
        for group in range(1, BOSS_HP_BAR_GROUP_COUNT + 1)
        for idx in boss_hp_bar_group_indices(group)
    }
    assert all_indices == set(range(BOSS_HP_BAR_LED_COUNT))


def test_boss_target_runtime_config_persists_config_not_match_progress() -> None:
    header = read("firmware/boss_target/config/runtime_config.h")
    source = read("firmware/boss_target/config/runtime_config.cpp")
    main = read("firmware/boss_target/main.cpp")

    assert "struct RuntimeConfig" in header
    assert "String displayName" in header
    assert "GameplayConfig gameplay" in header
    assert "TargetConfig target" in header
    assert "HpBarConfig hpBar" in header
    assert "HardwareProfileConfig hardware" in header
    assert "String wifiSsid" in header
    assert "String mqttHost" in header
    assert "bool otaCommandCenterControlled" in header
    assert '#include <bb_esp_nvs/common_runtime_config_store.h>' in source
    assert 'const char* kConfigNamespace = "boss_target";' in source
    assert "ScopedPreferences scopedPrefs" in source
    assert "scopedPrefs.begin(kConfigNamespace" in source
    assert "loadCommonRuntimeConfig(prefs, common, commonNvsKeys())" in source
    assert "saveCommonRuntimeConfig(" in source
    assert 'keys.deviceId = "device_id";' in source
    assert 'keys.wifiPassword = "wifi_pass";' in source
    assert 'keys.mqttPassword = "mqtt_pass";' in source
    assert 'keys.otaDesiredBuild = "ota_build";' in source
    assert 'prefs.getString("display"' in source
    assert 'prefs.putString("display"' in source
    assert 'prefs.getUShort("hp_max"' in source
    assert 'prefs.putUShort("damage"' in source
    assert 'prefs.getUChar("tgt_count"' in source
    assert 'doc["display_name"] = config.displayName' in source
    assert 'next.deviceId = getStringOr(doc["device_id"], next.deviceId);' in source
    assert 'next.displayName = getStringOr(doc["display_name"], next.displayName);' in source
    assert 'next.displayName = getStringOr(doc["name"], next.displayName);' in source
    assert "RuntimeConfig loaded = config;" in source
    assert "if (!validateConfig(loaded, error))" in source
    assert "stored config invalid" in source
    assert "RuntimeConfig salvaged = config;" in source
    assert "copyConnectivityConfig(salvaged, loaded);" in source
    assert "dest.deviceId = source.deviceId;" in source
    assert "preserving connectivity with compiled defaults" in source
    assert "config = loaded;" in source
    assert "const bool loadedStoredConfig = configStore.load(config);" in main
    assert "no valid stored config; using MAC-derived defaults" in main
    assert "next.deviceId != config.deviceId" in main
    assert "next.deviceId != config_->deviceId" in read("firmware/boss_target/mqtt/mqtt_bus.cpp")
    assert "hp_remaining" not in source
    assert "active_target_index" not in source
    assert "targetStartedMs" not in source


def test_boss_target_runtime_config_requires_versions_and_safe_mqtt_topics() -> None:
    source = read("firmware/boss_target/config/runtime_config.cpp")
    docs = read("firmware/boss_target/docs/runtime-config.md")
    readme = read("firmware/boss_target/README.md")

    assert 'doc.containsKey("config_version")' in source
    assert 'error = "config_version is required";' in source
    assert 'error = "config_version must be positive";' in source
    assert "incomingVersion < config.configVersion" in source
    assert "bool validateTopicSegment" in source
    assert "bool validateMqttRoot" in source
    assert "validateTopicSegment(config.deviceId, \"device_id\", error)" in source
    assert "validateTopicSegment(config.bossId, \"boss_id\", error)" in source
    assert "validateTopicSegment(config.targetId, \"target_id\", error)" in source
    assert "mqtt.root must not contain empty path segments" in source
    assert "mqtt.root must use slash-separated topic segments" in source

    assert "Every provisioning/config update must include a positive `config_version`" in docs
    assert "`boss_id`, `target_id`, and `device_id` are MQTT topic segments" in docs
    assert "`mqtt.root` is normalized as slash-separated MQTT topic segments" in docs
    assert "A-Z, a-z, 0-9, `_`, `-`, or `.`" in docs
    assert "`config_version` is mandatory for all `provision` and `config` payloads" in readme
    assert "`boss_id`, `target_id`, and `device_id` are restricted to MQTT-safe topic segment characters" in readme


def test_boss_target_mqtt_topics_status_and_ota_are_firmware_specific() -> None:
    topics = read("firmware/boss_target/mqtt/topics.cpp")
    bus = read("firmware/boss_target/mqtt/mqtt_bus.cpp")
    firmware = read("firmware/boss_target/app/firmware_info.h")
    manifest = read("firmware/boss_target/ota/ota_manifest.cpp")
    manifest_h = read("firmware/boss_target/ota/ota_manifest.h")
    common_manifest = read("lib/bb_esp_ota/src/bb_esp_ota/ota_manifest.cpp")
    workflow = read(".github/workflows/firmware-ota.yml")
    pio = read("platformio.ini")

    assert "#include <bb_esp_core/mqtt/device_topics.h>" in topics
    assert "normalizeRootOrDefault(config.mqttRoot)" in topics
    assert "makeDeviceTopicsChecked(root, config.deviceId" in topics
    assert 'makeAllOtaTopicChecked(root, "boss_targets"' in topics
    assert 'makeEntityTopicsChecked(\n            root, "boss_targets", config.bossId' in topics
    assert "topics.bossCommand = entityTopics.command;" in topics
    assert "publishStatus(\"heartbeat\")" in bus
    controller = read("firmware/boss_target/target/boss_target_controller.cpp")
    assert 'obj["display_name"] = config_.displayName;' in controller
    assert 'obj["name"] = config_.displayName;' in controller
    assert 'obj["hp_remaining"]' in controller
    assert 'BB_BOSS_TARGET_APP_NAME "battlebang-boss-target"' in firmware
    assert 'BB_BOSS_TARGET_HARDWARE "esp32dev-boss-target-ring-v1"' in firmware
    assert "boss-target-manifest.json" in firmware
    assert "#include <bb_esp_ota/ota_manifest.h>" in manifest_h
    assert "identity.app = BB_BOSS_TARGET_APP_NAME" in manifest
    assert "identity.hardware = BB_BOSS_TARGET_HARDWARE" in manifest
    assert "identity.build = BB_BOSS_TARGET_BUILD" in manifest
    assert "shouldApplyManifest(manifest, firmwareIdentity(), reason)" in manifest
    assert "manifest.app != identity.app" in common_manifest
    assert "manifest.hardware != identity.hardware" in common_manifest
    assert "manifest.build <= identity.build" in common_manifest
    assert "esp32dev_boss_target" in workflow
    assert "boss-target-latest" in workflow
    assert "boss-target-v" in workflow
    assert "[env:esp32dev_boss_target]" in pio
    assert "+<../firmware/boss_target/**>" in pio
    assert "board_build.partitions = min_spiffs.csv" in pio


def test_boss_target_ota_uses_tls_ca_and_recovers_after_failed_download() -> None:
    ota_wrapper = read("firmware/boss_target/ota/http_ota.h") + read("firmware/boss_target/ota/http_ota.cpp")
    ota = read("lib/bb_esp_ota/src/bb_esp_ota/http_ota.cpp")
    controller = read("firmware/boss_target/target/boss_target_controller.cpp")
    controller_header = read("firmware/boss_target/target/boss_target_controller.h")
    main = read("firmware/boss_target/main.cpp")
    mqtt = read("firmware/boss_target/mqtt/mqtt_bus.cpp")
    readme = read("firmware/boss_target/README.md")

    assert "setInsecure" not in ota_wrapper + ota
    assert "battlebang::esp::ota::runHttpOta" in ota_wrapper
    assert "kGithubReleaseRootCaPem" in ota
    assert "ensureTlsClock()" in ota
    assert 'configTime(0, 0, "pool.ntp.org", "time.nist.gov");' in ota
    assert "secureClient.setCACert(kGithubReleaseRootCaPem);" in ota
    assert "kOtaNoProgressTimeoutMs" in ota
    assert 'result.message = "download stalled with no progress";' in ota
    assert "Update.abort();" in ota
    assert "void recoverFromFailedOta(const char* source);" in controller_header
    assert "void BossTargetController::recoverFromFailedOta" in controller
    assert "otaPrepared_ = false;" in controller
    assert "hitEnabled_ = true;" in controller
    assert 'emit("ota_failed"' in controller
    assert 'target.recoverFromFailedOta("ota_failed");' in main
    assert 'target_->recoverFromFailedOta("mqtt_ota_failed");' in mqtt
    assert "uses a pinned GitHub Release root CA" in readme
    assert "clears OTA-prepared state and restores normal target rendering" in readme


def test_boss_target_helpers_and_docs_do_not_require_committed_secrets() -> None:
    env_example = read("firmware/boss_target/.env.boss_target.example")
    provision = read("scripts/boss_target/provision.py")
    mqtt_command = read("scripts/boss_target/mqtt_command.py")
    gitignore = read(".gitignore")
    readme = read("firmware/boss_target/README.md")

    assert "BOSS_TARGET_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in env_example
    assert "BOSS_TARGET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS" in env_example
    assert "BOSS_TARGET_STAGE_ID=boss_stage_v1" in env_example
    assert "BOSS_TARGET_DEVICE_ID=" in env_example
    assert "BOSS_TARGET_DISPLAY_NAME=Boss Target" in env_example
    assert "src/*/.env.*" in gitignore
    assert "DEFAULT_ENV_FILE = PROJECT_ROOT / \"firmware\" / \"boss_target\" / \".env.boss_target\"" in provision
    assert '"type": "provision"' in provision
    assert '"device_id": env_first(env, "BOSS_TARGET_DEVICE_ID", default="")' in provision
    assert 'doc["device_id"] = doc.get("boss_id") or doc.get("target_id") or ""' in provision
    assert '"hp_max": env_int(env, "BOSS_TARGET_HP_MAX", 10)' in provision
    assert '"damage_per_hit": env_int(env, "BOSS_TARGET_DAMAGE_PER_HIT", 1)' in provision
    assert 'set_if_present(doc, "display_name", env_first(env, "BOSS_TARGET_DISPLAY_NAME", "BOSS_TARGET_NAME"))' in provision
    assert '"ring_pins": split_csv_ints' in provision
    assert "publish_mqtt" in mqtt_command
    assert 'choices=("start", "reset", "status", "simulate-hit", "led-test", "led-test-off", "config", "ota", "all-ota")' in mqtt_command
    assert '"command": "led_test"' in mqtt_command
    assert "--duration-ms" in mqtt_command
    assert "There is no production `disable`, `enable`, `pause`, or `stop` command" in readme
    assert "docs/runtime-config.md" in readme
    assert "`config.json`: committed factory-default/schema reference" in readme
    assert "`hardware_profile` is accepted in provision/config payloads" in readme


def test_boss_target_runtime_config_docs_and_examples_explain_nvs_contract() -> None:
    docs = read("firmware/boss_target/docs/runtime-config.md")
    config = json.loads(read("firmware/boss_target/config.json"))
    provision = json.loads(read("firmware/boss_target/examples/provision.boss-target.example.json"))
    display_update = json.loads(read("firmware/boss_target/examples/config.display-name.example.json"))
    gameplay_update = json.loads(read("firmware/boss_target/examples/config.gameplay-target.example.json"))
    start_command = json.loads(read("firmware/boss_target/examples/command.start.example.json"))
    reset_command = json.loads(read("firmware/boss_target/examples/command.reset.example.json"))
    status_command = json.loads(read("firmware/boss_target/examples/command.status.example.json"))

    assert config["documentation"] == "docs/runtime-config.md"
    assert "examples/provision.boss-target.example.json" in config["examples"]
    assert "committed schema/default reference" in config["notes"]
    assert "hardware_profile is validated against the compiled firmware as a compatibility guard" in config["notes"]

    for required in (
        "ESP32 NVS namespace `boss_target`",
        "`firmware/boss_target/config.json`",
        "`firmware/boss_target/.env.boss_target.example`",
        "provision {json}",
        "config {json}",
        "battlebang/boss_targets/{boss_id}/config",
        "`device_id`",
        "`boss_id`",
        "`display_name`",
        "`gameplay.hp_max`",
        "`target.count`",
        "Every provisioning/config update must include a positive `config_version`",
        "`boss_id`, `target_id`, and `device_id` are MQTT topic segments",
        "`hardware_profile` appears in provision/config payloads",
        "`hp_remaining`",
        "`active_target_index`",
        "not written to NVS",
        "GPIO12 HP bar in dim white 30% idle fill",
    ):
        assert required in docs

    assert provision["type"] == "provision"
    assert provision["configured"] is True
    assert provision["device_id"] == "boss_target_dev_01"
    assert provision["display_name"] == "Mini Boss Left"
    assert provision["gameplay"]["hp_max"] == 10
    assert provision["gameplay"]["damage_per_hit"] == 1
    assert provision["target"]["count"] == 4
    assert provision["hp_bar"]["num_leds"] == 300
    assert provision["hardware_profile"]["max_targets"] == 4
    assert provision["hardware_profile"]["ring_pins"] == [23, 21, 18, 17]
    assert provision["hardware_profile"]["piezo_do_pins"] == [34, 27, 32, 33]
    assert provision["wifi"]["password"] == "YOUR_WIFI_PASSWORD"
    assert provision["mqtt"]["host"] == "COMMAND_CENTER_IP_OR_DNS"
    assert provision["ota"]["channel"] == "boss-target"

    assert display_update == {
        "schema": 1,
        "config_version": 2,
        "device_id": "boss_target_dev_01",
        "display_name": "Mini Boss Left",
        "group": "boss-stage",
        "stage_id": "boss_stage_v1",
        "location": "stage-left",
    }
    assert gameplay_update["gameplay"]["hp_max"] == 10
    assert gameplay_update["gameplay"]["damage_per_hit"] == 1
    assert gameplay_update["target"]["count"] == 4
    assert gameplay_update["hp_bar"]["palette"] == ["#00FF00", "#FFFF00", "#FF0000"]
    assert start_command == {"command": "start"}
    assert reset_command == {"command": "reset"}
    assert status_command == {"command": "status"}
