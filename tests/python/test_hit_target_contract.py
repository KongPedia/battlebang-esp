from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_hit_target_config_exposes_factory_defaults_for_runtime_config() -> None:
    config = json.loads(read("src/hit_target/config.json"))
    defaults = config["defaults"]

    assert defaults["hp_phase_count"] == 3
    assert defaults["hits_per_phase"] == 5
    assert defaults["max_hits"] == defaults["hp_phase_count"] * defaults["hits_per_phase"]
    assert defaults["hit_threshold"] == 1400
    assert defaults["hit_rearm_threshold"] == 800
    assert defaults["hit_threshold"] > defaults["hit_rearm_threshold"]
    assert defaults["hit_cooldown_ms"] == 200
    assert defaults["hit_rearm_stable_ms"] == 80
    assert defaults["digital_hit_min_edges"] == 2
    assert defaults["digital_isr_debounce_us"] == 5000
    assert "cooldown_blink_ms" not in defaults
    assert "hit_flash_ms" not in defaults
    assert "hp_hit_pulse_ms" not in defaults
    assert "orbit_step_ms" not in defaults
    assert "orbit_tail_leds" not in defaults
    assert defaults["damage_chip_ms"] == 240
    assert defaults["phase_backfill_gap_leds"] == 1
    assert 1 <= defaults["phase_backfill_scale"] <= 255
    assert defaults["defeat_blackout_ms"] == 90
    assert defaults["defeat_rainbow_ms"] == 1900
    assert defaults["defeat_rainbow_spins"] == 2
    assert defaults["activation_mode"] == "always_on"
    assert defaults["activation_linked_device_kind"] == "turret"
    assert defaults["activation_linked_device_id"] == ""
    assert defaults["activation_stale_ms"] == 3000
    assert defaults["led_pin"] == 18
    assert defaults["num_leds"] == 60
    assert defaults["led_type"] == "WS2812B"
    assert defaults["color_order"] == "GRB"
    assert defaults["piezo_do_pin"] == 27
    assert defaults["piezo_ao_pin"] == -1
    assert defaults["reset_button_pin"] == 0


def test_hit_target_uses_mac_derived_identity_not_numbered_profiles() -> None:
    config_text = read("src/hit_target/config.json")
    identity = read("src/hit_target/app/identity.cpp")
    runtime_config = read("src/hit_target/config/runtime_config.cpp")
    script = read("scripts/hit_target_config.py")

    assert "targets" not in json.loads(config_text)
    assert "target_01" not in config_text
    assert "target_01" not in identity
    assert "ESP.getEfuseMac()" in identity
    assert "static_cast<uint8_t>(efuseMac & 0xFF)" in identity
    assert "static_cast<uint8_t>((efuseMac >> 40) & 0xFF)" in identity
    assert "buildDeviceIdentity" in read("src/hit_target/main.cpp")
    assert "config.targetId = identity.targetId" in runtime_config
    assert "BATTLEBANG_HIT_TARGET_BUILD_TARGET_ID" not in read("src/hit_target/build_config.h")
    assert "BATTLEBANG_HIT_TARGET_ID" not in script
    assert "id_source=esp32_efuse_mac" in script


