from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GO2 = (
    "go2",
    ROOT / "firmware/go2/display/bar_display.cpp",
    ROOT / "firmware/go2/build_config.h",
    ROOT / "firmware/go2/hardware_profile.json",
)
GO2_NIXO = (
    "go2_nixo",
    ROOT / "firmware/go2_nixo/display/bar_display.cpp",
    ROOT / "firmware/go2_nixo/display/ring_display.cpp",
    ROOT / "firmware/go2_nixo/build_config.h",
    ROOT / "firmware/go2_nixo/hardware_profile.json",
)
HP_BAR_FIRMWARES = (
    GO2,
    (GO2_NIXO[0], GO2_NIXO[1], GO2_NIXO[3], GO2_NIXO[4]),
)
RING_FIRMWARES = (GO2_NIXO,)
BTB782_STANDALONE = ROOT / "scripts/btb782_esp_uart_hp_standalone"

HP_BAR_GROUP_COUNT = 28
HP_BAR_LEDS_PER_GROUP = 3
HP_BAR_LED_COUNT = HP_BAR_GROUP_COUNT * HP_BAR_LEDS_PER_GROUP
RING_LED_COUNT = 40


def run_provision_script(script: str, env_file: str, *args: str) -> dict:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / script),
            "--env-file",
            str(ROOT / env_file),
            "--no-serial",
            "--print-json-secrets",
            *args,
        ],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(result.stdout.strip().splitlines()[-1])


def linked_group_indices(group_1_based: int) -> tuple[int, int, int]:
    """Mirror the Go2 HP bar harness mapping from HW_Go2_HP_BAR."""
    return (
        group_1_based - 1,
        2 * HP_BAR_GROUP_COUNT - group_1_based,
        2 * HP_BAR_GROUP_COUNT - 1 + group_1_based,
    )


def healthy_groups(fill_ratio: float) -> int:
    return max(0, min(int(fill_ratio * HP_BAR_GROUP_COUNT + 0.5), HP_BAR_GROUP_COUNT))


def test_hp_bar_reference_group_mapping() -> None:
    assert linked_group_indices(1) == (0, 55, 56)  # LEDs 1, 56, 57
    assert linked_group_indices(2) == (1, 54, 57)  # LEDs 2, 55, 58
    assert linked_group_indices(28) == (27, 28, 83)  # LEDs 28, 29, 84

    all_indices = {
        idx
        for group in range(1, HP_BAR_GROUP_COUNT + 1)
        for idx in linked_group_indices(group)
    }
    assert all_indices == set(range(HP_BAR_LED_COUNT))


def test_hp_bar_sample_hp_fill_counts() -> None:
    assert healthy_groups(1.0) == 28
    assert healthy_groups(0.5) == 14
    assert healthy_groups(0.25) == 7
    assert healthy_groups(0.0) == 0


def test_go2_defaults_are_hp_bar_only() -> None:
    firmware, _bar_cpp, _build_config, robots_json = GO2
    defaults = json.loads(robots_json.read_text())["defaults"]
    assert defaults["led_pin"] == 18, firmware
    assert defaults["num_leds"] == HP_BAR_LED_COUNT, firmware
    assert defaults["led_brightness"] == 120, firmware
    assert "ring_led_pin" not in defaults, firmware
    assert "ring_num_leds" not in defaults, firmware
    assert "ring_led_brightness" not in defaults, firmware
    assert "nixo_mqtt_topic_prefix" not in defaults, firmware
    assert "nixo_fire_default_duration_ms" not in defaults, firmware
    assert "nixo_fire_cooldown_ms" not in defaults, firmware


def test_go2_nixo_defaults_disable_esp_side_nixo_cooldown() -> None:
    firmware, _bar_cpp, _ring_cpp, _build_config, robots_json = GO2_NIXO
    defaults = json.loads(robots_json.read_text())["defaults"]
    assert defaults["led_pin"] == 18, firmware
    assert defaults["num_leds"] == HP_BAR_LED_COUNT, firmware
    assert defaults["led_brightness"] == 120, firmware
    assert defaults["ring_led_pin"] == 4, firmware
    assert defaults["ring_num_leds"] == RING_LED_COUNT, firmware
    assert defaults["ring_led_brightness"] == 80, firmware
    assert defaults["nixo_fire_default_duration_ms"] == 3000, firmware
    assert defaults["nixo_fire_cooldown_ms"] == 0, firmware


def test_go2_identity_is_not_a_build_time_profile() -> None:
    for firmware, *_paths, hardware_profile in (GO2, GO2_NIXO):
        data = json.loads(hardware_profile.read_text())
        assert "defaults" in data, firmware
        assert "robots" not in data, firmware

    assert not (ROOT / "firmware/go2/robots.json").exists()
    assert not (ROOT / "firmware/go2_nixo/robots.json").exists()


def test_hp_bar_renderer_uses_bar_led_layout() -> None:
    for firmware, bar_cpp, _build_config, _robots_json in HP_BAR_FIRMWARES:
        source = bar_cpp.read_text()
        header = bar_cpp.with_suffix(".h").read_text()
        assert "CRGB leds_[HP_BAR_NUM_LEDS] = {};" in header, firmware
        assert "BarDisplay::begin" in source, firmware
        assert "void begin(uint16_t brightness = HP_BAR_LED_BRIGHTNESS);" in header, firmware
        assert "void setBrightness(uint16_t brightness);" in header, firmware
        assert "setBrightness(brightness);" in source, firmware
        assert "FastLED.addLeds<WS2815, HP_BAR_LED_PIN, RGB>" in source, firmware
        assert "i < HP_BAR_NUM_LEDS" in source, firmware
        assert "i < NUM_LEDS" not in source, firmware
        assert "setHpBarGroup" in source, firmware
        assert "group 1  -> LEDs 1, 56, 57" in source, firmware
        assert "group 28 -> LEDs 28, 29, 84" in source, firmware
        assert "row2Index = 2 * HP_BAR_GROUP_COUNT - group1Based" in source, firmware
        assert "row3Index = 2 * HP_BAR_GROUP_COUNT - 1 + group1Based" in source, firmware


def test_fire_ring_renderer_uses_original_ring_pin_only_in_go2_nixo() -> None:
    assert not (ROOT / "firmware/go2/display/ring_display.cpp").exists()
    assert not (ROOT / "firmware/go2/display/ring_display.h").exists()
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in RING_FIRMWARES:
        source = ring_cpp.read_text()
        header = ring_cpp.with_suffix(".h").read_text()
        assert "RingDisplay::begin" in source, firmware
        assert "void begin(uint16_t brightness = RING_LED_BRIGHTNESS);" in header, firmware
        assert "void setBrightness(uint16_t brightness);" in header, firmware
        assert "FastLED.addLeds<WS2811, RING_LED_PIN, RGB>" in source, firmware
        assert "renderCooldown" in source, firmware
        assert "RING_NUM_LEDS" in source, firmware


def test_fire_ring_renders_red_fire_and_original_cooldown_fill() -> None:
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in RING_FIRMWARES:
        source = ring_cpp.read_text()
        header = ring_cpp.with_suffix(".h").read_text()
        assert "renderFiring" in source, firmware
        assert "renderCooldown" in source, firmware
        assert "RING_COOLDOWN_FILL_STEPS" in source, firmware
        assert "cooldownColor" in source, firmware
        assert "completedSteps" in source, firmware
        assert "remainingMs" in source, firmware
        assert "uint32_t cooldownStartedMs_" in header, firmware
        assert "uint32_t cooldownDurationMs_" in header, firmware
        assert "uint32_t cooldownRemainingMs_" in header, firmware
        assert "uint16_t brightness_ = RING_LED_BRIGHTNESS;" in header, firmware
        assert "uint16_t scale = brightness_;" in source, firmware
        assert "if (firing_)" in source, firmware
        assert "if (inhibited_)" in source, firmware
        assert "CRGB color = scaled(96, 0, 0)" in source, firmware
        assert "CRGB color = scaled(0, 64, 0)" in source, firmware
        assert "return firing_ || inhibited_ || remainingMs(now) > 0;" in source, firmware


def test_btb782_standalone_uses_three_piezo_channels() -> None:
    source = (BTB782_STANDALONE / "src/main.cpp").read_text()
    platformio = (BTB782_STANDALONE / "platformio.ini").read_text()
    readme = (BTB782_STANDALONE / "README.md").read_text()

    for name, pin in (
        ("LEFT", 34),
        ("RIGHT", 35),
        ("FRONT", 32),
    ):
        assert f"BTB782_{name}_PIEZO_PIN" in source
        assert f"-D BTB782_{name}_PIEZO_PIN={pin}" in platformio

    assert "analogRead(BTB782_LEFT_PIEZO_PIN)" in source
    assert "analogRead(BTB782_RIGHT_PIEZO_PIN)" in source
    assert "analogRead(BTB782_FRONT_PIEZO_PIN)" in source
    assert "analogSetPinAttenuation(BTB782_LEFT_PIEZO_PIN, ADC_11db)" in source
    assert "analogSetPinAttenuation(BTB782_RIGHT_PIEZO_PIN, ADC_11db)" in source
    assert "analogSetPinAttenuation(BTB782_FRONT_PIEZO_PIN, ADC_11db)" in source
    assert "BTB782_LEFT_PIEZO_PIN != BTB782_RIGHT_PIEZO_PIN" in source
    assert "piezo:left" in source
    assert "piezo:right" in source
    assert "piezo:front" in source
    assert "left `D34`/`GPIO34`" in readme
    assert "right `D35`/`GPIO35`" in readme
    assert "front `D32`/`GPIO32`" in readme


