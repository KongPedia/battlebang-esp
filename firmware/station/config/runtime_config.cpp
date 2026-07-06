#include "runtime_config.h"

#include <ArduinoJson.h>
#include <bb_esp_core/config/common_runtime_config.h>
#include <bb_esp_core/config/runtime_config_json.h>
#include <bb_esp_core/mqtt/topic_utils.h>
#include <bb_esp_nvs/common_runtime_config_store.h>

#include "station/app/firmware_info.h"

namespace battlebang {
namespace station {
namespace {

constexpr uint16_t kMinHitThreshold = 1;
constexpr uint16_t kMaxAdcValue = 4095;
constexpr uint32_t kMinHitCooldownMs = 20;
constexpr uint32_t kMaxHitCooldownMs = 10000;
constexpr uint32_t kMaxAutoResetMs = 3600000;
constexpr uint32_t kMinHeartbeatMs = 1000;
constexpr uint32_t kMaxHeartbeatMs = 60000;

const char* kConfigNamespace = "station";

battlebang::esp::nvs::CommonRuntimeConfigKeys commonNvsKeys() {
  battlebang::esp::nvs::CommonRuntimeConfigKeys keys;
  keys.schema = "schema";
  keys.configVersion = "cfg_ver";
  keys.configured = "configured";
  keys.deviceId = "device_id";
  keys.group = "group";
  keys.stageId = "stage_id";
  keys.location = "location";
  keys.wifiSsid = "wifi_ssid";
  keys.wifiPassword = "wifi_pass";
  keys.networkAutoStart = "net_auto";
  keys.networkStartDelayMs = "net_delay";
  keys.mqttHost = "mqtt_host";
  keys.mqttPort = "mqtt_port";
  keys.mqttUsername = "mqtt_user";
  keys.mqttPassword = "mqtt_pass";
  keys.mqttRoot = "mqtt_root";
  keys.otaCommandCenterControlled = "ota_cc";
  keys.otaAutoCheckEnabled = "ota_auto";
  keys.otaChannel = "ota_channel";
  keys.otaDesiredBuild = "ota_build";
  keys.otaPublicManifestUrl = "ota_pub";
  keys.otaLocalMirrorUrl = "ota_mirror";
  keys.otaCheckIntervalS = "ota_secs";
  keys.otaApplyOnlyInSafeState = "ota_safe";
  return keys;
}

battlebang::esp::nvs::CommonRuntimeConfigSavePolicy commonNvsSavePolicy() {
  battlebang::esp::nvs::CommonRuntimeConfigSavePolicy policy;
  policy.requireMqttRoot = true;
  policy.requireOtaChannel = true;
  policy.requireOtaPublicManifestUrl = true;
  return policy;
}

battlebang::esp::config::CommonRuntimeConfig toCommonRuntimeConfig(const RuntimeConfig& config) {
  battlebang::esp::config::CommonRuntimeConfig common;
  common.schema = config.schema;
  common.configVersion = config.configVersion;
  common.configured = config.configured;
  common.deviceId = config.deviceId;
  common.group = config.group;
  common.stageId = config.stageId;
  common.location = config.location;
  common.wifiSsid = config.wifiSsid;
  common.wifiPassword = config.wifiPassword;
  common.networkAutoStart = config.networkAutoStart;
  common.networkStartDelayMs = config.networkStartDelayMs;
  common.mqttHost = config.mqttHost;
  common.mqttPort = config.mqttPort;
  common.mqttUsername = config.mqttUsername;
  common.mqttPassword = config.mqttPassword;
  common.mqttRoot = config.mqttRoot;
  common.otaCommandCenterControlled = config.otaCommandCenterControlled;
  common.otaAutoCheckEnabled = config.otaAutoCheckEnabled;
  common.otaChannel = config.otaChannel;
  common.otaDesiredBuild = config.otaDesiredBuild;
  common.otaPublicManifestUrl = config.otaPublicManifestUrl;
  common.otaLocalMirrorUrl = config.otaLocalMirrorUrl;
  common.otaCheckIntervalS = config.otaCheckIntervalS;
  common.otaApplyOnlyInSafeState = config.otaApplyOnlyInSafeState;
  return common;
}

void applyCommonRuntimeConfig(RuntimeConfig& config,
                              const battlebang::esp::config::CommonRuntimeConfig& common) {
  config.schema = common.schema;
  config.configVersion = common.configVersion;
  config.configured = common.configured;
  config.deviceId = common.deviceId;
  config.group = common.group;
  config.stageId = common.stageId;
  config.location = common.location;
  config.wifiSsid = common.wifiSsid;
  config.wifiPassword = common.wifiPassword;
  config.networkAutoStart = common.networkAutoStart;
  config.networkStartDelayMs = common.networkStartDelayMs;
  config.mqttHost = common.mqttHost;
  config.mqttPort = common.mqttPort;
  config.mqttUsername = common.mqttUsername;
  config.mqttPassword = common.mqttPassword;
  config.mqttRoot = common.mqttRoot;
  config.otaCommandCenterControlled = common.otaCommandCenterControlled;
  config.otaAutoCheckEnabled = common.otaAutoCheckEnabled;
  config.otaChannel = common.otaChannel;
  config.otaDesiredBuild = common.otaDesiredBuild;
  config.otaPublicManifestUrl = common.otaPublicManifestUrl;
  config.otaLocalMirrorUrl = common.otaLocalMirrorUrl;
  config.otaCheckIntervalS = common.otaCheckIntervalS;
  config.otaApplyOnlyInSafeState = common.otaApplyOnlyInSafeState;
}

String getStringOr(JsonVariantConst value, const String& fallback) {
  if (value.isNull()) return fallback;
  if (value.is<const char*>()) return String(value.as<const char*>());
  String out;
  serializeJson(value, out);
  return out;
}

uint32_t getUIntOr(JsonVariantConst value, uint32_t fallback) {
  if (value.isNull()) return fallback;
  if (value.is<unsigned long>()) return value.as<uint32_t>();
  if (value.is<long>()) {
    long v = value.as<long>();
    return v < 0 ? fallback : static_cast<uint32_t>(v);
  }
  if (value.is<const char*>()) {
    char* end = nullptr;
    unsigned long parsed = strtoul(value.as<const char*>(), &end, 10);
    return end != value.as<const char*>() ? static_cast<uint32_t>(parsed) : fallback;
  }
  return fallback;
}

int32_t getIntOr(JsonVariantConst value, int32_t fallback) {
  if (value.isNull()) return fallback;
  if (value.is<long>()) return value.as<int32_t>();
  if (value.is<const char*>()) {
    char* end = nullptr;
    long parsed = strtol(value.as<const char*>(), &end, 10);
    return end != value.as<const char*>() ? static_cast<int32_t>(parsed) : fallback;
  }
  return fallback;
}

bool getBoolOr(JsonVariantConst value, bool fallback) {
  if (value.isNull()) return fallback;
  if (value.is<bool>()) return value.as<bool>();
  if (value.is<const char*>()) {
    String s = value.as<const char*>();
    s.trim();
    s.toLowerCase();
    if (s == "1" || s == "true" || s == "yes" || s == "on" || s == "enabled") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off" || s == "disabled") return false;
  }
  return fallback;
}

uint32_t parseColor(JsonVariantConst value, uint32_t fallback) {
  if (value.isNull()) return fallback;
  if (value.is<unsigned long>() || value.is<long>()) return getUIntOr(value, fallback) & 0xFFFFFFUL;
  if (!value.is<const char*>()) return fallback;
  String s = value.as<const char*>();
  s.trim();
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.startsWith("0x") || s.startsWith("0X")) s.remove(0, 2);
  if (s.length() == 0 || s.length() > 6) return fallback;
  char* end = nullptr;
  unsigned long parsed = strtoul(s.c_str(), &end, 16);
  return end != s.c_str() ? (parsed & 0xFFFFFFUL) : fallback;
}

String colorString(uint32_t rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06lX", static_cast<unsigned long>(rgb & 0xFFFFFFUL));
  return String(buf);
}

void normalizeIdentity(RuntimeConfig& config) {
  config.deviceId.trim();
  config.stationId.trim();
  config.displayName.trim();
  config.group.trim();
  config.stageId.trim();
  config.location.trim();
  if (config.stationId.length() == 0) config.stationId = config.deviceId;
  if (config.deviceId.length() == 0) config.deviceId = config.stationId;
  if (config.displayName.length() == 0) config.displayName = config.stationId;
}

bool validateTopicSegment(const String& value, const char* field, String& error) {
  if (value.length() == 0) {
    error = String(field) + " is required";
    return false;
  }
  if (!battlebang::esp::mqtt::isSafeTopicSegment(value)) {
    error = String(field) + " must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  return true;
}

bool validatePinProfile(const RuntimeConfig& config, String& error) {
  if (config.hardware.piezoPin != ::station::PIEZO_AO_PIN) {
    error = "hardware_profile.piezo_pin must match compiled target-station board profile";
    return false;
  }
  if (config.hardware.ledPin != ::station::LED_PIN) {
    error = "hardware_profile.led_pin must match compiled target-station board profile";
    return false;
  }
  if (config.hardware.ledType != ::station::LED_TYPE_NAME) {
    error = "hardware_profile.led_type must match compiled target-station board profile";
    return false;
  }
  if (config.hardware.colorOrder != ::station::COLOR_ORDER_NAME) {
    error = "hardware_profile.color_order must match compiled target-station board profile";
    return false;
  }
  return true;
}

bool validateConfig(RuntimeConfig& config, String& error) {
  normalizeIdentity(config);
  battlebang::esp::config::CommonRuntimeConfig common = toCommonRuntimeConfig(config);
  if (!battlebang::esp::config::validateCommonRuntimeConfig(common, error)) return false;
  applyCommonRuntimeConfig(config, common);
  normalizeIdentity(config);

  if (!validateTopicSegment(config.deviceId, "device_id", error)) return false;
  if (!validateTopicSegment(config.stationId, "station_id", error)) return false;
  if (config.sensor.hitThreshold < kMinHitThreshold || config.sensor.hitThreshold > kMaxAdcValue) {
    error = "sensor.hit_threshold must be 1..4095";
    return false;
  }
  if (config.sensor.releaseThreshold >= config.sensor.hitThreshold) {
    error = "sensor.release_threshold must be below hit_threshold";
    return false;
  }
  if (config.sensor.hitCooldownMs < kMinHitCooldownMs || config.sensor.hitCooldownMs > kMaxHitCooldownMs) {
    error = "sensor.hit_cooldown_ms out of range";
    return false;
  }
  if (config.sensor.sampleIntervalMs == 0 || config.sensor.sampleIntervalMs > 1000) {
    error = "sensor.sample_interval_ms out of range";
    return false;
  }
  if (config.led.numLeds == 0 || config.led.numLeds > ::station::MAX_LED_NUM_LEDS) {
    error = "led.num_leds out of range";
    return false;
  }
  if (config.led.waitingBreathMin > config.led.waitingBreathMax) {
    error = "led.waiting_breath_min must be <= waiting_breath_max";
    return false;
  }
  if (config.gameplay.autoResetMs > kMaxAutoResetMs) {
    error = "gameplay.auto_reset_ms out of range";
    return false;
  }
  if (config.gameplay.heartbeatIntervalMs < kMinHeartbeatMs || config.gameplay.heartbeatIntervalMs > kMaxHeartbeatMs) {
    error = "gameplay.heartbeat_interval_ms out of range";
    return false;
  }
  if (!validatePinProfile(config, error)) return false;
  return true;
}

void copyConnectivityConfig(RuntimeConfig& dest, const RuntimeConfig& source) {
  dest.configVersion = source.configVersion;
  dest.configured = source.configured;
  dest.deviceId = source.deviceId;
  dest.stationId = source.stationId;
  dest.displayName = source.displayName;
  dest.group = source.group;
  dest.stageId = source.stageId;
  dest.location = source.location;
  dest.wifiSsid = source.wifiSsid;
  dest.wifiPassword = source.wifiPassword;
  dest.networkAutoStart = source.networkAutoStart;
  dest.networkStartDelayMs = source.networkStartDelayMs;
  dest.mqttHost = source.mqttHost;
  dest.mqttPort = source.mqttPort;
  dest.mqttUsername = source.mqttUsername;
  dest.mqttPassword = source.mqttPassword;
  dest.mqttRoot = source.mqttRoot;
  dest.otaCommandCenterControlled = source.otaCommandCenterControlled;
  dest.otaAutoCheckEnabled = source.otaAutoCheckEnabled;
  dest.otaChannel = source.otaChannel;
  dest.otaDesiredBuild = source.otaDesiredBuild;
  dest.otaPublicManifestUrl = source.otaPublicManifestUrl;
  dest.otaLocalMirrorUrl = source.otaLocalMirrorUrl;
  dest.otaCheckIntervalS = source.otaCheckIntervalS;
  dest.otaApplyOnlyInSafeState = source.otaApplyOnlyInSafeState;
}

}  // namespace

RuntimeConfig makeDefaultRuntimeConfig(const DeviceIdentity& identity) {
  RuntimeConfig config;
  config.deviceId = identity.deviceId;
  config.stationId = identity.stationId;
  config.displayName = identity.stationId;
  config.deviceMac = identity.mac;
  config.otaPublicManifestUrl = BB_STATION_LATEST_MANIFEST_URL;
  return config;
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid json: ") + err.c_str();
    return false;
  }
  if (!doc.is<JsonObject>()) {
    error = "config JSON must be an object";
    return false;
  }
  if (!doc.containsKey("config_version") && !doc.containsKey("cfg_ver")) {
    error = "config_version is required";
    return false;
  }
  const uint32_t incomingVersion = getUIntOr(doc["config_version"].isNull() ? doc["cfg_ver"] : doc["config_version"], 0);
  if (incomingVersion == 0) {
    error = "config_version must be positive";
    return false;
  }
  if (config.configVersion != 0 && incomingVersion < config.configVersion) {
    error = "config_version must not go backwards";
    return false;
  }

