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
    assert 40 <= defaults["hit_flash_ms"] <= 60
    assert 300 <= defaults["damage_chip_ms"] <= 650
    assert defaults["phase_backfill_gap_leds"] == 1
    assert 1 <= defaults["phase_backfill_scale"] <= 255
    assert defaults["defeat_blackout_ms"] == 90
    assert defaults["defeat_rainbow_ms"] == 900
    assert defaults["defeat_rainbow_spins"] == 2
    assert defaults["orbit_step_ms"] == 20
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
    assert "SensorConfig sensor" in header
    assert "LedConfig led" in header
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
    assert 'prefs.putUInt("dmg_chip"' in source
    assert 'prefs.putUShort("dig_edges"' in source

    assert 'const char* type = doc["type"] | "config"' in source
    assert 'strcmp(type, "provision")' in source
    assert "stale config_version" in source
    assert 'JsonObjectConst hp = doc["hp"]' in source
    assert 'hp["phase_count"]' in source
    assert 'hp["hits_per_phase"]' in source
    assert 'applyPalette(hp["palette"]' in source
    assert 'JsonObjectConst wifi = doc["wifi"]' in source
    assert 'JsonObjectConst mqtt = doc["mqtt"]' in source
    assert 'JsonObjectConst ota = doc["ota"]' in source
    assert 'wifi["password"] = includeSecrets ? config.wifiPassword : "***"' in source
    assert 'mqtt["password"] = includeSecrets ? config.mqttPassword : "***"' in source
    assert "led pin/type/color_order are hardware-profile build values" in source


def test_hit_target_controller_uses_runtime_config_for_hp_sensor_and_effects() -> None:
    source = read("src/hit_target/target/hit_target_controller.cpp")
    header = read("src/hit_target/target/hit_target_controller.h")

    assert "class HitTargetController" in header
    assert "void applyConfig(const RuntimeConfig& config" in header
    assert "void appendStatus(JsonObject obj) const" in header
    assert "bool isSafeForOta() const" in header
    assert "CRGB leds_[::hit_target::NUM_LEDS]" in header

    assert "return totalHits(config_);" in source
    assert "config_.hp.hitsPerPhase" in source
    assert "phaseColorRgb(config_" in source
    assert "config_.visual.orbitStepMs" in source
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
    assert "delayDamageChipUntil(timers_.hitFlashUntilMs);" in source
    assert "target_.hpRemaining--" in source
    assert "sensor_.armed = false" in source
    assert "if (isLockedOut(now)) return;" in source
    assert "digitalEdges >= config_.sensor.digitalHitMinEdges" in source


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
    assert '"/turrets/' not in topics

    assert "PubSubClient" in read("src/hit_target/mqtt/mqtt_bus.h")
    assert "handleConfigPayload" in bus
    assert "applyRuntimeConfigJson" in bus
    assert "store_->save" in bus
    assert "target_->applyConfig" in bus
    assert "command_reset" in bus
    assert "simulate_hit rejected" in bus
    assert "ota_downloading" in bus

    assert "knolleary/PubSubClient@^2.8" in pio
    assert "bblanchon/ArduinoJson@^6.21.5" in pio
    assert "board_build.partitions = min_spiffs.csv" in pio


def test_hit_target_ota_has_separate_app_hardware_manifest_and_workflow() -> None:
    firmware = read("src/hit_target/app/firmware_info.h")
    manifest = read("src/hit_target/ota/ota_manifest.cpp")
    http_ota = read("src/hit_target/ota/http_ota.cpp")
    main = read("src/hit_target/main.cpp")
    workflow = read(".github/workflows/hit-target-firmware.yml")

    assert 'BB_HIT_TARGET_APP_NAME "battlebang-hit-target"' in firmware
    assert 'BB_HIT_TARGET_HARDWARE "esp32dev-hit-target-ring-v1"' in firmware
    assert "hit-target-manifest.json" in firmware
    assert "manifest.app != BB_HIT_TARGET_APP_NAME" in manifest
    assert "manifest.hardware != BB_HIT_TARGET_HARDWARE" in manifest
    assert "manifest.build <= BB_HIT_TARGET_BUILD" in manifest
    assert "sha256 mismatch" in http_ota
    assert "secureClient.setInsecure()" in http_ota
    assert "commandCenterApprovesPolledOta" in main
    assert "config.otaDesiredBuild" in main
    assert "target.prepareForOta();" in main
    assert "writeOtaRebootMarker(true)" in main

    assert "name: Hit Target Firmware" in workflow
    assert "pio run -e esp32dev_hit_target" in workflow
    assert "src/hit_target/app/version_autogen.h" in workflow
    assert "battlebang-hit-target-${{ steps.version.outputs.version }}.bin" in workflow
    assert "hit-target-manifest.json" in workflow
    assert '--app "battlebang-hit-target"' in workflow
    assert '--hardware "esp32dev-hit-target-ring-v1"' in workflow
    assert "hit-target-v${{ steps.version.outputs.version }}" in workflow
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
    assert "BOOT/GPIO0" in readme
    assert "show-config" in readme
    assert "provision" in readme


def test_hit_target_platformio_env_is_distinct_from_go2_mounted_firmware() -> None:
    pio = read("platformio.ini")
    readme = read("README.md")

    assert "[env:esp32dev_hit_target]" in pio
    assert "+<hit_target/**>" in pio
    assert "+<go2_nixo/**>" in pio
    assert "src/go2_nixo/" in readme
    assert "src/hit_target/" in readme
    assert "Go2-mounted" in readme
    assert "Generic standalone hit target" in readme


def test_legacy_target_module_experiment_is_removed_after_migration() -> None:
    assert not (ROOT / "src" / "target_module-v2").exists()
    assert not (ROOT / "src" / "Wall_Target").exists()


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
    assert "HIT_TARGET_OTA_PUBLIC_MANIFEST_URL=https://github.com/KongPedia/battlebang-esp/releases/latest/download/hit-target-manifest.json" in example
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
    assert doc["hp"] == {
        "phase_count": 3,
        "hits_per_phase": 10,
        "palette": ["#009600", "#BE8200", "#BE0000"],
    }


def test_hit_target_github_action_is_scoped_to_hit_target_folder_changes() -> None:
    workflow = read(".github/workflows/hit-target-firmware.yml")

    assert "Folder-scoped trigger" in workflow
    assert '"src/hit_target/**"' in workflow
    assert '"scripts/hit_target/**"' in workflow
    assert '"scripts/hit_target_config.py"' in workflow
    assert '"scripts/firmware/make_release_manifest.py"' in workflow
    assert '"src/turret_fleet/**"' not in workflow
    assert '"src/go2_nixo/**"' not in workflow
    assert "platformio.ini remains included" in workflow