def test_btb782_standalone_can_fire_nixo_from_serial_terminal() -> None:
    source = (BTB782_STANDALONE / "src/main.cpp").read_text()
    platformio = (BTB782_STANDALONE / "platformio.ini").read_text()
    readme = (BTB782_STANDALONE / "README.md").read_text()

    assert "#define BTB782_NIXO_RELAY1_PIN 23" in source
    assert "#define BTB782_NIXO_RELAY2_PIN -1" in source
    assert "#define BTB782_NIXO_FIRE_DEFAULT_DURATION_MS 3000" in source
    assert "#define BTB782_NIXO_FIRE_COOLDOWN_MS 1500" in source
    assert "-D BTB782_NIXO_RELAY1_PIN=23" in platformio
    assert "-D BTB782_NIXO_FIRE_DEFAULT_DURATION_MS=3000" in platformio
    assert "static bool startFireSequence" in source
    assert "static void updateFireSequence" in source
    assert "awaiting_fire_go2_id" not in source
    assert 'return strcmp(source, "jetson") == 0;' in source
    assert "reason=jetson_uart_required" in source
    assert 'lower == "f" || lower == "fire"' in source
    assert 'lower.startsWith("f ") || lower.startsWith("fire ")' not in source
    assert 'return value.startsWith("go2_");' not in source
    assert "go2_id=" not in source
    assert "startFireSequence(BTB782_NIXO_FIRE_DEFAULT_DURATION_MS, source);" in source
    assert 'lower == "x" || lower == "stop-fire" || lower == "fire off"' in source
    assert "updateFireSequence(now);" in source
    assert "Live fire is accepted only from Jetson UART" in readme
    assert "`f` / `fire`: fire this robot's local Nixo." in readme
    assert not (BTB782_STANDALONE / "jetson_terminal.py").exists()


def test_bar_remote_ttl_is_bounded_for_signed_expiry_math() -> None:
    for firmware, bar_cpp, _build_config, _robots_json in HP_BAR_FIRMWARES:
        source = bar_cpp.read_text()
        assert "if (ttlMs < 1) ttlMs = 1;" in source, firmware
        assert "if (ttlMs > 0x7ffffffful) ttlMs = 0x7ffffffful;" in source, firmware
        assert "remoteExpiresMs_ = now + ttlMs;" in source, firmware


def test_fire_ring_cooldown_fill_updates_only_when_pixels_change() -> None:
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in RING_FIRMWARES:
        source = ring_cpp.read_text()
        assert "bool mismatch = false;" in source, firmware
        assert "CRGB expected = (i < lit) ? cooldownColor : CRGB::Black;" in source, firmware
        assert "if (leds_[i] != expected)" in source, firmware
        assert "if (mismatch)" in source, firmware


def test_go2_and_go2_nixo_reuse_common_runtime_config_and_mqtt_topic_helpers() -> None:
    go2_runtime = (ROOT / "firmware/go2/config/runtime_config.h").read_text()
    go2_runtime_source = (ROOT / "firmware/go2/config/runtime_config.cpp").read_text()
    go2_nixo_runtime = (ROOT / "firmware/go2_nixo/config/runtime_config.h").read_text()
    go2_nixo_runtime_source = (ROOT / "firmware/go2_nixo/config/runtime_config.cpp").read_text()
    go2_hit_header = (ROOT / "firmware/go2/mqtt/hit_mqtt_client.h").read_text()
    go2_hit_source = (ROOT / "firmware/go2/mqtt/hit_mqtt_client.cpp").read_text()
    go2_main = (ROOT / "firmware/go2/main.cpp").read_text()
    go2_nixo_hit_header = (ROOT / "firmware/go2_nixo/mqtt/hit_mqtt_client.h").read_text()
    go2_nixo_hit_source = (ROOT / "firmware/go2_nixo/mqtt/hit_mqtt_client.cpp").read_text()
    go2_nixo_main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    nixo_fire_header = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.h").read_text()
    nixo_fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()

    for firmware, runtime, runtime_source in (
        ("go2", go2_runtime, go2_runtime_source),
        ("go2_nixo", go2_nixo_runtime, go2_nixo_runtime_source),
    ):
        assert "struct RuntimeConfig" in runtime, firmware
        assert "battlebang::esp::config::CommonRuntimeConfig common;" in runtime, firmware
        assert "uint16_t piezoAoThresholdRaw = PIEZO_AO_THRESHOLD_RAW;" in runtime, firmware
        assert "uint16_t piezoAoRearmRaw = PIEZO_AO_REARM_RAW;" in runtime, firmware
        assert "uint16_t offlineQueueCapacity = OFFLINE_HIT_QUEUE_CAPACITY;" in runtime, firmware
        assert "config.hit.hitTopicPrefix" in runtime_source, firmware
        assert "RuntimeConfig runtimeConfigFromBuild();" in runtime, firmware
        assert "RuntimeConfig runtimeConfigFromNvsOrBuild();" in runtime, firmware
        assert "bool loadRuntimeConfigFromNvs(RuntimeConfig& config);" in runtime, firmware
        assert "bool saveRuntimeConfigToNvs(const RuntimeConfig& config);" in runtime, firmware
        assert "bool clearRuntimeConfigNvs();" in runtime, firmware
        assert "bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);" in runtime, firmware
        assert "String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);" in runtime, firmware
        assert "#include <bb_esp_core/config/build_time_config.h>" in runtime_source, firmware
        assert "#include <bb_esp_core/config/runtime_config_json.h>" in runtime_source, firmware
        assert "#include <bb_esp_nvs/common_runtime_config_store.h>" in runtime_source, firmware
        assert "makeBuildTimeCommonRuntimeConfig(commonDefaults)" in runtime_source, firmware
        assert "commonDefaults.deviceId = buildDeviceId.c_str();" in runtime_source, firmware
        assert "defaultRuntimeRobotId()" in runtime_source, firmware
        assert 'commonDefaults.mqttRoot = "battlebang";' in runtime_source, firmware
        assert "commonDefaults.otaPublicManifestUrl" in runtime_source, firmware
        assert "standardCommonRuntimeConfigKeys()" in runtime_source, firmware
        assert "loadCommonRuntimeConfig(prefs.preferences(), config.common, commonNvsKeys())" in runtime_source, firmware
        assert "saveCommonRuntimeConfig(" in runtime_source, firmware
        assert "clearNamespace(kConfigNamespace)" in runtime_source, firmware
        assert 'config.common.configured = hasStoredConfig || config.common.configured;' in runtime_source, firmware
        assert "applyCommonRuntimeConfigJson(root, next.common, error)" in runtime_source, firmware
        assert "writeCommonRuntimeConfigJson(root, config.common, includeSecrets)" in runtime_source, firmware
        assert "validateRuntimeConfig(next, error)" in runtime_source, firmware

    for firmware, header, source, main in (
        ("go2", go2_hit_header, go2_hit_source, go2_main),
        ("go2_nixo", go2_nixo_hit_header, go2_nixo_hit_source, go2_nixo_main),
    ):
        assert "void begin(const RuntimeConfig& config, BarDisplayHandler barHandler);" in header, firmware
        assert "void setManagementHandlers(ManagementMessageHandler configHandler, ManagementMessageHandler otaHandler);" in header, firmware
        assert "begin(runtimeConfigFromBuild(), barHandler);" in source, firmware
        if firmware in {"go2", "go2_nixo"}:
            assert "#include <WiFi.h>" in source, firmware
        assert "networkConfigured_ = config.common.configured;" in source, firmware
        assert "offlineQueueCapacity_ = static_cast<uint8_t>(config.hit.offlineQueueCapacity);" in source, firmware
        assert "offlineQueueFlushIntervalMs_ = config.hit.offlineQueueFlushIntervalMs;" in source, firmware
        assert "#include <bb_esp_core/config/string_buffer.h>" in source, firmware
        assert "#include <bb_esp_core/mqtt/device_topics.h>" in source, firmware
        assert 'copyStringOrWarn("mqtt.host", config.common.mqttHost, mqttHost_, sizeof(mqttHost_));' in source, firmware
        assert 'copyStringOrWarn("mqtt.username", config.common.mqttUsername, mqttUsername_, sizeof(mqttUsername_));' in source, firmware
        assert 'copyStringOrWarn("mqtt.password", config.common.mqttPassword, mqttPassword_, sizeof(mqttPassword_));' in source, firmware
        assert "char mqttUsername_[64] = {0};" in header, firmware
        assert "char mqttPassword_[96] = {0};" in header, firmware
        assert "mqttClient_.connect(clientId_, mqttUsername_, mqttPassword_)" in source, firmware
        assert "mqtt_auth_configured" in main, firmware
        assert 'warnIfFormatTruncated("mqtt.client_id", clientIdLength, sizeof(clientId_));' in source, firmware
        assert 'battlebang::esp::mqtt::joinTopic(config.hit.hitTopicPrefix, config.hit.robotId, "events")' in source, firmware
        assert 'config.hit.hitTopicPrefix, config.hit.robotId, "ring_display", "command"' in source, firmware
        expected_device_type = "go2_nixo" if firmware == "go2_nixo" else "go2"
        assert (
            f'makeDeviceTopics(config.common.mqttRoot, "{expected_device_type}", config.common.deviceId)'
            in source
        ), firmware
        assert "deviceStatusTopic_" in header, firmware
        assert "deviceConfigTopic_" in header, firmware
        assert "deviceOtaTopic_" in header, firmware
        assert "mqttClient_.subscribe(deviceConfigTopic_, 1)" in source, firmware
        assert "mqttClient_.subscribe(deviceOtaTopic_, 1)" in source, firmware
        assert "publishDeviceStatus" in source, firmware
        assert '"%s/%s/events"' not in source, firmware
        assert '"%s/%s/ring_display/command"' not in source, firmware
        assert "RuntimeConfig runtimeConfig;" in main, firmware
        assert "runtimeConfig = runtimeConfigFromNvsOrBuild();" in main, firmware
        assert "hitMqtt.begin(runtimeConfig, onBarDisplayUpdate);" in main, firmware
        assert "runtimeConfig.hit.piezoAoThresholdRaw" in main, firmware
        assert "runtimeConfig.hit.hitCooldownMs" in main, firmware
        assert "PIEZO_LEFT_AO_PIN" in main, firmware
        assert "PIEZO_RIGHT_AO_PIN" in main, firmware
        assert "PIEZO_FRONT_AO_PIN" in main, firmware
        assert '"piezo_ao_threshold_raw"' in main, firmware
        assert "#include <bb_esp_ota/http_ota.h>" in main, firmware
        assert "#include <bb_esp_ota/ota_manifest.h>" in main, firmware
        assert "#include <bb_esp_ota/reboot_marker.h>" in main, firmware
        assert "writeUnsupportedOtaStatus(" not in main, firmware
        assert 'doc["ota_supported"] = true;' in main, firmware
        assert "checkOtaManifestJson" in main, firmware
        assert "checkOtaManifestUrlWithPolicy" in main, firmware
        assert "pollConfiguredOta" in main, firmware
        assert "publishDeviceStatusIfConnected" in main, firmware
        assert "onMqttConfigMessage" in main, firmware
        assert "onMqttOtaMessage" in main, firmware
        assert "if (mqttClient_.connected()) mqttClient_.disconnect();" in source, firmware

    assert "struct NixoRuntimeConfig" in go2_nixo_runtime
    assert "config.nixo.nixoId = NIXO_ID_VALUE;" in go2_nixo_runtime_source
    assert "config.nixo.commandTopicPrefix" in go2_nixo_runtime_source
    assert "void begin(const RuntimeConfig& config);" in nixo_fire_header
    assert "begin(runtimeConfigFromBuild());" in nixo_fire_source
    assert "#include <WiFi.h>" in nixo_fire_source
    assert "#include <bb_esp_core/config/string_buffer.h>" in nixo_fire_source
    assert "#include <PubSubClient.h>" in nixo_fire_header
    assert "networkConfigured_ = config.common.configured;" in nixo_fire_source
    assert 'copyStringOrWarn("nixo.id", config.nixo.nixoId, nixoId_, sizeof(nixoId_));' in nixo_fire_source
    assert 'copyStringOrWarn("mqtt.username", config.common.mqttUsername, mqttUsername_, sizeof(mqttUsername_));' in nixo_fire_source
    assert 'copyStringOrWarn("mqtt.password", config.common.mqttPassword, mqttPassword_, sizeof(mqttPassword_));' in nixo_fire_source
    assert "char mqttUsername_[64] = {0};" in nixo_fire_header
    assert "char mqttPassword_[96] = {0};" in nixo_fire_header
    assert "mqttClient_.connect(clientId_, mqttUsername_, mqttPassword_)" in nixo_fire_source
    assert 'warnIfFormatTruncated("nixo.client_id", clientIdLength, sizeof(clientId_));' in nixo_fire_source
    assert 'config.nixo.commandTopicPrefix, config.nixo.nixoId, "command"' in nixo_fire_source
    assert '"%s/%s/command"' not in nixo_fire_source
    assert "tickNetwork" in nixo_fire_header
    assert "tickNetwork" in nixo_fire_source
    assert "nixoFire.begin(runtimeConfig);" in go2_nixo_main
    assert "deferred: Nixo relay is firing" in go2_nixo_main
    assert "uint32_t fireDefaultDurationMs = NIXO_FIRE_DEFAULT_DURATION_MS;" in go2_nixo_runtime
    assert "uint32_t fireCooldownMs = NIXO_FIRE_COOLDOWN_MS;" in go2_nixo_runtime
    assert "config.nixo.fireDefaultDurationMs = NIXO_FIRE_DEFAULT_DURATION_MS;" in go2_nixo_runtime_source
    assert "nixo.fireCooldownMs = 0;" in go2_nixo_runtime_source
    assert "fireDefaultDurationMs_ = config.nixo.fireDefaultDurationMs;" in nixo_fire_source
    assert "fireCooldownMs_ = 0;" in nixo_fire_source
    assert "relayDelay1Ms_ = config.nixo.relayDelay1Ms;" in nixo_fire_source
    assert "runtimeConfig.nixo.fireDefaultDurationMs" in go2_nixo_main
    assert '"nixo_fire_default_duration_ms"' in go2_nixo_main
    assert "nixo_relay1_pin" not in go2_nixo_runtime_source
    assert "nixo_relay2_pin" not in go2_nixo_runtime_source