def test_runtime_config_persists_gameplay_network_mqtt_and_ota_in_nvs() -> None:
    header = read("src/hit_target/config/runtime_config.h")
    source = read("src/hit_target/config/runtime_config.cpp")

    assert "struct RuntimeConfig" in header
    assert "String deviceId" in header
    assert "String targetId" in header
    assert "HpConfig hp" in header
    assert "VisualConfig visual" in header
    assert "ActivationConfig activation" in header
    assert "SensorConfig sensor" in header
    assert "LedConfig led" in header
    assert 'String mode = "always_on"' in header
    assert 'String linkedDeviceKind = "turret"' in header
    assert "String linkedDeviceId" in header
    assert "String wifiSsid" in header
    assert "String mqttHost" in header
    assert "bool otaCommandCenterControlled" in header
    assert "applyRuntimeConfigJson" in header
    assert "RuntimeConfigStore" in header

    assert 'constexpr const char* kNamespace = "bb_hit_target"' in source
    assert "Preferences prefs" in source
    assert 'prefs.getString("wifi_ssid"' in source
    assert 'prefs.putString("wifi_pass"' in source
    assert 'prefs.getString("mqtt_host"' in source
    assert 'prefs.putString("mqtt_pass"' in source
    assert 'prefs.getBool("ota_cc"' in source
    assert 'prefs.putUInt("ota_build"' in source
    assert 'prefs.putUShort("hp_per"' in source
    assert 'prefs.putString("act_mode"' in source
    assert 'prefs.putString("act_kind"' in source
    assert 'prefs.putString("act_device"' in source
    assert 'prefs.putUInt("act_stale"' in source
    assert 'prefs.putUInt("dmg_chip"' in source
    assert 'prefs.putUShort("dig_edges"' in source

    assert 'const char* type = doc["type"] | "config"' in source
    assert 'strcmp(type, "provision")' in source
    assert "stale config_version" in source
    assert 'JsonObjectConst hp = doc["hp"]' in source
    assert 'hp["phase_count"]' in source
    assert 'hp["hits_per_phase"]' in source
    assert 'applyPalette(hp["palette"]' in source
    assert 'JsonObjectConst activation = doc["activation"]' in source
    assert 'activation["linked_device_kind"]' in source
    assert 'activation["linked_device_id"]' in source
    assert '"activation.mode must be always_on or linked_device"' in source
    assert 'JsonObjectConst wifi = doc["wifi"]' in source
    assert 'JsonObjectConst mqtt = doc["mqtt"]' in source
    assert 'JsonObjectConst ota = doc["ota"]' in source
    assert 'wifi["password"] = includeSecrets ? config.wifiPassword : "***"' in source
    assert 'mqtt["password"] = includeSecrets ? config.mqttPassword : "***"' in source
    assert "led pin/type/color_order are hardware-profile build values" in source
    assert "activationSubscriptionChanged" in source


def test_hit_target_uses_case_correct_arduino_esp_header_for_linux_ci() -> None:
    assert "#include <ESP.h>" not in read("src/hit_target/main.cpp")
    assert "#include <ESP.h>" not in read("src/hit_target/mqtt/mqtt_bus.cpp")
    assert "#include <Esp.h>" in read("src/hit_target/main.cpp")
    assert "#include <Esp.h>" in read("src/hit_target/mqtt/mqtt_bus.cpp")


def test_hit_target_controller_uses_runtime_config_for_hp_sensor_and_effects() -> None:
    source = read("src/hit_target/target/hit_target_controller.cpp")
    header = read("src/hit_target/target/hit_target_controller.h")

    assert "class HitTargetController" in header
    assert "void applyConfig(const RuntimeConfig& config" in header
    assert "void appendStatus(JsonObject obj) const" in header
    assert "bool isSafeForOta() const" in header
    assert "bool vulnerableNow(uint32_t now) const" in header
    assert "bool applyLinkedDeviceStatus(JsonObjectConst status" in header
    assert "CRGB leds_[::hit_target::NUM_LEDS]" in header

    assert "return totalHits(config_);" in source
    assert "config_.hp.hitsPerPhase" in source
    assert "phaseColorRgb(config_" in source
    assert "config_.activation.mode" in source
    assert "config_.activation.linkedDeviceKind" in source
    assert "config_.activation.linkedDeviceId" in source
    assert source.index('incomingDeviceId = statusString(status, "turret_id");') < source.index(
        'incomingDeviceId = statusString(status, "device_id");'
    )
    assert "ready_for_next_command" in source
    assert 'statusBoolFlag(status, "activation_active", explicitActive)' in source
    assert 'statusBoolFlag(status, "hit_target_active", explicitActive)' in source
    assert 'mode == "WAIT_COMMAND"' in source
    assert 'mode == "PATTERN"' in source
    assert 'mode == "HOME"' not in source.split("bool activeTurretStatus", 1)[1].split("bool activeGenericDeviceStatus", 1)[0]
    assert "config_.visual.damageChipMs" in source
    assert "config_.visual.defeatRainbowMs" in source
    assert "config_.sensor.hitThreshold" in source
    assert "config_.sensor.digitalHitMinEdges" in source
    assert "config_.sensor.digitalIsrDebounceUs" in source
    assert "config_.sensor.hitCooldownMs" in source
    assert "config_.led.numLeds" in source
    assert "config_.led.brightness" in source
    assert "config_.led.maxMa" in source
    assert "config_.reset.buttonHoldMs" in source

    assert "damageChip_.phaseTransition = currentHp > 0" in source
    assert "renderPhaseBackfill" in source
    assert "renderPhaseTransitionReveal" in source
    assert "renderDefeatRainbow" in source
    assert "renderOrbitLayer" not in source
    assert "hitFlashUntilMs" not in source
    assert "hpPulseUntilMs" not in source
    assert "target_.hpRemaining--" in source
    assert "sensor_.armed = false" in source
    assert "if (!vulnerableNow(now)) return;" in source
    assert "const bool vulnerable = vulnerableNow(now);" in source
    assert "wasVulnerable_" in header
    assert "if (wasVulnerable_ || capture_.active || !sensor_.armed)" in source
    assert "if (isLockedOut(now)) return;" in source
    assert "digitalEdges >= config_.sensor.digitalHitMinEdges" in source
    assert "renderFrameAnimated" in source
    assert "renderFrameSignature" in source
    assert "if (!animatedFrame && frameRendered_ && frameSignature == lastFrameSignature_) return;" in source