  RuntimeConfig next = config;
  battlebang::esp::config::CommonRuntimeConfig common = toCommonRuntimeConfig(next);
  if (!battlebang::esp::config::applyCommonRuntimeConfigJson(doc.as<JsonObjectConst>(), common, error)) return false;
  applyCommonRuntimeConfig(next, common);

  next.stationId = getStringOr(doc["station_id"], next.stationId);
  next.stationId = getStringOr(doc["stationId"], next.stationId);
  next.displayName = getStringOr(doc["display_name"], next.displayName);
  next.displayName = getStringOr(doc["name"], next.displayName);
  next.debugAllowSimulateHit = getBoolOr(doc["debug_allow_simulate_hit"], next.debugAllowSimulateHit);

  JsonObjectConst sensor = doc["sensor"].as<JsonObjectConst>();
  if (!sensor.isNull()) {
    next.sensor.hitThreshold = static_cast<uint16_t>(getUIntOr(sensor["hit_threshold"], next.sensor.hitThreshold));
    next.sensor.releaseThreshold = static_cast<uint16_t>(getUIntOr(sensor["release_threshold"], next.sensor.releaseThreshold));
    next.sensor.hitCooldownMs = getUIntOr(sensor["hit_cooldown_ms"], next.sensor.hitCooldownMs);
    next.sensor.sampleIntervalMs = getUIntOr(sensor["sample_interval_ms"], next.sensor.sampleIntervalMs);
    next.sensor.settleUs = getUIntOr(sensor["settle_us"], next.sensor.settleUs);
  }