def assert_local_hit_state_contract(firmware_dir: str, env_prefix: str) -> None:
    main = (ROOT / f"firmware/{firmware_dir}/main.cpp").read_text()
    bar_header = (ROOT / f"firmware/{firmware_dir}/display/bar_display.h").read_text()
    bar_source = (ROOT / f"firmware/{firmware_dir}/display/bar_display.cpp").read_text()
    mqtt_header = (ROOT / f"firmware/{firmware_dir}/mqtt/hit_mqtt_client.h").read_text()
    mqtt_source = (ROOT / f"firmware/{firmware_dir}/mqtt/hit_mqtt_client.cpp").read_text()
    runtime_header = (ROOT / f"firmware/{firmware_dir}/config/runtime_config.h").read_text()
    runtime_source = (ROOT / f"firmware/{firmware_dir}/config/runtime_config.cpp").read_text()
    build_config = (ROOT / f"firmware/{firmware_dir}/build_config.h").read_text()
    env_example = (ROOT / f"firmware/{firmware_dir}/.env.{firmware_dir}.example").read_text()
    provision_script = (ROOT / f"scripts/{firmware_dir}/provision.py").read_text()
    mqtt_contract = (ROOT / f"firmware/{firmware_dir}/docs/mqtt-hit-contract.md").read_text()

    assert "struct LocalHitState" in main
    assert "acceptedHitCount" in main
    assert "hpRemaining" in main
    assert "applyLocalHit" in main
    assert "publishAdcHitEvent" in main
    assert "barDisplay.setLocalHpState" in main
    assert "publishDeviceStatusIfConnected(localHitState.down ? \"local_hit_down\" : \"local_hit\")" in main
    assert 'doc["accepted_hit_count"] = localHitState.acceptedHitCount;' in main
    assert 'doc["hp_remaining"] = localHitState.hpRemaining;' in main
    assert 'doc["max_hits"] = localHitState.maxHits;' in main
    assert 'doc["down"] = localHitState.down;' in main
    assert 'JsonObject combat = doc.createNestedObject("combat");' in main
    assert 'combat["hp_current"] = localHitState.hpRemaining;' in main
    assert 'combat["hp_max"] = localHitState.maxHits;' in main
    assert "resetLocalHitState" in main
    assert "syncLocalHitStateWithRuntimeConfig" in main
    assert "publishMqttReconnectStatus" in main
    assert "bool hasSeenMqttConnection = false;" in main
    assert 'hasSeenMqttConnection ? "mqtt_reconnected" : "mqtt_connected"' in main

    publish_function = main.split("static void publishAdcHitEvent", 1)[1].split(
        "static void updateAnalogDebugStats", 1
    )[0]
    assert "if (localHitState.down || localHitState.hpRemaining == 0)" in publish_function
    assert 'publishDeviceStatusIfConnected("local_hit_ignored_down");' in publish_function
    assert publish_function.index("local_hit_ignored_down") < publish_function.index(
        "uint32_t sequence = ++hitSequence;"
    )

    reset_function = main.split("static void resetLocalHitState()", 1)[1].split("static float localHpFillRatio", 1)[0]
    assert "localHitState.maxHits = runtimeConfig.hit.maxHits" in reset_function
    assert "localHitState.hpRemaining = localHitState.maxHits;" in reset_function
    assert "localHitState.acceptedHitCount = 0;" in reset_function
    assert "localHitState.down = false;" in reset_function
    assert "barDisplay.resetLocalHpState(localHitState.maxHits);" in reset_function

    reset_block = main.split("static void resetAll", 1)[1].split("static void handleCommandChar", 1)[0]
    assert "resetAnalogPiezoState();" in reset_block
    assert "hitMqtt.clearOfflineQueue();" in reset_block
    assert "resetLocalHitState();" in reset_block

    setup_block = main.split("void setup()", 1)[1].split("void loop()", 1)[0]
    assert setup_block.index("runtimeConfig = runtimeConfigFromNvsOrBuild();") < setup_block.index(
        "resetLocalHitState();"
    )
    assert "resetLocalHitState();" in setup_block

    loop_block = main.split("void loop()", 1)[1]
    assert loop_block.index("pollAnalogPiezo(now);") < loop_block.index("hitMqtt.tick(now")
    assert loop_block.index("barDisplay.tick(now);") < loop_block.index("hitMqtt.tick(now")

    assert "setLocalHpState" in bar_header
    assert "resetLocalHpState" in bar_header
    assert "renderLocal" in bar_source
    assert "handleLocalFlashExpiry" in bar_source
    assert "localFillRatio" in bar_source
    assert "if (hitFlashMs > 0x7ffffffful) hitFlashMs = 0x7ffffffful;" in bar_source

    assert "publishHitEvent" in mqtt_header
    if firmware_dir == "go2_nixo":
        assert "publishHpResetEvent" in mqtt_header
        assert 'doc["event_type"] = "hp_reset";' in mqtt_source
        assert 'doc["sensor_id"] = "hit_ring";' in mqtt_source
        assert 'doc["reset_hit_state"] = true;' in mqtt_source
        assert 'doc["hp_reset"] = true;' in mqtt_source
        assert "return mqttClient_.publish(deviceStatusTopic_, payload, true);" in mqtt_source
        assert "pendingHpResetEvent" in main
        assert 'publishHpResetEventIfConnected("mqtt_reset")' in main
        assert "publishHpResetEventIfConnected(hasSeenMqttConnection ? \"mqtt_reconnected\" : \"boot\")" in main
    assert "queueHitEvent" in mqtt_header
    assert "QueuedHitEvent" in mqtt_header
    assert "DynamicJsonDocument doc(MQTT_BUFFER_SIZE);" in mqtt_source
    assert "String buffer;" in mqtt_source
    assert "buffer.reserve(MQTT_BUFFER_SIZE);" in mqtt_source
    assert 'doc["schema_version"] = 2;' in mqtt_source
    assert 'doc["event"] = "hit_event";' in mqtt_source
    assert 'doc["accepted"] = true;' in mqtt_source
    assert 'doc["accepted_hit_count"] = acceptedHitCount;' in mqtt_source
    assert 'doc["hp_remaining"] = hpRemaining;' in mqtt_source
    assert 'doc["max_hits"] = maxHits;' in mqtt_source
    assert 'metadata["decision_owner"] = "esp_local";' in mqtt_source
    assert 'metadata["display_owner"] = "esp_local";' in mqtt_source
    assert 'metadata["hp_current"] = hpRemaining;' in mqtt_source
    assert 'metadata["hp_max"] = maxHits;' in mqtt_source
    assert "ring command ignored: ESP owns local HP bar" in mqtt_source
    assert "debug_override" in mqtt_source
    assert "reset_hit_state" in mqtt_source
    assert "if (barHandler_ != nullptr) barHandler_(update);" in mqtt_source.split(
        "if (!update.resetHitState && !update.debugOverride)"
    )[1]

    assert "uint16_t maxHits = MAX_HITS;" in runtime_header
    assert "uint32_t hitFlashMs = HIT_FLASH_MS;" in runtime_header
    assert 'readUInt16ConfigField(object, "max_hits", hit.maxHits);' in runtime_source
    assert 'readUInt16ConfigField(object, "hits_to_down", hit.maxHits);' in runtime_source
    assert 'readUInt32Field(object, "hit_flash_ms", hit.hitFlashMs);' in runtime_source
    assert 'prefs.preferences().getUInt("max_hits", hit.maxHits)' in runtime_source
    assert 'prefs.preferences().getUInt("hit_flash", hit.hitFlashMs)' in runtime_source
    assert 'hitObject["max_hits"] = hit.maxHits;' in runtime_source
    assert 'hitObject["hits_to_down"] = hit.maxHits;' in runtime_source
    assert 'hitObject["hit_flash_ms"] = hit.hitFlashMs;' in runtime_source
    assert "accepted_hit_count" not in runtime_source
    assert "hp_remaining" not in runtime_source
    assert "last_hit_sequence" not in runtime_source

    assert "#define BATTLEBANG_MAX_HITS 14" in build_config
    assert "#define BATTLEBANG_HIT_FLASH_MS 900" in build_config
    assert "static_assert(MAX_HITS >= 1 && MAX_HITS <= 1000" in build_config
    assert "static_assert(HIT_FLASH_MS <= 60000UL" in build_config
    expected_mqtt_buffer = 3072 if firmware_dir == "go2_nixo" else 2048
    assert f"static constexpr uint16_t MQTT_BUFFER_SIZE = {expected_mqtt_buffer};" in build_config
    assert f"{env_prefix}_MAX_HITS=14" in env_example
    assert f"{env_prefix}_HIT_FLASH_MS=900" in env_example
    assert '"max_hits": env_int' in provision_script
    assert '"hit_flash_ms": env_int' in provision_script

    assert "hit_event" in mqtt_contract
    assert "decision_owner" in mqtt_contract
    assert "display_owner" in mqtt_contract
    assert "local_hit_ignored_down" in mqtt_contract
    assert "reset/debug compatibility" in mqtt_contract
    assert "hit_candidate" not in mqtt_contract


