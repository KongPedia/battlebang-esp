from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_common_nvs_store_is_key_mapped_and_raii_scoped() -> None:
    header = read("lib/bb_esp_nvs/src/bb_esp_nvs/common_runtime_config_store.h")
    source = read("lib/bb_esp_nvs/src/bb_esp_nvs/common_runtime_config_store.cpp")
    library = read("lib/bb_esp_nvs/library.json")

    assert "class ScopedPreferences" in header
    assert "ScopedPreferences::~ScopedPreferences()" in source
    assert "preferences_.end();" in source
    assert "struct CommonRuntimeConfigKeys" in header
    assert "standardCommonRuntimeConfigKeys" in header
    assert "struct CommonRuntimeConfigSavePolicy" in header
    assert "loadCommonRuntimeConfig" in header
    assert "saveCommonRuntimeConfig" in header
    assert "clearNamespace(const char* nvsNamespace)" in header
    assert "bb_esp_nvs" in read("lib/bb_esp_nvs/library.json")
    assert "nvs" not in read("lib/bb_esp_core/library.json")
    assert "prefs.putString(key, value)" in source
    assert "policy.requireMqttRoot" in source
    assert '"nvs"' in library
    assert 'keys.deviceId = "device_id";' in header
    assert 'keys.stageId = "stage_id";' in header
    assert 'keys.wifiSsid = "wifi_ssid";' in header
    assert 'keys.mqttHost = "mqtt_host";' in header
    assert 'keys.otaPublicManifestUrl = "ota_pub_url";' in header


def test_active_firmware_config_stores_use_common_nvs_without_renaming_existing_keys() -> None:
    boss = read("firmware/boss_target/config/runtime_config.cpp")
    heavy = read("firmware/heavy_blaster/config/runtime_config.cpp")
    turret = read("firmware/turret_fleet/config/runtime_config.cpp")

    for source in (boss, heavy, turret):
        assert "#include <bb_esp_nvs/common_runtime_config_store.h>" in source
        assert "battlebang::esp::nvs::CommonRuntimeConfigKeys commonNvsKeys()" in source
        assert "battlebang::esp::config::CommonRuntimeConfig toCommonRuntimeConfig" in source
        assert "void applyCommonRuntimeConfig" in source
        assert "loadCommonRuntimeConfig(prefs, common, commonNvsKeys())" in source
        assert "saveCommonRuntimeConfig(" in source
        assert "clearNamespace(" in source
        assert 'prefs.getString("wifi_ssid"' not in source
        assert 'prefs.putString("wifi_ssid"' not in source
        assert 'prefs.getString("mqtt_root"' not in source
        assert 'prefs.putString("mqtt_root"' not in source
        assert 'prefs.getString("ota_pub"' not in source
        assert 'prefs.putString("ota_pub"' not in source

    # Existing persisted key names stay firmware-specific for backward compatibility.
    assert 'const char* kConfigNamespace = "boss_target";' in boss
    assert 'keys.otaChannel = "ota_channel";' in boss
    assert 'keys.otaPublicManifestUrl = "ota_pub";' in boss
    assert 'keys.otaCheckIntervalS = "ota_secs";' in boss
    assert "keys.schema =" not in boss
    assert 'keys.deviceId = "device_id";' in boss
    assert "policy.requireOtaPublicManifestUrl = true;" in boss

    assert 'const char* kConfigNamespace = "heavy_blaster";' in heavy
    assert 'keys.schema = "schema";' in heavy
    assert 'keys.deviceId = "device_id";' in heavy
    assert 'keys.stageId = "stage_id";' in heavy
    assert 'keys.otaChannel = "ota_chan";' in heavy
    assert 'keys.otaCheckIntervalS = "ota_int";' in heavy
    assert 'prefs.getBool("has_cfg", false)' in heavy
    assert 'prefs.putBool("has_cfg", true)' in heavy

    assert 'const char* kNamespace = "bb_fleet";' in turret
    assert 'keys.schema = "schema";' in turret
    assert 'keys.stageId = "stage_id";' in turret
    assert 'keys.otaPublicManifestUrl = "ota_pub_url";' in turret
    assert 'keys.otaLocalMirrorUrl = "ota_mir_url";' in turret
    assert 'keys.otaCheckIntervalS = "ota_int_s";' in turret
    assert 'policy.requireMqttRoot = false;' in turret
    assert 'keys.deviceId = "device_id";' in turret


def test_core_build_time_config_adapter_is_header_only_and_normalizes_common_fields() -> None:
    header = read("lib/bb_esp_core/src/bb_esp_core/config/build_time_config.h")
    library = read("lib/bb_esp_core/library.json")

    assert "struct BuildTimeCommonRuntimeConfigDefaults" in header
    assert "makeBuildTimeCommonRuntimeConfig" in header
    assert "config.configured = hasValue(defaults.wifiSsid) && hasValue(defaults.mqttHost);" in header
    assert "config.deviceId = safeString(defaults.deviceId);" in header
    assert "config.stageId = safeString(defaults.stageId);" in header
    assert "config.mqttPort = defaults.mqttPort;" in header
    assert "normalizeRootOrDefault" in header
    assert "Preferences" not in header
    assert "HTTPClient" not in header
    assert "PubSubClient" not in header
    assert "build_time_config.cpp" not in library