  JsonObjectConst led = doc["led"].as<JsonObjectConst>();
  if (!led.isNull()) {
    next.led.numLeds = static_cast<uint16_t>(getUIntOr(led["num_leds"], next.led.numLeds));
    next.led.brightness = static_cast<uint8_t>(getUIntOr(led["brightness"], next.led.brightness));
    next.led.maxMa = static_cast<uint16_t>(getUIntOr(led["max_ma"], next.led.maxMa));
    next.led.waitingColor = parseColor(led["waiting_color"], next.led.waitingColor);
    next.led.capturedColor = parseColor(led[String("captured") + "_color"], next.led.capturedColor);
    next.led.hitFlashColor = parseColor(led["hit_flash_color"], next.led.hitFlashColor);
    next.led.waitingBreathBpm = static_cast<uint16_t>(getUIntOr(led["waiting_breath_bpm"], next.led.waitingBreathBpm));
    next.led.waitingBreathMin = static_cast<uint8_t>(getUIntOr(led["waiting_breath_min"], next.led.waitingBreathMin));
    next.led.waitingBreathMax = static_cast<uint8_t>(getUIntOr(led["waiting_breath_max"], next.led.waitingBreathMax));
  }

  JsonObjectConst gameplay = doc["gameplay"].as<JsonObjectConst>();
  if (!gameplay.isNull()) {
    next.gameplay.lockAfterHit = getBoolOr(gameplay["lock_after_hit"], next.gameplay.lockAfterHit);
    next.gameplay.autoResetMs = getUIntOr(gameplay["auto_reset_ms"], next.gameplay.autoResetMs);
    next.gameplay.heartbeatIntervalMs = getUIntOr(gameplay["heartbeat_interval_ms"], next.gameplay.heartbeatIntervalMs);
  }

