#include "runtime_config.h"

#include <ArduinoJson.h>
#include <bb_esp_core/config/common_runtime_config.h>
#include <bb_esp_core/config/runtime_config_json.h>
#include <bb_esp_nvs/common_runtime_config_store.h>

#include "heavy_blaster/app/firmware_info.h"

namespace battlebang {
namespace heavy_blaster {
namespace {

constexpr uint8_t kMaxSlots = ::heavy_blaster::kSlotCount;
constexpr uint16_t kMaxMatrixLeds = ::heavy_blaster::MAX_MATRIX_NUM_LEDS;
constexpr uint32_t kMinPreEffectMs = 0;
constexpr uint32_t kMaxPreEffectMs = 60000;
constexpr uint32_t kMaxFadeOutMs = 60000;
constexpr uint16_t kMinBlinkBpm = 1;
constexpr uint16_t kMaxBlinkBpm = 240;

const char* kConfigNamespace = "heavy_blaster";

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
  keys.otaChannel = "ota_chan";
  keys.otaDesiredBuild = "ota_build";
  keys.otaPublicManifestUrl = "ota_pub";
  keys.otaLocalMirrorUrl = "ota_mirror";
  keys.otaCheckIntervalS = "ota_int";
  keys.otaApplyOnlyInSafeState = "ota_safe";
  return keys;
}

battlebang::esp::nvs::CommonRuntimeConfigSavePolicy commonNvsSavePolicy() {
  battlebang::esp::nvs::CommonRuntimeConfigSavePolicy policy;
  policy.requireMqttRoot = true;
  policy.requireOtaChannel = true;
  policy.requireOtaPublicManifestUrl = false;
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

bool isSafeTopicSegmentChar(char c) {
  return (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '_' ||
         c == '-' ||
         c == '.';
}

void normalizeIdentity(RuntimeConfig& config) {
  config.deviceId.trim();
  config.blasterId.trim();
  config.displayName.trim();
  config.group.trim();
  config.stageId.trim();
  config.location.trim();
  if (config.blasterId.length() == 0) config.blasterId = config.deviceId;
  if (config.displayName.length() == 0) config.displayName = config.blasterId;
}

bool validateTopicSegment(const String& value, const char* field, String& error) {
  if (value.length() == 0) {
    error = String(field) + " is required";
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isSafeTopicSegmentChar(value[i])) {
      error = String(field) + " must use only A-Z, a-z, 0-9, '_', '-', or '.'";
      return false;
    }
  }
  return true;
}

bool validateMqttRoot(RuntimeConfig& config, String& error) {
  config.mqttRoot.trim();
  while (config.mqttRoot.startsWith("/")) config.mqttRoot.remove(0, 1);
  while (config.mqttRoot.endsWith("/")) config.mqttRoot.remove(config.mqttRoot.length() - 1);
  if (config.mqttRoot.length() == 0) config.mqttRoot = "battlebang";

  bool previousWasSlash = false;
  for (size_t i = 0; i < config.mqttRoot.length(); ++i) {
    const char c = config.mqttRoot[i];
    if (c == '/') {
      if (previousWasSlash) {
        error = "mqtt.root must not contain empty path segments";
        return false;
      }
      previousWasSlash = true;
      continue;
    }
    previousWasSlash = false;
    if (!isSafeTopicSegmentChar(c)) {
      error = "mqtt.root must use slash-separated topic segments with only A-Z, a-z, 0-9, '_', '-', or '.'";
      return false;
    }
  }
  return true;
}

bool applyRelayProfile(const String& profile, RuntimeConfig& config, String& error) {
  if (profile.length() == 0 || profile == "single_channel_active_high" || profile == "active_high") {
    config.hardware.relayProfile = "single_channel_active_high";
    config.hardware.relayActiveLow = false;
    return true;
  }
  if (profile == "single_channel_active_low" || profile == "active_low") {
    config.hardware.relayProfile = "single_channel_active_low";
    config.hardware.relayActiveLow = true;
    return true;
  }
  error = "hardware_profile.relay_profile must be single_channel_active_high or single_channel_active_low";
  return false;
}

bool hardwareProfileMatchesCompiled(const RuntimeConfig& config) {
  if (config.hardware.slotCount != ::heavy_blaster::kSlotCount) return false;
  if (config.led.slotCount != ::heavy_blaster::kSlotCount) return false;
  if (config.led.matrixWidth != ::heavy_blaster::MATRIX_WIDTH) return false;
  if (config.led.matrixHeight != ::heavy_blaster::MATRIX_HEIGHT) return false;
  if (config.led.matrixNumLeds != ::heavy_blaster::MATRIX_NUM_LEDS) return false;
  if (config.hardware.relayPin != ::heavy_blaster::RELAY_PIN) return false;
  if (config.hardware.ledType != ::heavy_blaster::LED_TYPE_NAME) return false;
  if (config.hardware.colorOrder != ::heavy_blaster::COLOR_ORDER_NAME) return false;
  for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) {
    if (config.hardware.matrixPins[i] != ::heavy_blaster::MATRIX_PINS[i]) return false;
  }
  return true;
}

bool validateConfig(RuntimeConfig& config, String& error) {
  normalizeIdentity(config);
  if (!validateTopicSegment(config.deviceId, "device_id", error)) return false;
  if (!validateTopicSegment(config.blasterId, "blaster_id", error)) return false;
  if (config.blasterId.length() > 64) {
    error = "blaster_id too long";
    return false;
  }
  if (config.displayName.length() > 64) {
    error = "display_name too long";
    return false;
  }
  if (config.stageId.length() > 0 && !validateTopicSegment(config.stageId, "stage_id", error)) return false;
  if (config.led.slotCount < 1 || config.led.slotCount > kMaxSlots) {
    error = String("led.slot_count must be 1..") + kMaxSlots;
    return false;
  }
  if (config.unlock.requiredSlots < 1 || config.unlock.requiredSlots > config.led.slotCount) {
    error = "unlock.required_slots must be 1..led.slot_count";
    return false;
  }
  if (config.unlock.preEffectMs > kMaxPreEffectMs || config.unlock.preEffectMs < kMinPreEffectMs) {
    error = "unlock.pre_effect_ms out of range";
    return false;
  }
  if (config.unlock.fadeOutMs > kMaxFadeOutMs || config.unlock.fadeOutMs > config.unlock.preEffectMs) {
    error = "unlock.fade_out_ms must be <= unlock.pre_effect_ms";
    return false;
  }
  if (config.unlock.blinkBpm < kMinBlinkBpm || config.unlock.blinkBpm > kMaxBlinkBpm) {
    error = "unlock.blink_bpm out of range";
    return false;
  }
  if (config.led.matrixNumLeds < 1 || config.led.matrixNumLeds > kMaxMatrixLeds) {
    error = String("led.matrix_num_leds must be 1..") + kMaxMatrixLeds;
    return false;
  }
  if (config.led.brightness < 1) {
    error = "led.brightness must be positive";
    return false;
  }
  if (config.led.maxMa < 100 || config.led.maxMa > 12000) {
    error = "led.max_ma must be 100..12000";
    return false;
  }
  if (config.mqttPort == 0) {
    error = "mqtt.port must be positive";
    return false;
  }
  if (!validateMqttRoot(config, error)) return false;
  config.otaPublicManifestUrl.trim();
  config.otaLocalMirrorUrl.trim();
  if (!battlebang::esp::config::validateOtaManifestUrl(
          config.otaPublicManifestUrl, "ota.public_manifest_url", error)) {
    return false;
  }
  if (!battlebang::esp::config::validateOtaManifestUrl(
          config.otaLocalMirrorUrl, "ota.local_mirror_url", error)) {
    return false;
  }
  if (!hardwareProfileMatchesCompiled(config)) {
    error = "hardware_profile does not match compiled heavy-blaster board profile";
    return false;
  }
  return true;
}

void copyConnectivityConfig(RuntimeConfig& dest, const RuntimeConfig& source) {
  dest.configVersion = source.configVersion;
  dest.configured = source.configured;
  dest.deviceId = source.deviceId;
  dest.blasterId = source.blasterId;
  dest.displayName = source.displayName;
  dest.group = source.group;
  dest.stageId = source.stageId;
  dest.location = source.location;
  dest.debugAllowLocalControl = source.debugAllowLocalControl;
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
  config.blasterId = identity.blasterId;
  config.displayName = "Heavy Blaster";
  config.deviceMac = identity.mac;
  config.otaPublicManifestUrl = BB_HEAVY_BLASTER_LATEST_MANIFEST_URL;
  return config;
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  DynamicJsonDocument doc(6144);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid JSON: ") + err.c_str();
    return false;
  }
  if (!doc.containsKey("config_version")) {
    error = "config_version is required";
    return false;
  }
  const uint32_t incomingVersion = getUIntOr(doc["config_version"], 0);
  if (incomingVersion == 0) {
    error = "config_version must be positive";
    return false;
  }
  if (config.configVersion != 0 && incomingVersion < config.configVersion) {
    error = "config_version must not go backwards";
    return false;
  }