def test_hit_target_mqtt_topics_and_remote_config_are_target_specific() -> None:
    topics = read("src/hit_target/mqtt/topics.cpp")
    bus = read("src/hit_target/mqtt/mqtt_bus.cpp")
    pio = read("platformio.ini")

    assert 'root + "/devices/" + config.deviceId + "/status"' in topics
    assert 'root + "/devices/" + config.deviceId + "/config"' in topics
    assert 'root + "/devices/" + config.deviceId + "/ota"' in topics
    assert 'root + "/hit_targets/all/ota"' in topics
    assert 'root + "/hit_targets/" + config.targetId' in topics
    assert 'topics.targetCommand = base + "/command"' in topics
    assert 'if (normalized == "turret") return "turrets";' in topics
    assert 'topics.linkedDeviceStatus = root + "/" + linkedDeviceCollection(config.activation.linkedDeviceKind)' in topics
    assert "if (topics.linkedDeviceStatus.length() > 0) result.push_back(topics.linkedDeviceStatus);" in topics

    assert "PubSubClient" in read("src/hit_target/mqtt/mqtt_bus.h")
    assert "handleConfigPayload" in bus
    assert "applyRuntimeConfigJson" in bus
    assert "store_->save" in bus
    assert "target_->applyConfig" in bus
    assert "handleLinkedDeviceStatusPayload" in bus
    assert "activationSubscriptionChanged" in bus
    assert "DeserializationOption::Filter(filter)" in bus
    assert "deserializeJson(doc, payload, length" in bus
    assert "kVerboseMqttPayloadLog = false" in bus
    assert 'Serial.print(" payload=");' not in bus
    assert "kLinkedDeviceStatusDocCapacity" in bus
    assert "kLinkedDeviceStatusDocCapacity = 2048" in bus
    assert "command_reset" in bus
    assert "simulate_hit rejected" in bus
    assert "ota_downloading" in bus

    assert "knolleary/PubSubClient@^2.8" in pio
    assert "bblanchon/ArduinoJson@^6.21.5" in pio
    assert "board_build.partitions = min_spiffs.csv" in pio