  JsonObjectConst hw = doc["hardware_profile"].as<JsonObjectConst>();
  if (!hw.isNull()) {
    next.hardware.piezoPin = static_cast<int16_t>(getIntOr(hw["piezo_pin"], next.hardware.piezoPin));
    next.hardware.ledPin = static_cast<int16_t>(getIntOr(hw["led_pin"], next.hardware.ledPin));
    next.hardware.ledType = getStringOr(hw["led_type"], next.hardware.ledType);
    next.hardware.colorOrder = getStringOr(hw["color_order"], next.hardware.colorOrder);
  }

  if (!validateConfig(next, error)) return false;
  config = next;
  return true;
}

String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets) {
  DynamicJsonDocument doc(4096);
  battlebang::esp::config::writeCommonRuntimeConfigJson(doc.to<JsonObject>(), toCommonRuntimeConfig(config), includeSecrets);
  doc["station_id"] = config.stationId;
  doc["display_name"] = config.displayName;
  doc["device_mac"] = config.deviceMac;
  doc["debug_allow_simulate_hit"] = config.debugAllowSimulateHit;

  JsonObject sensor = doc.createNestedObject("sensor");
  sensor["hit_threshold"] = config.sensor.hitThreshold;
  sensor["release_threshold"] = config.sensor.releaseThreshold;
  sensor["hit_cooldown_ms"] = config.sensor.hitCooldownMs;
  sensor["sample_interval_ms"] = config.sensor.sampleIntervalMs;
  sensor["settle_us"] = config.sensor.settleUs;

  JsonObject led = doc.createNestedObject("led");
  led["num_leds"] = config.led.numLeds;
  led["brightness"] = config.led.brightness;
  led["max_ma"] = config.led.maxMa;
  led["waiting_color"] = colorString(config.led.waitingColor);
  led[String("captured") + "_color"] = colorString(config.led.capturedColor);
  led["hit_flash_color"] = colorString(config.led.hitFlashColor);
  led["waiting_breath_bpm"] = config.led.waitingBreathBpm;
  led["waiting_breath_min"] = config.led.waitingBreathMin;
  led["waiting_breath_max"] = config.led.waitingBreathMax;

  JsonObject gameplay = doc.createNestedObject("gameplay");
  gameplay["lock_after_hit"] = config.gameplay.lockAfterHit;
  gameplay["auto_reset_ms"] = config.gameplay.autoResetMs;
  gameplay["heartbeat_interval_ms"] = config.gameplay.heartbeatIntervalMs;

  JsonObject hw = doc.createNestedObject("hardware_profile");
  hw["piezo_pin"] = config.hardware.piezoPin;
  hw["led_pin"] = config.hardware.ledPin;
  hw["led_type"] = config.hardware.ledType;
  hw["color_order"] = config.hardware.colorOrder;

  String out;
  serializeJson(doc, out);
  return out;
}