  RuntimeConfig next = config;
  next.schema = static_cast<uint16_t>(getUIntOr(doc["schema"], next.schema));
  next.configVersion = incomingVersion;
  next.configured = getBoolOr(doc["configured"], next.configured);
  next.blasterId = getStringOr(doc["blaster_id"], next.blasterId);
  next.deviceId = getStringOr(doc["device_id"], next.deviceId);
  next.displayName = getStringOr(doc["display_name"], next.displayName);
  next.displayName = getStringOr(doc["name"], next.displayName);
  next.group = getStringOr(doc["group"], next.group);
  next.stageId = getStringOr(doc["stage_id"], next.stageId);
  next.location = getStringOr(doc["location"], next.location);
  next.debugAllowLocalControl = getBoolOr(doc["debug_allow_local_control"], next.debugAllowLocalControl);

  JsonObjectConst unlock = doc["unlock"].as<JsonObjectConst>();
  if (!unlock.isNull()) {
    next.unlock.requiredSlots = static_cast<uint8_t>(getUIntOr(unlock["required_slots"], next.unlock.requiredSlots));
    next.unlock.preEffectMs = getUIntOr(unlock["pre_effect_ms"], next.unlock.preEffectMs);
    next.unlock.fadeOutMs = getUIntOr(unlock["fade_out_ms"], next.unlock.fadeOutMs);
    next.unlock.blinkBpm = static_cast<uint16_t>(getUIntOr(unlock["blink_bpm"], next.unlock.blinkBpm));
    next.unlock.relayOnAfterAllSlots = getBoolOr(unlock["relay_on_after_all_slots"], next.unlock.relayOnAfterAllSlots);
  }