def test_go2_local_hit_state_owns_hp_bar_and_publishes_combat_status() -> None:
    assert_local_hit_state_contract("go2", "GO2")


def test_go2_nixo_local_hit_state_owns_hp_bar_while_ring_led_remains_nixo_fire_state() -> None:
    assert_local_hit_state_contract("go2_nixo", "GO2_NIXO")
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    ring_source = (ROOT / "firmware/go2_nixo/display/ring_display.cpp").read_text()
    nixo_fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()
    assert "ring LED is reserved for Nixo ready/firing/inhibited state, never HP state" in main
    assert "ringDisplay.setCooldownState" in main
    assert "nixoFire.cooldownRemainingMs(now)" in main
    assert "nixoFire.cooldownDurationMs()" in main
    assert "renderCooldown" in ring_source
    assert "cooldownStartedMs_" in nixo_fire_source
    assert 'nixoFire.stopFire("mqtt-hit-reset")' not in main
    bar_update_block = main.split("static void onBarDisplayUpdate", 1)[1].split("void setup()", 1)[0]
    debug_override_block = bar_update_block.split("if (!update.debugOverride) return;", 1)[1]
    assert "nixoFire.setFireInhibited" not in debug_override_block
    tick_block = nixo_fire_source.split("void NixoFireClient::tick", 1)[1].split(
        "bool NixoFireClient::configured", 1
    )[0]
    assert tick_block.index("updateFireSequence(now);") < tick_block.index("ensureMqttConnected(now);")


def test_go2_nixo_bar_renders_remaining_hp_and_recent_damage() -> None:
    bar_header = (ROOT / "firmware/go2_nixo/display/bar_display.h").read_text()
    bar_source = (ROOT / "firmware/go2_nixo/display/bar_display.cpp").read_text()
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()

    local_block = bar_source.split("void BarDisplay::renderLocal", 1)[1].split(
        "void BarDisplay::renderRemote", 1
    )[0]
    assert "fillRatio <= 0.30f ? CRGB::Yellow : CRGB::Green" in local_block
    assert "renderHpBar(fillRatio," in local_block
    assert "CRGB::Black);" in local_block
    assert "setHpBarGroup(group, CRGB::Red);" in local_block
    assert "localHpRemaining_ + 1" in local_block
    assert 'localMode_ == "hit_flash"' in local_block
    assert "CRGB::White" not in local_block
    assert "CRGB::Orange" not in local_block
    assert "renderSegmentNumber" not in bar_source
    assert "DIGIT_PIXELS" not in bar_source
    assert "setFiring" not in bar_header
    assert "barDisplay.setFiring" not in main
    assert "setHpBarPixel" in bar_header
    assert "group = HP_BAR_GROUP_COUNT - 1 - group;" in bar_source
    assert "strip = HP_BAR_LEDS_PER_GROUP - 1 - strip;" in bar_source
    assert "localHpRemaining_) / static_cast<float>(localMaxHits_)" in bar_source
    assert "localHitState.hpRemaining--;" in main
    assert "barDisplay.resetLocalHpState(localHitState.maxHits);" in main


def test_go2_nixo_ignores_hits_during_three_second_startup_loading() -> None:
    bar_header = (ROOT / "firmware/go2_nixo/display/bar_display.h").read_text()
    bar_source = (ROOT / "firmware/go2_nixo/display/bar_display.cpp").read_text()
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()

    assert "STARTUP_LOADING_MS = 3000" in bar_source
    assert "bool startupReady(uint32_t now) const;" in bar_header
    assert "const int interiorGroups = HP_BAR_GROUP_COUNT - 2;" in bar_source
    assert "setHpBarPixel(group, 0, CRGB::White);" in bar_source
    assert "setHpBarPixel(group, 1, CRGB::Blue);" in bar_source
    publish_function = main.split("static void publishAdcHitEvent", 1)[1].split(
        "static void updateAnalogDebugStats", 1
    )[0]
    assert "if (!barDisplay.startupReady(eventTsMs))" in publish_function
    assert publish_function.index("startupReady(eventTsMs)") < publish_function.index(
        "uint32_t sequence = ++hitSequence;"
    )