bool RuntimeConfigStore::load(RuntimeConfig& config) {
  battlebang::esp::nvs::ScopedPreferences scopedPrefs;
  if (!scopedPrefs.begin(kConfigNamespace, true)) return false;
  Preferences& prefs = scopedPrefs.preferences();

  battlebang::esp::config::CommonRuntimeConfig common = toCommonRuntimeConfig(config);
  loadCommonRuntimeConfig(prefs, common, commonNvsKeys());
  RuntimeConfig loaded = config;
  applyCommonRuntimeConfig(loaded, common);
  loaded.stationId = prefs.getString("station_id", loaded.stationId);
  loaded.displayName = prefs.getString("display", loaded.displayName);
  loaded.debugAllowSimulateHit = prefs.getBool("dbg_sim", loaded.debugAllowSimulateHit);
  loaded.sensor.hitThreshold = prefs.getUShort("hit_thr", loaded.sensor.hitThreshold);
  loaded.sensor.releaseThreshold = prefs.getUShort("rel_thr", loaded.sensor.releaseThreshold);
  loaded.sensor.hitCooldownMs = prefs.getUInt("hit_cd", loaded.sensor.hitCooldownMs);
  loaded.sensor.sampleIntervalMs = prefs.getUInt("sample_ms", loaded.sensor.sampleIntervalMs);
  loaded.sensor.settleUs = prefs.getUInt("settle_us", loaded.sensor.settleUs);
  loaded.led.numLeds = prefs.getUShort("led_count", loaded.led.numLeds);
  loaded.led.brightness = static_cast<uint8_t>(prefs.getUChar("led_bright", loaded.led.brightness));
  loaded.led.maxMa = prefs.getUShort("led_max_ma", loaded.led.maxMa);
  loaded.led.waitingColor = prefs.getUInt("wait_color", loaded.led.waitingColor);
  loaded.led.capturedColor = prefs.getUInt("cap_color", loaded.led.capturedColor);
  loaded.led.hitFlashColor = prefs.getUInt("hit_color", loaded.led.hitFlashColor);
  loaded.led.waitingBreathBpm = prefs.getUShort("breath_bpm", loaded.led.waitingBreathBpm);
  loaded.led.waitingBreathMin = prefs.getUChar("breath_min", loaded.led.waitingBreathMin);
  loaded.led.waitingBreathMax = prefs.getUChar("breath_max", loaded.led.waitingBreathMax);
  loaded.gameplay.lockAfterHit = prefs.getBool("lock_hit", loaded.gameplay.lockAfterHit);
  loaded.gameplay.autoResetMs = prefs.getUInt("auto_rst", loaded.gameplay.autoResetMs);
  loaded.gameplay.heartbeatIntervalMs = prefs.getUInt("hb_ms", loaded.gameplay.heartbeatIntervalMs);
  loaded.hardware.piezoPin = prefs.getShort("piezo_pin", loaded.hardware.piezoPin);
  loaded.hardware.ledPin = prefs.getShort("led_pin", loaded.hardware.ledPin);
  loaded.hardware.ledType = prefs.getString("led_type", loaded.hardware.ledType);
  loaded.hardware.colorOrder = prefs.getString("color_order", loaded.hardware.colorOrder);

  String error;
  if (!validateConfig(loaded, error)) {
    Serial.print("[station][config] stored config invalid: ");
    Serial.println(error);
    RuntimeConfig salvaged = config;
    copyConnectivityConfig(salvaged, loaded);
    String salvageError;
    if (!validateConfig(salvaged, salvageError)) return false;
    config = salvaged;
    return true;
  }
  config = loaded;
  return config.configured;
}