  JsonObjectConst led = doc["led"].as<JsonObjectConst>();
  if (!led.isNull()) {
    next.led.slotCount = static_cast<uint8_t>(getUIntOr(led["slot_count"], next.led.slotCount));
    next.led.matrixWidth = static_cast<uint8_t>(getUIntOr(led["matrix_width"], next.led.matrixWidth));
    next.led.matrixHeight = static_cast<uint8_t>(getUIntOr(led["matrix_height"], next.led.matrixHeight));
    next.led.matrixNumLeds = static_cast<uint16_t>(getUIntOr(led["matrix_num_leds"], next.led.matrixNumLeds));
    next.led.brightness = static_cast<uint8_t>(getUIntOr(led["brightness"], next.led.brightness));
    next.led.maxMa = static_cast<uint16_t>(getUIntOr(led["max_ma"], next.led.maxMa));
    next.led.activeColor = parseColor(led["active_color"], next.led.activeColor);
  }

  JsonObjectConst hw = doc["hardware_profile"].as<JsonObjectConst>();
  if (!hw.isNull()) {
    next.hardware.slotCount = static_cast<uint8_t>(getUIntOr(hw["slot_count"], next.hardware.slotCount));
    JsonArrayConst matrixPins = hw["matrix_pins"].as<JsonArrayConst>();
    if (!matrixPins.isNull()) {
      uint8_t i = 0;
      for (JsonVariantConst value : matrixPins) {
        if (i >= ::heavy_blaster::kSlotCount) break;
        const int8_t fallback = next.hardware.matrixPins[i];
        next.hardware.matrixPins[i] = static_cast<int8_t>(getIntOr(value, fallback));
        i++;
      }
    }
    next.hardware.relayPin = static_cast<int8_t>(getIntOr(hw["relay_pin"], next.hardware.relayPin));
    if (hw["relay_profile"].is<const char*>()) {
      if (!applyRelayProfile(getStringOr(hw["relay_profile"], next.hardware.relayProfile), next, error)) return false;
    }
    next.hardware.relayActiveLow = getBoolOr(hw["relay_active_low"], next.hardware.relayActiveLow);
    next.hardware.ledType = getStringOr(hw["led_type"], next.hardware.ledType);
    next.hardware.colorOrder = getStringOr(hw["color_order"], next.hardware.colorOrder);
  }