def test_go2_platformio_envs_are_generic_and_identity_is_nvs_provisioned() -> None:
    platformio = (ROOT / "platformio.ini").read_text()
    go2_config = (ROOT / "scripts/go2_config.py").read_text()
    go2_nixo_config = (ROOT / "scripts/go2_nixo_config.py").read_text()
    go2_flash = (ROOT / "scripts/go2_flash.py").read_text()
    go2_build = (ROOT / "firmware/go2/build_config.h").read_text()
    go2_nixo_build = (ROOT / "firmware/go2_nixo/build_config.h").read_text()
    go2_runtime = (ROOT / "firmware/go2/config/runtime_config.cpp").read_text()
    go2_nixo_runtime = (ROOT / "firmware/go2_nixo/config/runtime_config.cpp").read_text()

    assert "[env:esp32dev_go2]" in platformio
    assert "[env:esp32dev_go2_nixo]" in platformio
    assert "[env:esp32dev_go2_nixo_1ch]" in platformio
    assert "[env:esp32dev_go2_nixo_2ch]" in platformio
    for forbidden in (
        "esp32dev_go2_go2_",
        "esp32dev_go2_nixo_go2_",
        "esp32dev_go2_nixo_1ch_go2_",
        "esp32dev_go2_nixo_2ch_go2_",
        "custom_robot_id",
    ):
        assert forbidden not in platformio

    for script in (go2_config, go2_nixo_config):
        assert "hardware_profile.json" in script
        assert "robots.json" not in script
        assert "detect_robot_id" not in script
        assert "custom_robot_id" not in script
        assert "BATTLEBANG_BUILD_ROBOT_ID" not in script

    assert '#define BATTLEBANG_ROBOT_ID ""' in go2_build
    assert '#define BATTLEBANG_ROBOT_ID ""' in go2_nixo_build
    assert '#define BATTLEBANG_NIXO_ID ""' in go2_nixo_build
    assert "BATTLEBANG_ENABLE_LOCAL_SECRETS" in go2_build
    assert "BATTLEBANG_ENABLE_LOCAL_SECRETS" in go2_nixo_build
    assert "BATTLEBANG_SKIP_LOCAL_SECRETS" not in go2_build
    assert "BATTLEBANG_SKIP_LOCAL_SECRETS" not in go2_nixo_build
    assert "--use-local-secrets" in go2_flash
    assert "none (runtime NVS provisioning expected)" in go2_flash
    assert '"go2_05"' not in go2_build
    assert '"go2_05"' not in go2_nixo_build
    assert "defaultRuntimeRobotId()" in go2_runtime
    assert "defaultRuntimeRobotId()" in go2_nixo_runtime
    assert 'String("go2-") + efuseMacHex()' in go2_runtime
    assert 'String("go2-nixo-") + efuseMacHex()' in go2_nixo_runtime
    assert 'String("nixo_") + robotId' in go2_nixo_runtime

def test_go2_nvs_bridge_keeps_build_defaults_as_fallback_and_uses_standard_keys() -> None:
    go2_runtime_source = (ROOT / "firmware/go2/config/runtime_config.cpp").read_text()
    go2_nixo_runtime_source = (ROOT / "firmware/go2_nixo/config/runtime_config.cpp").read_text()
    nixo_fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()
    platformio = (ROOT / "platformio.ini").read_text()

    assert "+<../firmware/go2/config/**>" in platformio

    for firmware, source, namespace in (
        ("go2", go2_runtime_source, "go2"),
        ("go2_nixo", go2_nixo_runtime_source, "go2_nixo"),
    ):
        assert f'const char* kConfigNamespace = "{namespace}";' in source, firmware
        assert 'const char* kHitTopicPrefixKey = "hit_topic";' in source, firmware
        assert "return battlebang::esp::nvs::standardCommonRuntimeConfigKeys();" in source, firmware
        assert 'prefs.preferences().getBool("configured", false)' in source, firmware
        assert 'prefs.preferences().getString("robot_id", hit.robotId)' in source, firmware
        assert 'prefs.preferences().getString(kHitTopicPrefixKey, hit.hitTopicPrefix)' in source, firmware
        assert 'prefs.preferences().putString("robot_id", hit.robotId) > 0' in source, firmware
        assert 'prefs.preferences().putString(kHitTopicPrefixKey, hit.hitTopicPrefix) > 0' in source, firmware
        for key in (
            "hit_cd_ms",
            "offq_cap",
            "offq_flush",
            "led_bright",
            "piezo_thr",
            "piezo_rearm",
            "piezo_cap_ms",
            "piezo_dbg_ms",
            "piezo_arm_ms",
        ):
            assert key in source, firmware
        assert "normalizeRuntimeConfig(config);" in source, firmware

    assert 'nixo.nixoId = prefs.preferences().getString("nixo_id", nixo.nixoId)' in go2_nixo_runtime_source
    assert 'const char* kNixoCommandTopicPrefixKey = "nixo_cmd_topic";' in go2_nixo_runtime_source
    assert 'prefs.preferences().getString(kNixoCommandTopicPrefixKey, nixo.commandTopicPrefix)' in go2_nixo_runtime_source
    assert 'prefs.preferences().putString("nixo_id", nixo.nixoId) > 0' in go2_nixo_runtime_source
    assert 'prefs.preferences().putString(kNixoCommandTopicPrefixKey, nixo.commandTopicPrefix) > 0' in go2_nixo_runtime_source
    assert "ring_bright" in go2_nixo_runtime_source
    for key in (
        "fire_def_ms",
        "fire_min_ms",
        "fire_max_ms",
        "fire_cd_ms",
        "prefire_ms",
        "relay_dly1_ms",
    ):
        assert key in go2_nixo_runtime_source