bool RuntimeConfigStore::save(const RuntimeConfig& config) {
  RuntimeConfig copy = config;
  String error;
  if (!validateConfig(copy, error)) {
    Serial.print("[station][config] refusing to save invalid config: ");
    Serial.println(error);
    return false;
  }
  battlebang::esp::nvs::ScopedPreferences scopedPrefs;
  if (!scopedPrefs.begin(kConfigNamespace, false)) return false;
  Preferences& prefs = scopedPrefs.preferences();
  bool ok = true;
  ok &= saveCommonRuntimeConfig(prefs, toCommonRuntimeConfig(copy), commonNvsKeys(), commonNvsSavePolicy());
  ok &= prefs.putString("station_id", copy.stationId) > 0;
  ok &= prefs.putString("display", copy.displayName) > 0 || copy.displayName.length() == 0;
  ok &= prefs.putBool("dbg_sim", copy.debugAllowSimulateHit) > 0;
  ok &= prefs.putUShort("hit_thr", copy.sensor.hitThreshold) > 0;
  ok &= prefs.putUShort("rel_thr", copy.sensor.releaseThreshold) > 0;
  ok &= prefs.putUInt("hit_cd", copy.sensor.hitCooldownMs) > 0;
  ok &= prefs.putUInt("sample_ms", copy.sensor.sampleIntervalMs) > 0;
  ok &= prefs.putUInt("settle_us", copy.sensor.settleUs) > 0;
  ok &= prefs.putUShort("led_count", copy.led.numLeds) > 0;
  ok &= prefs.putUChar("led_bright", copy.led.brightness) > 0;
  ok &= prefs.putUShort("led_max_ma", copy.led.maxMa) > 0;
  ok &= prefs.putUInt("wait_color", copy.led.waitingColor) > 0;
  ok &= prefs.putUInt("cap_color", copy.led.capturedColor) > 0;
  ok &= prefs.putUInt("hit_color", copy.led.hitFlashColor) > 0;
  ok &= prefs.putUShort("breath_bpm", copy.led.waitingBreathBpm) > 0;
  ok &= prefs.putUChar("breath_min", copy.led.waitingBreathMin) > 0;
  ok &= prefs.putUChar("breath_max", copy.led.waitingBreathMax) > 0;
  ok &= prefs.putBool("lock_hit", copy.gameplay.lockAfterHit) > 0;
  ok &= prefs.putUInt("auto_rst", copy.gameplay.autoResetMs) > 0;
  ok &= prefs.putUInt("hb_ms", copy.gameplay.heartbeatIntervalMs) > 0;
  ok &= prefs.putShort("piezo_pin", copy.hardware.piezoPin) > 0;
  ok &= prefs.putShort("led_pin", copy.hardware.ledPin) > 0;
  ok &= prefs.putString("led_type", copy.hardware.ledType) > 0;
  ok &= prefs.putString("color_order", copy.hardware.colorOrder) > 0;
  return ok;
}