def test_hit_target_source_remains_legacy_with_ota_contract_but_no_ci_workflow() -> None:
    firmware = read("src/hit_target/app/firmware_info.h")
    manifest = read("src/hit_target/ota/ota_manifest.cpp")
    http_ota = read("src/hit_target/ota/http_ota.cpp")
    main = read("src/hit_target/main.cpp")

    assert 'BB_HIT_TARGET_APP_NAME "battlebang-hit-target"' in firmware
    assert 'BB_HIT_TARGET_HARDWARE "esp32dev-hit-target-ring-v1"' in firmware
    assert "hit-target-manifest.json" in firmware
    assert '"https://github.com/" BB_HIT_TARGET_RELEASE_REPO' in firmware
    assert '"/releases/download/hit-target-latest/hit-target-manifest.json"' in firmware
    assert "manifest.app != BB_HIT_TARGET_APP_NAME" in manifest
    assert "manifest.hardware != BB_HIT_TARGET_HARDWARE" in manifest
    assert "manifest.build <= BB_HIT_TARGET_BUILD" in manifest
    assert "sha256 mismatch" in http_ota
    assert "secureClient.setInsecure()" in http_ota
    assert "commandCenterApprovesPolledOta" in main
    assert "config.otaDesiredBuild" in main
    assert "target.prepareForOta();" in main
    assert "writeOtaRebootMarker(true)" in main

    assert not (ROOT / ".github/workflows/hit-target-firmware.yml").exists()
    manifest_script = read("scripts/firmware/make_release_manifest.py")
    assert "Create a BattleBang firmware release manifest." in manifest_script
    assert "--app" in manifest_script
    assert "--hardware" in manifest_script


def test_serial_provisioning_keeps_existing_local_reset_and_status() -> None:
    main = read("src/hit_target/main.cpp")
    readme = read("src/hit_target/README.md")

    assert "show-config" in main
    assert "config {json}" in main or "config " in main
    assert "provision {json}" in main or "provision " in main
    assert "clear-config" in main
    assert "start-network" in main
    assert "check-ota" in main
    assert "target.reset(\"serial\")" in main
    assert "target.simulateHit(\"serial\")" in main
    assert "target.appendStatus" in main
    assert "activationSubscriptionChanged(config, next)" in main
    assert "activation subscription changed; reconnecting/resubscribing" in main
    assert "BOOT/GPIO0" in readme
    assert "show-config" in readme
    assert "provision" in readme


def test_hit_target_platformio_env_is_distinct_from_go2_mounted_firmware() -> None:
    pio = read("platformio.ini")
    readme = read("README.md")

    assert "[env:esp32dev_hit_target]" in pio
    assert "+<hit_target/**>" in pio
    assert "+<../firmware/go2_nixo/**>" in pio
    assert "firmware/go2_nixo/" in readme
    assert "src/hit_target/" in readme
    assert "Go2-mounted" in readme
    assert "Generic standalone hit target" in readme


def test_legacy_target_module_experiment_is_removed_after_migration() -> None:
    assert not (ROOT / "src" / "target_module-v2").exists()
    assert not (ROOT / "src" / "Wall_Target").exists()


def test_hit_target_mqtt_helper_can_publish_config_command_and_ota() -> None:
    helper = read("scripts/hit_target/mqtt_command.py")
    bus = read("src/hit_target/mqtt/mqtt_bus.cpp")
    readme = read("src/hit_target/README.md")

    assert 'DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "hit_target" / ".env.hit_target"' in helper
    assert "def publish_mqtt" in helper
    assert "def build_config_payload" in helper
    assert "def build_command_payload" in helper
    assert "def build_bench_open_payload" in helper
    assert "def build_bench_close_payload" in helper
    assert "def build_linked_device_status_payload" in helper
    assert "def linked_device_status_topic" in helper
    assert "def build_ota_payload" in helper
    assert "hit_targets/all/ota" in helper
    assert "HIT_TARGET_MQTT_HOST" in helper
    assert "--hits-per-phase" in helper
    assert "--cooldown-blink-ms" not in helper
    assert "--hit-flash-ms" not in helper
    assert "--hp-hit-pulse-ms" not in helper
    assert "--activation-mode" in helper
    assert "--linked-device-kind" in helper
    assert "--linked-device-id" in helper
    assert "--activation-stale-ms" in helper
    assert "--hit-threshold" in helper
    assert "--led-brightness" in helper
    assert "--reset-button-hold-ms" in helper
    assert "--ota-desired-build" in helper
    assert "--debug-allow-simulate-hit" in helper
    assert "bench-open" in helper
    assert "bench-hit" in helper
    assert "bench-close" in helper
    assert "linked-device-status" in helper
    assert "linked_device_collection" in helper
    assert "client_.unsubscribe(previousTopic.c_str())" in bus
    assert "subscribedTopics_ = topics" in bus
    assert "StaticJsonDocument<512> filter" in bus
    assert '"mode": "PATTERN"' in helper
    assert '"mode": "WAIT_COMMAND"' in helper
    assert "activation.mode to always_on" in helper
    assert "hit-target-mqtt bench-open" in readme
    assert "hit-target-mqtt bench-close" in readme
    assert "hit-target-mqtt linked-device-status turret_4 active" in readme
    assert "hit-target-mqtt linked-device-status turret_4 idle" in readme
    assert "scripts/hit_target/mqtt_command.py config" in readme
    assert "scripts/hit_target/mqtt_command.py ota" in readme