def test_go2_runtime_nvs_bridge_has_serial_management_commands() -> None:
    go2_main = (ROOT / "firmware/go2/main.cpp").read_text()
    go2_nixo_main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    go2_runtime_source = (ROOT / "firmware/go2/config/runtime_config.cpp").read_text()
    go2_nixo_runtime_source = (ROOT / "firmware/go2_nixo/config/runtime_config.cpp").read_text()
    nixo_fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()

    for firmware, main, source in (
        ("go2", go2_main, go2_runtime_source),
        ("go2_nixo", go2_nixo_main, go2_nixo_runtime_source),
    ):
        assert "String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets)" in source, firmware
        assert "bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error)" in source, firmware
        assert "DynamicJsonDocument doc(" in source, firmware
        assert 'readStringField(root, "robot_id", next.hit.robotId);' in source, firmware
        assert 'readStringField(root, "hit_topic_prefix", next.hit.hitTopicPrefix);' in source, firmware
        assert "readHitTuningJson(root, next.hit);" in source, firmware
        assert 'hitObject["topic_prefix"] = hit.hitTopicPrefix;' in source, firmware

        assert "static void applyAndPersistConfig(const String& json, const char* source)" in main, firmware
        assert "RuntimeConfig next = runtimeConfig;" in main, firmware
        assert "applyRuntimeConfigJson(json.c_str(), next, error)" in main, firmware
        assert "const bool saved = saveRuntimeConfigToNvs(next);" in main, firmware
        assert "runtimeConfig = next;" in main, firmware
        assert "reapplyRuntimeConfig(\"serial_config\");" in main, firmware
        assert "runtimeConfigToJson(runtimeConfig, false)" in main, firmware
        assert "clearRuntimeConfigNvs()" in main, firmware
        assert "runtimeConfig = runtimeConfigFromBuild();" in main, firmware
        assert "status/show-status" in main, firmware
        assert "provision {json}" in main, firmware
        assert "config {json}" in main, firmware
        assert "clear-config" in main, firmware
        assert "pollCommandStream" in main, firmware
        assert "COMMAND_LINE_MAX = 2048" in main, firmware
        assert "isImmediateCommandChar(c) && stream.available() == 0" in main, firmware

    assert 'readStringField(root, "nixo_id", next.nixo.nixoId);' in go2_nixo_runtime_source
    assert 'readStringField(root, "nixo_command_topic_prefix", next.nixo.commandTopicPrefix);' in go2_nixo_runtime_source
    assert 'readStringField(nixo, "command_topic_prefix", next.nixo.commandTopicPrefix);' in go2_nixo_runtime_source
    assert 'root["nixo_id"] = nixo.nixoId;' in go2_nixo_runtime_source
    assert "pollCommandStream(JetsonSerial, jetsonCommandLine, \"jetson\");" in go2_nixo_main
    assert 'return strcmp(source, "jetson") == 0 || strcmp(source, "usb") == 0;' in go2_nixo_main
    assert "reason=jetson_uart_required" in go2_nixo_main
    assert 'lower == "x" || lower == "0" || lower == "stop-fire" || lower == "fire off"' in go2_nixo_main
    assert "JETSON_FIRE_HOLD_TIMEOUT_MS = 300" in go2_nixo_main
    assert "jetsonFireHoldActive = true;" in go2_nixo_main
    assert "jetsonFireReleaseRequired" in go2_nixo_main
    assert "reason=non_jetson_fire_active" in go2_nixo_main
    assert "reason=release_required_after_duration" in go2_nixo_main
    assert "reason=release_required" in go2_nixo_main
    assert "lastPublishedJetsonReleaseRequired" in go2_nixo_main
    assert "runtimeConfig.nixo.fireMaxDurationMs" in go2_nixo_main
    assert "nixoFire.startFire(runtimeConfig.nixo.fireMaxDurationMs, fireSource, true)" in go2_nixo_main
    assert "isJetsonBufferedImmediateCommand" in go2_nixo_main
    assert "c == 'x' || c == '0'" in go2_nixo_main
    assert "nixo_relay2_readback" in go2_nixo_main
    assert 'doc.createNestedObject("hp")' in go2_nixo_main
    assert 'doc.createNestedObject("nixo")' in go2_nixo_main
    assert 'nixo["state"] = nixoFire.fireStateName();' in go2_nixo_main
    assert 'doc["nixo_state"] = nixoFire.fireStateName();' in go2_nixo_main
    assert 'doc["nixo_fire_source"] = nixoFire.activeFireSource();' in go2_nixo_main
    assert 'nixo["active_source"] = nixoFire.activeFireSource();' in go2_nixo_main
    assert 'doc["jetson_fire_release_required"] = jetsonFireReleaseRequired;' in go2_nixo_main
    assert 'nixo["jetson_release_required"] = jetsonFireReleaseRequired;' in go2_nixo_main
    assert 'lower.startsWith("fire ")' in go2_nixo_main
    assert 'if (fireSource.startsWith("source="))' in go2_nixo_main
    assert 'return strcmp(source, "jetson") == 0 ? "jetson_uart" : source;' in go2_nixo_main
    assert "lastPublishedNixoState" in go2_nixo_main
    assert "lastPublishedNixoActiveSource" in go2_nixo_main
    assert 'publishDeviceStatusIfConnected("state_changed")' in go2_nixo_main
    assert "writeJetsonHpEvent(isDead ? 'd' : (isHit ? 'h' : 'r'));" in go2_nixo_main
    assert 'if (String(source) == "jetson") JetsonSerial.println(line);' not in go2_nixo_main
    assert "lastJetsonHpStatusMs" not in go2_nixo_main
    assert 'if (!hasSentJetsonHpStatus) {' in go2_nixo_main
    assert 'sendJetsonHpStatus("reset");' in go2_nixo_main
    assert 'const bool hpDecreased = localHitState.hpRemaining < lastJetsonHpRemaining;' in go2_nixo_main
    assert 'const bool hpIncreased = localHitState.hpRemaining > lastJetsonHpRemaining;' in go2_nixo_main
    assert 'const bool becameDead = isDead && !lastJetsonDead;' in go2_nixo_main
    assert 'sendJetsonHpStatus("dead");' in go2_nixo_main
    assert 'sendJetsonHpStatus("hit");' in go2_nixo_main
    assert "const char* fireStateName() const;" in (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.h").read_text()
    assert 'return "ready";' in nixo_fire_source
    assert '"flywheel_spinup"' in nixo_fire_source
    assert 'return "firing";' in nixo_fire_source


def test_go2_host_provisioning_scripts_generate_standard_runtime_json_without_relay_pin_runtime_fields() -> None:
    go2_script = (ROOT / "scripts/go2/provision.py").read_text()
    go2_nixo_script = (ROOT / "scripts/go2_nixo/provision.py").read_text()
    common_script = (ROOT / "scripts/go2_runtime/provisioning.py").read_text()

    assert "from go2_runtime.provisioning import" in go2_script
    assert "from go2_runtime.provisioning import" in go2_nixo_script
    assert "build_common_runtime_doc" in common_script
    assert "MAX_SERIAL_COMMAND_BYTES = 2048" in common_script
    assert "GO2_NIXO_RELAY1_PIN" not in go2_nixo_script
    assert "GO2_NIXO_RELAY2_PIN" not in go2_nixo_script
    assert "GO2_NIXO_RELAY_ON_LEVEL" not in go2_nixo_script
    assert "GO2_NIXO_RELAY_OFF_LEVEL" not in go2_nixo_script

    go2 = run_provision_script(
        "scripts/go2/provision.py",
        "firmware/go2/.env.go2.example",
        "--robot-id",
        "go2_03",
    )
    assert go2["type"] == "provision"
    assert go2["schema"] == 1
    assert go2["configured"] is True
    assert go2["config_version"] > 0
    assert go2["device_id"] == "go2_03"
    assert go2["group"] == "go2"
    assert "stage_id" in go2
    assert go2["stage_id"] == "boss_stage_v1"
    assert go2["robot_id"] == "go2_03"
    assert go2["hit_topic_prefix"] == "battlebang/hit"
    assert go2["hit"]["robot_id"] == "go2_03"
    assert go2["hit"]["topic_prefix"] == "battlebang/hit"
    assert go2["hit"]["piezo_ao_threshold_raw"] == 2400
    assert go2["hit"]["piezo_ao_rearm_raw"] == 1800
    assert go2["hit"]["led_brightness"] == 120
    assert go2["hit"]["max_hits"] == 14
    assert go2["hit"]["hit_flash_ms"] == 900
    assert go2["hit"]["offline_queue_capacity"] == 32
    assert go2["wifi"]["ssid"] == "YOUR_WIFI_SSID"
    assert go2["mqtt"]["host"] == "COMMAND_CENTER_IP_OR_DNS"
    assert go2["mqtt"]["root"] == "battlebang"
    assert go2["ota"]["channel"] == "go2"
    assert go2["ota"]["public_manifest_url"].endswith("/go2-latest/go2-manifest.json")
    assert "nixo_id" not in go2

    go2_nixo = run_provision_script(
        "scripts/go2_nixo/provision.py",
        "firmware/go2_nixo/.env.go2_nixo.example",
        "--robot-id",
        "go2_03",
    )
    assert go2_nixo["type"] == "provision"
    assert go2_nixo["schema"] == 1
    assert go2_nixo["configured"] is True
    assert go2_nixo["device_id"] == "go2_03"
    assert go2_nixo["group"] == "go2_nixo"
    assert "stage_id" in go2_nixo
    assert go2_nixo["stage_id"] == "boss_stage_v1"
    assert go2_nixo["robot_id"] == "go2_03"
    assert go2_nixo["hit_topic_prefix"] == "battlebang/hit"
    assert go2_nixo["nixo_id"] == "nixo_go2_03"
    assert go2_nixo["nixo_command_topic_prefix"] == "battlebang/nixo"
    assert go2_nixo["hit"]["piezo_ao_threshold_raw"] == 2400
    assert go2_nixo["hit"]["piezo_ao_rearm_raw"] == 1800
    assert go2_nixo["hit"]["led_brightness"] == 120
    assert go2_nixo["hit"]["ring_brightness"] == 80
    assert go2_nixo["hit"]["max_hits"] == 14
    assert go2_nixo["hit"]["hit_flash_ms"] == 900
    assert go2_nixo["nixo"]["id"] == "nixo_go2_03"
    assert go2_nixo["nixo"]["command_topic_prefix"] == "battlebang/nixo"
    assert go2_nixo["nixo"]["fire_default_duration_ms"] == 3000
    assert go2_nixo["nixo"]["fire_min_duration_ms"] == 100
    assert go2_nixo["nixo"]["fire_max_duration_ms"] == 10000
    assert go2_nixo["nixo"]["fire_cooldown_ms"] == 0
    assert go2_nixo["nixo"]["prefire_delay_ms"] == 600
    assert go2_nixo["nixo"]["relay_delay1_ms"] == 800
    assert go2_nixo["ota"]["channel"] == "go2-nixo-2ch"
    assert go2_nixo["ota"]["public_manifest_url"].endswith(
        "/go2-nixo-2ch-latest/go2-nixo-2ch-manifest.json"
    )

    go2_nixo_2ch = run_provision_script(
        "scripts/go2_nixo/provision.py",
        "firmware/go2_nixo/.env.go2_nixo.example",
        "--robot-id",
        "go2_03",
        "--relay-variant",
        "relay_2ch",
    )
    assert go2_nixo_2ch["ota"]["channel"] == "go2-nixo-2ch"
    assert go2_nixo_2ch["ota"]["public_manifest_url"].endswith(
        "/go2-nixo-2ch-latest/go2-nixo-2ch-manifest.json"
    )
    assert not [key for key in go2_nixo if "relay" in key.lower()]
    assert "relay1_pin" not in go2_nixo["nixo"]
    assert "relay2_pin" not in go2_nixo["nixo"]


def test_go2_piezo_threshold_defaults_match_btb782_three_channel_script() -> None:
    for firmware, _bar_cpp, build_config, robots_json in HP_BAR_FIRMWARES:
        defaults = json.loads(robots_json.read_text())["defaults"]
        assert defaults["piezo_left_pin"] == 34, firmware
        assert defaults["piezo_right_pin"] == 35, firmware
        assert defaults["piezo_front_pin"] == 32, firmware
        assert "piezo_ao_pin" not in defaults, firmware
        assert defaults["piezo_ao_threshold_raw"] == 2400, firmware
        assert defaults["piezo_ao_rearm_raw"] == 1800, firmware
        assert defaults["piezo_ao_debug_period_ms"] == 1000, firmware
        assert defaults["piezo_ao_threshold_raw"] > defaults["piezo_ao_rearm_raw"], firmware
        source = build_config.read_text()
        assert "#define BATTLEBANG_LEFT_PIEZO_AO_PIN 34" in source, firmware
        assert "#define BATTLEBANG_RIGHT_PIEZO_AO_PIN 35" in source, firmware
        assert "#define BATTLEBANG_FRONT_PIEZO_AO_PIN 32" in source, firmware
        assert "#define BATTLEBANG_PIEZO_AO_THRESHOLD_RAW 2400" in source, firmware
        assert "#define BATTLEBANG_PIEZO_AO_REARM_RAW 1800" in source, firmware
        assert "static constexpr uint32_t HIT_REARM_STABLE_MS = 30;" in source, firmware
        runtime_source = (ROOT / f"firmware/{firmware}/config/runtime_config.cpp").read_text()
        assert '"piezo_ao_threshold_raw"' in runtime_source, firmware
        assert '"piezo_ao_rearm_raw"' in runtime_source, firmware
        assert "piezo_thr" in runtime_source, firmware


def test_go2_build_config_locks_hp_bar_shape_without_nixo_ring() -> None:
    firmware, _bar_cpp, build_config, _robots_json = GO2
    source = build_config.read_text()
    assert "#define BATTLEBANG_LED_PIN 18" in source, firmware
    assert "#define BATTLEBANG_NUM_LEDS 84" in source, firmware
    assert "#define BATTLEBANG_LED_BRIGHTNESS 120" in source, firmware
    assert "#define BATTLEBANG_HP_BAR_GROUP_COUNT 28" in source, firmware
    assert "#define BATTLEBANG_HP_BAR_LEDS_PER_GROUP 3" in source, firmware
    assert "HP bar LED count must match grouped bar layout" in source, firmware
    assert "BATTLEBANG_RING_" not in source, firmware
    assert "BATTLEBANG_NIXO_" not in source, firmware


def test_go2_nixo_build_config_locks_bar_and_ring_shapes() -> None:
    firmware, _bar_cpp, _ring_cpp, build_config, _robots_json = GO2_NIXO
    source = build_config.read_text()
    assert "#define BATTLEBANG_LED_PIN 18" in source, firmware
    assert "#define BATTLEBANG_NUM_LEDS 84" in source, firmware
    assert "#define BATTLEBANG_LED_BRIGHTNESS 120" in source, firmware
    assert "#define BATTLEBANG_RING_LED_PIN 4" in source, firmware
    assert "#define BATTLEBANG_RING_NUM_LEDS 40" in source, firmware
    assert "#define BATTLEBANG_RING_LED_BRIGHTNESS 80" in source, firmware
    assert "#define BATTLEBANG_NIXO_FIRE_DEFAULT_DURATION_MS 3000" in source, firmware
    assert "#define BATTLEBANG_NIXO_FIRE_COOLDOWN_MS 0" in source, firmware
    assert "#define BATTLEBANG_HP_BAR_GROUP_COUNT 28" in source, firmware
    assert "#define BATTLEBANG_HP_BAR_LEDS_PER_GROUP 3" in source, firmware
    assert "HP bar LED count must match grouped bar layout" in source, firmware
    assert "HP bar and fire ring pins must be different" in source, firmware


def test_go2_does_not_mirror_or_render_nixo_fire() -> None:
    source = (ROOT / "firmware/go2/mqtt/hit_mqtt_client.cpp").read_text()
    header = (ROOT / "firmware/go2/mqtt/hit_mqtt_client.h").read_text()
    main = (ROOT / "firmware/go2/main.cpp").read_text()
    config_script = (ROOT / "scripts/go2_config.py").read_text()
    platformio = (ROOT / "platformio.ini").read_text()

    assert "RingDisplay" not in main
    assert "ringDisplay" not in main
    assert "nixoCommandTopic" not in header
    assert "handleNixoCommandMessage" not in source
    assert "+<../firmware/go2/display/bar_display.cpp>" in platformio
    assert "+<../firmware/go2/display/**>" not in platformio


def test_go2_nixo_drives_ring_from_local_fire_and_cooldown_state() -> None:
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    fire_header = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.h").read_text()
    fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()
    assert "ringDisplay.setCooldownState" in main
    assert "ringDisplay.setFireState" not in main
    assert "nixoFire.isFiring()" in main
    assert "nixoFire.cooldownRemainingMs(now)" in main
    assert "nixoFire.cooldownDurationMs()" in main
    assert "uint32_t cooldownRemainingMs(uint32_t now) const;" in fire_header
    assert "uint32_t cooldownStartedMs_ = 0;" in fire_header
    assert "void beginCooldown(uint32_t now);" in fire_header
    assert "uint32_t remainingMs = cooldownRemainingMs(now)" in fire_source
    assert "beginCooldown(now)" in fire_source
    assert "bool wasFiring = isFiring();" in fire_source
    assert "beginCooldown(millis());" not in fire_source
    assert 'doc["enabled"].is<bool>()' in fire_source
    assert 'const char* source = doc["source"] | "mqtt";' in fire_source
    assert 'const bool enabled = doc["enabled"].as<bool>();' in fire_source
    assert "char activeFireSource_[32] = {0};" in fire_header
    assert "void noteFireSource(const char* source);" in fire_header
    assert "bool startFire(uint32_t durationMs = 0, const char* source = \"local\", bool immediateFlywheel = false);" in fire_header
    assert "void startFlywheelNow(uint32_t now);" in fire_header
    assert "void beginStopSequence(uint32_t now);" in fire_header
    assert "void NixoFireClient::startFlywheelNow(uint32_t now)" in fire_source
    assert "void NixoFireClient::beginStopSequence(uint32_t now)" in fire_source
    assert "immediate_flywheel=%s" in fire_source
    assert "startFire(durationMs, source, false)" in fire_source
    assert "nixoFire.startFire(runtimeConfig.nixo.fireMaxDurationMs, fireSource, true)" in main
    assert 'return "flywheel_spindown";' in fire_source
    assert 'doc["enabled"] | true' not in fire_source
    assert "uint32_t elapsed = now - cooldownStartedMs_;" in fire_source
    stop_block = fire_source.split("void NixoFireClient::stopFire", 1)[1].split(
        "const char* NixoFireClient::commandTopic",
        1,
    )[0]
    assert "cooldownStartedMs_ = 0;" not in stop_block


def test_go2_nixo_defers_network_io_during_jetson_uart_fire_window() -> None:
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    hit_header = (ROOT / "firmware/go2_nixo/mqtt/hit_mqtt_client.h").read_text()
    hit_source = (ROOT / "firmware/go2_nixo/mqtt/hit_mqtt_client.cpp").read_text()
    fire_header = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.h").read_text()
    fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()

    assert "void tickLocal(uint32_t now);" in fire_header
    assert "void tickNetwork(uint32_t now);" in fire_header
    assert "void NixoFireClient::tickLocal(uint32_t now)" in fire_source
    assert "void NixoFireClient::tickNetwork(uint32_t now)" in fire_source
    assert "void tick(uint32_t now, bool remoteDisplayActive, bool allowNetworkIo = true);" in hit_header
    assert "if (!allowNetworkIo) return;" in hit_source
    assert "mqttClient_.setSocketTimeout(kMqttSocketTimeoutSeconds);" in hit_source
    assert "wifiClient_.setTimeout(kMqttSocketTimeoutSeconds);" in hit_source
    assert "mqttClient_.setSocketTimeout(kMqttSocketTimeoutSeconds);" in fire_source
    assert "wifiClient_.setTimeout(kMqttSocketTimeoutSeconds);" in fire_source
    assert "class UartFriendlyWiFiClient" in (ROOT / "firmware/go2_nixo/mqtt/uart_friendly_wifi_client.h").read_text()
    assert "kConnectTimeoutMs = 100" in (ROOT / "firmware/go2_nixo/mqtt/uart_friendly_wifi_client.h").read_text()
    assert "if (WiFi.status() != WL_CONNECTED) return;" in hit_source
    assert "if (WiFi.status() != WL_CONNECTED) return;" in fire_source

    assert "FIRE_NETWORK_QUIET_MS = 250" in main
    assert "markNetworkQuietForFireStop(now);" in main
    assert "shouldDeferNetworkForFire(now)" in main
    assert 'constexpr const char* NIXO_TRANSPORT = "jetson_uart+mqtt";' in main
    assert "nixoFire.tickNetwork(now);" in main
    assert "nixoFire.tickLocal(now);" in main
    local_index = main.index("nixoFire.tickLocal(now);")
    defer_index = main.index("const bool deferNetworkForFire = shouldDeferNetworkForFire(now);")
    nixo_network_index = main.index("nixoFire.tickNetwork(now);")
    network_index = main.index("hitMqtt.tick(now, barDisplay.remoteDisplayActive(), true);")
    assert local_index < defer_index < nixo_network_index < network_index
    assert "hitMqtt.tick(now, barDisplay.remoteDisplayActive(), true);" in main
    assert "publishStateChangeDeviceStatus(now);" in main[network_index:]


def test_go2_nixo_rejects_null_fire_source() -> None:
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    assert "static bool sourceCanFire(const char* source)" in main
    assert "if (source == nullptr) return false;" in main


def test_go2_nixo_integrated_fire_supports_1ch_and_2ch_variants() -> None:
    defaults = json.loads((ROOT / "firmware/go2_nixo/hardware_profile.json").read_text())["defaults"]
    relay_1ch = json.loads((ROOT / "firmware/go2_nixo/variants/relay_1ch/config.json").read_text())
    relay_2ch = json.loads((ROOT / "firmware/go2_nixo/variants/relay_2ch/config.json").read_text())
    platformio = (ROOT / "platformio.ini").read_text()
    build_config = (ROOT / "firmware/go2_nixo/build_config.h").read_text()
    fire_source = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.cpp").read_text()
    config_script = (ROOT / "scripts/go2_nixo_config.py").read_text()
    shared_relay_utils = (ROOT / "lib/bb_esp_hw/src/bb_esp_hw/relay_pin_utils.h").read_text()
    compat_relay_utils = (ROOT / "src/common/relay_pin_utils.h").read_text()

    assert defaults["nixo_relay1_pin"] == 23
    assert defaults["nixo_relay2_pin"] == -1
    assert defaults["nixo_relay_on_level"] == 1
    assert defaults["nixo_relay_off_level"] == 0
    assert defaults["nixo_relay_delay1_ms"] == 800

    assert relay_1ch["nixo_relay1_pin"] == 23
    assert relay_1ch["nixo_relay2_pin"] == -1
    assert relay_1ch["nixo_relay_on_level"] == 1
    assert relay_1ch["nixo_relay_off_level"] == 0
    assert relay_2ch["nixo_relay1_pin"] == 22
    assert relay_2ch["nixo_relay2_pin"] == 23
    assert relay_2ch["nixo_relay_on_level"] == 1
    assert relay_2ch["nixo_relay_off_level"] == 0
    assert relay_2ch["nixo_relay_delay1_ms"] == 150

    assert "custom_nixo_variant = relay_1ch" in platformio
    assert "custom_nixo_variant = relay_2ch" in platformio
    assert "[env:esp32dev_go2_nixo_1ch]" in platformio
    assert "[env:esp32dev_go2_nixo_2ch]" in platformio
    assert "esp32dev_go2_nixo_1ch_go2_" not in platformio
    assert "esp32dev_go2_nixo_2ch_go2_" not in platformio

    assert "def load_relay_variant" in config_script
    assert '"GO2_NIXO_RELAY_VARIANT"' in config_script
    assert '"GO2_NIXO_RELAY_VARIANT"' in config_script
    assert '"custom_nixo_variant"' in config_script
    assert "detect_robot_id" not in config_script
    assert "custom_robot_id" not in config_script
    assert "BATTLEBANG_BUILD_ROBOT_ID" not in config_script
    assert '"nixo_relay_delay1_ms": "BATTLEBANG_BUILD_NIXO_RELAY_DELAY1_MS"' in config_script
    assert "#define BATTLEBANG_NIXO_RELAY1_PIN 23" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY2_PIN -1" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY_ON_LEVEL HIGH" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY_OFF_LEVEL LOW" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY_DELAY1_MS 800" in build_config
    assert "static constexpr uint32_t NIXO_RELAY_DELAY1_MS =\n    BATTLEBANG_NIXO_RELAY_DELAY1_MS;" in build_config

    assert "#include <bb_esp_hw/relay_pin_utils.h>" in fire_source
    assert "void configureRelayPinOff(int pin)" not in fire_source
    assert "void configureRelayPinOff(int pin)" not in (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.h").read_text()
    assert "battlebang::esp::hw::configureRelayPinOffWithLevel(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);" in fire_source
    assert "battlebang::esp::hw::configureRelayPinOffWithLevel(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);" in fire_source
    assert "inline void configureRelayPinOffWithLevel(int pin, int offLevel)" in shared_relay_utils
    assert "pinMode(pin, offLevel == HIGH ? INPUT_PULLUP : INPUT_PULLDOWN);" in shared_relay_utils
    assert "#include <bb_esp_hw/relay_pin_utils.h>" in compat_relay_utils
    begin_block = fire_source.split("void NixoFireClient::begin()", 1)[1].split("mqttClient_.setServer", 1)[0]
    assert begin_block.index("configureRelayPinOffWithLevel(NIXO_RELAY2_PIN_VALUE") < begin_block.index(
        "configureRelayPinOffWithLevel(NIXO_RELAY1_PIN_VALUE"
    )
    relay_off_block = fire_source.split("void NixoFireClient::relayOff()", 1)[1].split(
        "void NixoFireClient::updateFireSequence",
        1,
    )[0]
    assert relay_off_block.index("digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);") < relay_off_block.index(
        "digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);"
    )
    start_flywheel_block = fire_source.split("void NixoFireClient::startFlywheelNow(uint32_t now)", 1)[1].split(
        "void NixoFireClient::beginStopSequence",
        1,
    )[0]
    assert "digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_ON_LEVEL_VALUE);" in start_flywheel_block
    assert start_flywheel_block.index("digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_ON_LEVEL_VALUE);") < (
        start_flywheel_block.index("if (NIXO_RELAY2_ENABLED_VALUE)")
    )
    assert 'return NIXO_RELAY2_ENABLED_VALUE ? "flywheel_spinup" : "firing";' in fire_source
    update_block = fire_source.split("void NixoFireClient::updateFireSequence", 1)[1]
    one_channel_done = update_block.split("case FIRE_RELAY_WAIT1:", 1)[1].split(
        "if (now - fireTimerMs_ >= relayDelay1Ms_)",
        1,
    )[0]
    two_channel_done = update_block.split("case FIRE_RELAY_WAIT2:", 1)[1].split("case FIRE_STOP_DELAY:", 1)[0]
    stop_delay = update_block.split("case FIRE_STOP_DELAY:", 1)[1]
    assert "activeFireSource_[0] = '\\0';" in one_channel_done
    assert one_channel_done.index("activeFireSource_[0] = '\\0';") < one_channel_done.index("beginCooldown(now);")
    assert "beginStopSequence(now);" in two_channel_done
    assert "digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);" in fire_source
    assert "now - fireTimerMs_ >= relayDelay1Ms_" in stop_delay
    assert "digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);" in stop_delay


def test_go2_nixo_legacy_uart_admin_damage_reports_authoritative_hp() -> None:
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text()
    damage_block = main.split("if (c == 'h')", 1)[1].split("if (c == 'x'", 1)[0]

    assert 'strcmp(source, "jetson") != 0' in damage_block
    assert "applyLocalHit(++hitSequence, millis());" in damage_block
    assert 'publishDeviceStatusIfConnected("jetson_hp_damage");' in damage_block
    assert "startFire" not in damage_block
    assert "c == 'h'" in main.split("static bool isImmediateCommandChar", 1)[1]
    assert r'{\"type\":\"hp_status\"' in main
    assert "writeJetsonHpSnapshot();" in main


def test_standalone_nixo_starts_local_cooldown_after_fire_completion() -> None:
    source = (ROOT / "src/nIxo/main.cpp").read_text()
    build_config = (ROOT / "src/nIxo/build_config.h").read_text()
    stop_block = source.split("static void stopFireSequence", 1)[1].split("static bool startFireSequence", 1)[0]
    assert "uint32_t cooldownStartedMs = 0;" in source
    assert "static uint32_t cooldownRemainingMs(uint32_t now)" in source
    assert "uint32_t elapsed = now - cooldownStartedMs;" in source
    assert "static void beginCooldown(uint32_t now)" in source
    assert "uint32_t remainingMs = cooldownRemainingMs(now);" in source
    assert "lastFireStartMs" not in source
    assert "beginCooldown(millis());" in stop_block
    assert "#include <bb_esp_hw/relay_pin_utils.h>" in source
    assert "void configureRelayPinOff(int pin)" not in source
    assert "battlebang::esp::hw::configureRelayPinOffWithLevel(RELAY1_PIN, RELAY_OFF);" in source
    assert "battlebang::esp::hw::configureRelayPinOffWithLevel(RELAY2_PIN, RELAY_OFF);" in source
    assert "stopFireSequence(\"mqtt\")" in source
    assert 'doc["enabled"].is<bool>()' in source
    assert 'const bool enabled = doc["enabled"].as<bool>();' in source
    assert 'doc["enabled"] | true' not in source
    assert "#define NIXO_RELAY1_PIN 23" in build_config
    assert "#define NIXO_RELAY2_PIN -1" in build_config
    assert "#define NIXO_RELAY_ON_LEVEL HIGH" in build_config
    assert "#define NIXO_FIRE_DEFAULT_DURATION_MS 3000" in build_config
    assert "#define NIXO_FIRE_COOLDOWN_MS 1500" in build_config
    assert source.count("beginCooldown(now);") == 2