bool RuntimeConfigStore::clear() {
  return battlebang::esp::nvs::clearNamespace(kConfigNamespace);
}

bool connectivityConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.wifiSsid != after.wifiSsid || before.wifiPassword != after.wifiPassword ||
         before.networkAutoStart != after.networkAutoStart || before.networkStartDelayMs != after.networkStartDelayMs ||
         before.mqttHost != after.mqttHost || before.mqttPort != after.mqttPort ||
         before.mqttUsername != after.mqttUsername || before.mqttPassword != after.mqttPassword;
}

bool mqttIdentityConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.deviceId != after.deviceId || before.stationId != after.stationId || before.mqttRoot != after.mqttRoot;
}

bool sensorConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.sensor.hitThreshold != after.sensor.hitThreshold || before.sensor.releaseThreshold != after.sensor.releaseThreshold ||
         before.sensor.hitCooldownMs != after.sensor.hitCooldownMs || before.sensor.sampleIntervalMs != after.sensor.sampleIntervalMs ||
         before.sensor.settleUs != after.sensor.settleUs;
}

bool visualConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.led.numLeds != after.led.numLeds || before.led.brightness != after.led.brightness ||
         before.led.maxMa != after.led.maxMa || before.led.waitingColor != after.led.waitingColor ||
         before.led.capturedColor != after.led.capturedColor || before.led.hitFlashColor != after.led.hitFlashColor ||
         before.led.waitingBreathBpm != after.led.waitingBreathBpm ||
         before.led.waitingBreathMin != after.led.waitingBreathMin || before.led.waitingBreathMax != after.led.waitingBreathMax;
}

bool hardwareProfileChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.hardware.piezoPin != after.hardware.piezoPin || before.hardware.ledPin != after.hardware.ledPin ||
         before.hardware.ledType != after.hardware.ledType || before.hardware.colorOrder != after.hardware.colorOrder;
}

}  // namespace station
}  // namespace battlebang