def test_core_device_topic_helper_namespaces_by_device_type() -> None:
    header = read("lib/bb_esp_core/src/bb_esp_core/mqtt/device_topics.h")

    assert 'joinTopic(normalizeRootOrDefault(root), "devices", deviceType, deviceId)' in header
    assert "makeDeviceTopics(const String& root, const String& deviceType, const String& deviceId)" in header
    assert "makeDeviceTopicsChecked(const String& root," in header
    assert "isSafeTopicSegment(deviceType)" in header
    assert '"device_type must use only' in header


def test_core_string_buffer_helper_is_header_only_and_reports_truncation_status() -> None:
    header = read("lib/bb_esp_core/src/bb_esp_core/config/string_buffer.h")
    library = read("lib/bb_esp_core/library.json")

    assert "copyToFixedBuffer" in header
    assert "value.toCharArray(buffer, length);" in header
    assert "return value.length() < length;" in header
    assert "Preferences" not in header
    assert "HTTPClient" not in header
    assert "PubSubClient" not in header
    assert "string_buffer.cpp" not in library


def test_core_runtime_config_json_helper_keeps_common_json_policy_out_of_firmware_folders() -> None:
    header = read("lib/bb_esp_core/src/bb_esp_core/config/runtime_config_json.h")
    library = read("lib/bb_esp_core/library.json")

    assert "applyCommonRuntimeConfigJson" in header
    assert "writeCommonRuntimeConfigJson" in header
    assert "validateCommonRuntimeConfig" in header
    assert "normalizeCommonRuntimeConfig" in header
    assert "validateOtaManifestUrl" in header
    assert "isExamplePlaceholderUrl" in header
    assert "example.invalid" in header
    assert "example.com" in header
    assert 'validateOtaManifestUrl(config.otaPublicManifestUrl, "ota.public_manifest_url", error)' in header
    assert 'validateOtaManifestUrl(config.otaLocalMirrorUrl, "ota.local_mirror_url", error)' in header
    assert "must use a real release manifest URL, not an example placeholder URL" in header
    assert "config_version is required" in header
    assert "config_version must not go backwards" in header
    assert "wifi.ssid is required when configured=true" in header
    assert "mqtt.host is required when configured=true" in header
    assert "stage_id must use only" in header
    assert "normalizeConfiguredRoot(config.mqttRoot, error)" in header
    assert "JsonObjectConst wifi = doc[\"wifi\"].as<JsonObjectConst>();" in header
    assert "JsonObjectConst mqtt = doc[\"mqtt\"].as<JsonObjectConst>();" in header
    assert "JsonObjectConst ota = doc[\"ota\"].as<JsonObjectConst>();" in header
    assert 'doc["config_version"] = config.configVersion;' in header
    assert 'doc["stage_id"] = config.stageId;' in header
    assert 'wifi["password"] = maskedSecret(config.wifiPassword, includeSecrets);' in header
    assert 'mqtt["password"] = maskedSecret(config.mqttPassword, includeSecrets);' in header
    assert "Preferences" not in header
    assert "HTTPClient" not in header
    assert "PubSubClient" not in header
    assert "runtime_config_json.cpp" not in library


def test_core_ota_policy_status_helper_is_header_only_and_does_not_pull_ota_transport() -> None:
    header = read("lib/bb_esp_core/src/bb_esp_core/config/ota_policy_status_json.h")
    library = read("lib/bb_esp_core/library.json")

    assert "writeUnsupportedOtaStatus" in header
    assert 'doc["ota_supported"] = false;' in header
    assert 'doc["ota_state"] = "unsupported";' in header
    assert 'doc["ota_policy_command_center_controlled"]' in header
    assert 'doc["ota_policy_auto_check_enabled"]' in header
    assert 'doc["ota_policy_channel"]' in header
    assert 'doc["ota_policy_desired_build"]' in header
    assert 'doc["ota_policy_apply_only_in_safe_state"]' in header
    assert "HTTPClient" not in header
    assert "Update" not in header
    assert "WiFiClientSecure" not in header
    assert "ota_policy_status_json.cpp" not in library


def test_ota_transport_lives_in_bb_esp_ota_not_per_firmware_copy_paste() -> None:
    header = read("lib/bb_esp_ota/src/bb_esp_ota/http_ota.h")
    source = read("lib/bb_esp_ota/src/bb_esp_ota/http_ota.cpp")
    library = read("lib/bb_esp_ota/library.json")
    boss_wrapper = read("firmware/boss_target/ota/http_ota.h") + read("firmware/boss_target/ota/http_ota.cpp")
    turret_wrapper = read("firmware/turret_fleet/ota/http_ota.h") + read("firmware/turret_fleet/ota/http_ota.cpp")

    assert "struct OtaResult" in header
    assert "fetchHttpText" in header
    assert "runHttpOta" in header
    assert "#include <HTTPClient.h>" in source
    assert "#include <Update.h>" in source
    assert "#include <WiFiClientSecure.h>" in source
    assert "secureClient.setCACert(kGithubReleaseRootCaPem)" in source
    assert "release-assets.githubusercontent.com" in source
    assert source.count("-----BEGIN CERTIFICATE-----") >= 2
    assert "Update.write(buffer, bytesRead)" in source
    assert "sha256 mismatch" in source
    assert "HTTP update" in library

    for wrapper in (boss_wrapper, turret_wrapper):
        assert "#include <bb_esp_ota/http_ota.h>" in wrapper
        assert "battlebang::esp::ota::fetchHttpText" in wrapper
        assert "battlebang::esp::ota::runHttpOta" in wrapper
        assert "#include <HTTPClient.h>" not in wrapper
        assert "#include <Update.h>" not in wrapper
        assert "setInsecure" not in wrapper