  JsonObjectConst wifi = doc["wifi"].as<JsonObjectConst>();
  if (!wifi.isNull()) {
    next.wifiSsid = getStringOr(wifi["ssid"], next.wifiSsid);
    next.wifiPassword = getStringOr(wifi["password"], next.wifiPassword);
    next.networkAutoStart = getBoolOr(wifi["auto_start"], next.networkAutoStart);
    next.networkStartDelayMs = getUIntOr(wifi["start_delay_ms"], next.networkStartDelayMs);
  }

  JsonObjectConst mqtt = doc["mqtt"].as<JsonObjectConst>();
  if (!mqtt.isNull()) {
    next.mqttHost = getStringOr(mqtt["host"], next.mqttHost);
    next.mqttPort = static_cast<uint16_t>(getUIntOr(mqtt["port"], next.mqttPort));
    next.mqttUsername = getStringOr(mqtt["username"], next.mqttUsername);
    next.mqttPassword = getStringOr(mqtt["password"], next.mqttPassword);
    next.mqttRoot = getStringOr(mqtt["root"], next.mqttRoot);
  }

  JsonObjectConst ota = doc["ota"].as<JsonObjectConst>();
  if (!ota.isNull()) {
    next.otaCommandCenterControlled = getBoolOr(ota["command_center_controlled"], next.otaCommandCenterControlled);
    next.otaAutoCheckEnabled = getBoolOr(ota["auto_check_enabled"], next.otaAutoCheckEnabled);
    next.otaChannel = getStringOr(ota["channel"], next.otaChannel);
    next.otaDesiredBuild = getUIntOr(ota["desired_build"], next.otaDesiredBuild);
    next.otaPublicManifestUrl = getStringOr(ota["public_manifest_url"], next.otaPublicManifestUrl);
    next.otaLocalMirrorUrl = getStringOr(ota["local_mirror_url"], next.otaLocalMirrorUrl);
    next.otaCheckIntervalS = getUIntOr(ota["check_interval_s"], next.otaCheckIntervalS);
    next.otaApplyOnlyInSafeState = getBoolOr(ota["apply_only_in_safe_state"], next.otaApplyOnlyInSafeState);
  }