def test_hit_target_serial_heartbeat_is_slow_enough_not_to_stutter_leds() -> None:
    main = read("src/hit_target/main.cpp")

    assert "constexpr uint32_t SERIAL_STATUS_PERIOD_MS = 10000;" in main
    assert "Large JSON writes at 115200 bps can visibly stall LED animation" in main


def test_hit_target_local_env_example_and_gitignore_keep_runtime_secrets_out_of_git() -> None:
    example = read("src/hit_target/.env.hit_target.example")
    gitignore = read(".gitignore")

    assert "Copy to src/hit_target/.env.hit_target" in example
    assert "Do not commit real Wi-Fi/MQTT secrets" in example
    assert "HIT_TARGET_WIFI_SSID=YOUR_WIFI_SSID" in example
    assert "HIT_TARGET_WIFI_PASSWORD=YOUR_WIFI_PASSWORD" in example
    assert "HIT_TARGET_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS" in example
    assert "HIT_TARGET_MQTT_PORT=1883" in example
    assert "HIT_TARGET_MQTT_ROOT=battlebang" in example
    assert "HIT_TARGET_COOLDOWN_BLINK_MS=60" not in example
    assert "HIT_TARGET_DAMAGE_CHIP_MS=240" in example
    assert "HIT_TARGET_ACTIVATION_MODE=always_on" in example
    assert "HIT_TARGET_LINKED_DEVICE_KIND=turret" in example
    assert "HIT_TARGET_LINKED_DEVICE_ID=" in example
    assert "HIT_TARGET_LED_BRIGHTNESS=80" in example
    assert "HIT_TARGET_RESET_BUTTON_HOLD_MS=1200" in example
    assert "HIT_TARGET_OTA_PUBLIC_MANIFEST_URL=https://github.com/KongPedia/battlebang-esp/releases/download/hit-target-latest/hit-target-manifest.json" in example
    assert "src/hit_target/.env.hit_target" in gitignore
    assert "src/*/.env.*" in gitignore
    assert "!src/*/.env*.example" in gitignore