  if (!validateConfig(next, error)) return false;
  config = next;
  return true;
}

String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets) {
  DynamicJsonDocument doc(6144);
  doc["schema"] = config.schema;
  doc["config_version"] = config.configVersion;
  doc["configured"] = config.configured;
  doc["blaster_id"] = config.blasterId;
  doc["device_id"] = config.deviceId;
  doc["device_mac"] = config.deviceMac;
  doc["display_name"] = config.displayName;
  doc["group"] = config.group;
  doc["stage_id"] = config.stageId;
  doc["location"] = config.location;
  doc["debug_allow_local_control"] = config.debugAllowLocalControl;

  JsonObject unlock = doc.createNestedObject("unlock");
  unlock["required_slots"] = config.unlock.requiredSlots;
  unlock["pre_effect_ms"] = config.unlock.preEffectMs;
  unlock["fade_out_ms"] = config.unlock.fadeOutMs;
  unlock["blink_bpm"] = config.unlock.blinkBpm;
  unlock["relay_on_after_all_slots"] = config.unlock.relayOnAfterAllSlots;

  JsonObject led = doc.createNestedObject("led");
  led["slot_count"] = config.led.slotCount;
  led["matrix_width"] = config.led.matrixWidth;
  led["matrix_height"] = config.led.matrixHeight;
  led["matrix_num_leds"] = config.led.matrixNumLeds;
  led["brightness"] = config.led.brightness;
  led["max_ma"] = config.led.maxMa;
  led["active_color"] = colorString(config.led.activeColor);

  JsonObject hw = doc.createNestedObject("hardware_profile");
  hw["slot_count"] = config.hardware.slotCount;
  JsonArray pins = hw.createNestedArray("matrix_pins");
  for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) pins.add(config.hardware.matrixPins[i]);
  hw["relay_pin"] = config.hardware.relayPin;
  hw["relay_active_low"] = config.hardware.relayActiveLow;
  hw["relay_profile"] = config.hardware.relayProfile;
  hw["led_type"] = config.hardware.ledType;
  hw["color_order"] = config.hardware.colorOrder;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSsid;
  wifi["password"] = includeSecrets ? config.wifiPassword : String("********");
  wifi["auto_start"] = config.networkAutoStart;
  wifi["start_delay_ms"] = config.networkStartDelayMs;

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = config.mqttHost;
  mqtt["port"] = config.mqttPort;
  mqtt["username"] = config.mqttUsername;
  mqtt["password"] = includeSecrets ? config.mqttPassword : String("********");
  mqtt["root"] = config.mqttRoot;

  JsonObject ota = doc.createNestedObject("ota");
  ota["command_center_controlled"] = config.otaCommandCenterControlled;
  ota["auto_check_enabled"] = config.otaAutoCheckEnabled;
  ota["channel"] = config.otaChannel;
  ota["desired_build"] = config.otaDesiredBuild;
  ota["public_manifest_url"] = config.otaPublicManifestUrl;
  ota["local_mirror_url"] = config.otaLocalMirrorUrl;
  ota["check_interval_s"] = config.otaCheckIntervalS;
  ota["apply_only_in_safe_state"] = config.otaApplyOnlyInSafeState;

  String out;
  serializeJson(doc, out);
  return out;
}

bool connectivityConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.wifiSsid != after.wifiSsid || before.wifiPassword != after.wifiPassword ||
         before.networkAutoStart != after.networkAutoStart || before.networkStartDelayMs != after.networkStartDelayMs;
}

bool mqttIdentityConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.mqttHost != after.mqttHost || before.mqttPort != after.mqttPort ||
         before.mqttUsername != after.mqttUsername || before.mqttPassword != after.mqttPassword ||
         before.mqttRoot != after.mqttRoot || before.blasterId != after.blasterId || before.deviceId != after.deviceId;
}

bool visualConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.led.brightness != after.led.brightness || before.led.maxMa != after.led.maxMa ||
         before.led.activeColor != after.led.activeColor || before.unlock.requiredSlots != after.unlock.requiredSlots ||
         before.unlock.preEffectMs != after.unlock.preEffectMs || before.unlock.fadeOutMs != after.unlock.fadeOutMs ||
         before.unlock.blinkBpm != after.unlock.blinkBpm ||
         before.unlock.relayOnAfterAllSlots != after.unlock.relayOnAfterAllSlots;
}

bool hardwareProfileChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  if (before.hardware.relayActiveLow != after.hardware.relayActiveLow ||
      before.hardware.relayProfile != after.hardware.relayProfile) {
    return true;
  }
  for (uint8_t i = 0; i < ::heavy_blaster::kSlotCount; ++i) {
    if (before.hardware.matrixPins[i] != after.hardware.matrixPins[i]) return true;
  }
  return before.hardware.relayPin != after.hardware.relayPin || before.hardware.ledType != after.hardware.ledType ||
         before.hardware.colorOrder != after.hardware.colorOrder;
}

bool RuntimeConfigStore::load(RuntimeConfig& config) {
  battlebang::esp::nvs::ScopedPreferences scopedPrefs;
  if (!scopedPrefs.begin(kConfigNamespace, true)) return false;
  Preferences& prefs = scopedPrefs.preferences();
  if (!prefs.getBool("has_cfg", false)) {
    scopedPrefs.end();
    return false;
  }

  RuntimeConfig loaded = config;
  battlebang::esp::config::CommonRuntimeConfig common = toCommonRuntimeConfig(loaded);
  battlebang::esp::nvs::loadCommonRuntimeConfig(prefs, common, commonNvsKeys());
  applyCommonRuntimeConfig(loaded, common);

  loaded.blasterId = prefs.getString("blast_id", loaded.blasterId);
  loaded.displayName = prefs.getString("display", loaded.displayName);
  loaded.debugAllowLocalControl = prefs.getBool("dbg_local", loaded.debugAllowLocalControl);
  loaded.unlock.requiredSlots = prefs.getUChar("req_slots", loaded.unlock.requiredSlots);
  loaded.unlock.preEffectMs = prefs.getUInt("pre_ms", loaded.unlock.preEffectMs);
  loaded.unlock.fadeOutMs = prefs.getUInt("fade_ms", loaded.unlock.fadeOutMs);
  loaded.unlock.blinkBpm = prefs.getUShort("blink_bpm", loaded.unlock.blinkBpm);
  loaded.unlock.relayOnAfterAllSlots = prefs.getBool("rel_unlock", loaded.unlock.relayOnAfterAllSlots);
  loaded.led.brightness = prefs.getUChar("brightness", loaded.led.brightness);
  loaded.led.maxMa = prefs.getUShort("max_ma", loaded.led.maxMa);
  loaded.led.activeColor = prefs.getUInt("active_c", loaded.led.activeColor);
  loaded.hardware.relayActiveLow = prefs.getBool("rel_low", loaded.hardware.relayActiveLow);
  loaded.hardware.relayProfile = prefs.getString("rel_prof", loaded.hardware.relayProfile);
  scopedPrefs.end();

  String error;
  if (!validateConfig(loaded, error)) {
    Serial.print("[heavy-blaster][config] stored config invalid: ");
    Serial.println(error);
    RuntimeConfig salvaged = config;
    copyConnectivityConfig(salvaged, loaded);
    String salvageError;
    if (validateConfig(salvaged, salvageError)) {
      Serial.println("[heavy-blaster][config] preserving connectivity with compiled defaults");
      config = salvaged;
      return true;
    }
    return false;
  }
  config = loaded;
  return true;
}

bool RuntimeConfigStore::save(const RuntimeConfig& config) {
  battlebang::esp::nvs::ScopedPreferences scopedPrefs;
  if (!scopedPrefs.begin(kConfigNamespace, false)) return false;
  Preferences& prefs = scopedPrefs.preferences();

  bool ok = true;
  ok &= prefs.putBool("has_cfg", true) > 0;
  ok &= battlebang::esp::nvs::saveCommonRuntimeConfig(
      prefs, toCommonRuntimeConfig(config), commonNvsKeys(), commonNvsSavePolicy());
  ok &= prefs.putString("blast_id", config.blasterId) > 0;
  ok &= prefs.putString("display", config.displayName) >= 0;
  ok &= prefs.putBool("dbg_local", config.debugAllowLocalControl) > 0;
  ok &= prefs.putUChar("req_slots", config.unlock.requiredSlots) > 0;
  ok &= prefs.putUInt("pre_ms", config.unlock.preEffectMs) > 0;
  ok &= prefs.putUInt("fade_ms", config.unlock.fadeOutMs) > 0;
  ok &= prefs.putUShort("blink_bpm", config.unlock.blinkBpm) > 0;
  ok &= prefs.putBool("rel_unlock", config.unlock.relayOnAfterAllSlots) > 0;
  ok &= prefs.putUChar("brightness", config.led.brightness) > 0;
  ok &= prefs.putUShort("max_ma", config.led.maxMa) > 0;
  ok &= prefs.putUInt("active_c", config.led.activeColor) > 0;
  ok &= prefs.putBool("rel_low", config.hardware.relayActiveLow) > 0;
  ok &= prefs.putString("rel_prof", config.hardware.relayProfile) > 0;
  return ok;
}

bool RuntimeConfigStore::clear() {
  return battlebang::esp::nvs::clearNamespace(kConfigNamespace);
}


}  // namespace heavy_blaster
}  // namespace battlebang