def test_hit_target_provision_helper_maps_env_to_nvs_runtime_config() -> None:
    script_path = ROOT / "scripts" / "hit_target" / "provision.py"
    script = script_path.read_text(encoding="utf-8")

    assert 'DEFAULT_ENV_FILE = PROJECT_ROOT / "src" / "hit_target" / ".env.hit_target"' in script
    assert "def parse_dotenv" in script
    assert "def build_provision_config" in script
    assert "provision {payload}" in script
    assert "serial.Serial" in script
    assert "HIT_TARGET_WIFI_SSID" in script
    assert "HIT_TARGET_WIFI_PASSWORD" in script
    assert "HIT_TARGET_MQTT_HOST" in script
    assert "HIT_TARGET_MQTT_ROOT" in script
    assert "HIT_TARGET_TARGET_ID" in script

    import importlib.util

    spec = importlib.util.spec_from_file_location("hit_target_provision", script_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    doc = module.build_provision_config(
        {
            "HIT_TARGET_CONFIG_VERSION": "123",
            "HIT_TARGET_WIFI_SSID": "lab-wifi",
            "HIT_TARGET_WIFI_PASSWORD": "secret",
            "HIT_TARGET_MQTT_HOST": "10.0.0.5",
            "HIT_TARGET_MQTT_ROOT": "battlebang-dev",
            "HIT_TARGET_HP_PHASE_COUNT": "3",
            "HIT_TARGET_HITS_PER_PHASE": "10",
            "HIT_TARGET_HP_PALETTE": "#009600,#BE8200,#BE0000",
            "HIT_TARGET_DAMAGE_CHIP_MS": "140",
            "HIT_TARGET_ACTIVATION_MODE": "linked_device",
            "HIT_TARGET_LINKED_DEVICE_KIND": "turret",
            "HIT_TARGET_LINKED_DEVICE_ID": "turret_4",
            "HIT_TARGET_LED_BRIGHTNESS": "72",
            "HIT_TARGET_RESET_BUTTON_HOLD_MS": "1500",
            "HIT_TARGET_NETWORK_AUTO_START": "true",
        }
    )
    assert doc["type"] == "provision"
    assert doc["config_version"] == 123
    assert doc["configured"] is True
    assert doc["wifi"] == {"ssid": "lab-wifi", "password": "secret"}
    assert doc["mqtt"]["host"] == "10.0.0.5"
    assert doc["mqtt"]["root"] == "battlebang-dev"
    assert doc["network"]["auto_start"] is True
    assert doc["visual"]["damage_chip_ms"] == 140
    assert doc["visual"]["defeat_rainbow_ms"] == 1900
    assert doc["activation"] == {
        "mode": "linked_device",
        "linked_device_kind": "turret",
        "linked_device_id": "turret_4",
        "stale_ms": 3000,
    }
    assert doc["led"]["brightness"] == 72
    assert doc["reset"]["button_hold_ms"] == 1500
    assert doc["hp"] == {
        "phase_count": 3,
        "hits_per_phase": 10,
        "palette": ["#009600", "#BE8200", "#BE0000"],
    }


def test_hit_target_github_action_removed_while_firmware_ota_action_is_scoped() -> None:
    assert not (ROOT / ".github/workflows/hit-target-firmware.yml").exists()
    workflow = read(".github/workflows/firmware-ota.yml")

    assert "Firmware OTA Releases" in workflow
    assert '"firmware/boss_target/**"' in workflow
    assert '"scripts/firmware/make_release_manifest.py"' in workflow
    assert '"src/hit_target/**"' not in workflow
    assert '"scripts/boss_target/**"' not in workflow
    assert '"scripts/go2/**"' not in workflow
    assert '"scripts/go2_runtime/**"' not in workflow
    assert '"scripts/go2_nixo/**"' not in workflow
    assert '"scripts/heavy_blaster/**"' not in workflow


def test_src_docs_define_firmware_migration_scope_and_ota_contract() -> None:
    readme = read("src/README.md")
    agents = read("src/AGENTS.md")
    root_agents = read("AGENTS.md")

    assert "current compatibility workspace" in readme
    assert "New standardized firmware should live under `firmware/`" in readme
    assert "`src/hit_target/`" in readme
    assert "Unused/retire" in readme
    assert "hit_target` is unused" in readme
    assert "`firmware/go2/` — already moved" in readme
    assert "`firmware/go2_nixo/` — already moved" in readme
    assert "turret-fleet-latest/manifest.json" in readme
    assert "hit-target-latest/hit-target-manifest.json" not in readme

    assert "Applies to legacy/reference firmware folders under `src/**`" in agents
    assert "If copying values between ignored env files" in agents
    assert "Do not use GitHub's repo-wide `/releases/latest/download/...`" in agents
    assert "hit-target-latest" in agents
    assert "turret-fleet-latest" in agents

    assert "src/hit_target/.env.hit_target" in root_agents
    assert "BB_HIT_TARGET" not in root_agents  # keep root guidance operational, not compile-flag specific
