#include "runtime_config.h"

#include <ArduinoJson.h>
#include <bb_esp_core/config/common_runtime_config.h>
#include <bb_esp_core/config/runtime_config_json.h>
#include <bb_esp_nvs/common_runtime_config_store.h>

#include "boss_target/app/firmware_info.h"

namespace battlebang {
namespace boss_target {
namespace {

constexpr uint16_t kMaxHp = 999;
constexpr uint16_t kMaxTargetDurationMs = 60000;
constexpr uint16_t kMinTargetDurationMs = 250;
constexpr uint16_t kMaxHitCooldownMs = 5000;
constexpr uint16_t kMinHitCooldownMs = 20;
constexpr uint32_t kMinIsrDebounceUs = 500;
constexpr uint32_t kMaxIsrDebounceUs = 50000;

const char* kConfigNamespace = "boss_target";

battlebang::esp::nvs::CommonRuntimeConfigKeys commonNvsKeys() {
  battlebang::esp::nvs::CommonRuntimeConfigKeys keys;
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
  config.bossId.trim();
  config.targetId.trim();
  config.displayName.trim();
  config.deviceId.trim();
  config.group.trim();
  config.stageId.trim();
  config.location.trim();
  if (config.bossId.length() == 0) config.bossId = config.targetId;
  if (config.bossId.length() == 0) config.bossId = config.deviceId;
  if (config.targetId.length() == 0) config.targetId = config.bossId;
  if (config.displayName.length() == 0) config.displayName = config.bossId;
}

bool isSafeTopicSegmentChar(char c) {
  return (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '_' ||
         c == '-' ||
         c == '.';
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

bool hardwareProfileMatchesCompiled(const RuntimeConfig& config) {
  if (config.hardware.maxTargets != ::boss_target::kMaxTargets) return false;
  if (config.hardware.hpBarPin != ::boss_target::HP_BAR_PIN) return false;
  if (config.hardware.ledType != ::boss_target::LED_TYPE_NAME) return false;
  if (config.hardware.colorOrder != ::boss_target::COLOR_ORDER_NAME) return false;
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    if (config.hardware.ringPins[i] != ::boss_target::RING_PINS[i]) return false;
    if (config.hardware.piezoDoPins[i] != ::boss_target::PIEZO_DO_PINS[i]) return false;
  }
  return true;
}

bool validateConfig(RuntimeConfig& config, String& error) {
  normalizeIdentity(config);
  if (!validateTopicSegment(config.deviceId, "device_id", error)) return false;
  if (!validateTopicSegment(config.bossId, "boss_id", error)) return false;
  if (!validateTopicSegment(config.targetId, "target_id", error)) return false;
  if (config.bossId.length() > 48) {
    error = "boss_id too long";
    return false;
  }
  if (config.targetId.length() > 48) {
    error = "target_id too long";
    return false;
  }
  if (config.displayName.length() > 64) {
    error = "display_name too long";
    return false;
  }
  if (config.stageId.length() > 0 && !validateTopicSegment(config.stageId, "stage_id", error)) return false;
  if (config.target.count < 1 || config.target.count > ::boss_target::kMaxTargets) {
    error = String("target.count must be 1..") + ::boss_target::kMaxTargets;
    return false;
  }
  if (config.target.ringNumLeds < 1 || config.target.ringNumLeds > ::boss_target::MAX_RING_NUM_LEDS) {
    error = String("target.ring_num_leds must be 1..") + ::boss_target::MAX_RING_NUM_LEDS;
    return false;
  }
  if (config.hpBar.numLeds < ::boss_target::HP_BAR_LEDS_PER_GROUP ||
      config.hpBar.numLeds > ::boss_target::MAX_HP_BAR_NUM_LEDS) {
    error = String("hp_bar.num_leds must be ") + ::boss_target::HP_BAR_LEDS_PER_GROUP + ".." +
            ::boss_target::MAX_HP_BAR_NUM_LEDS;
    return false;
  }
  if (config.hpBar.numLeds % ::boss_target::HP_BAR_LEDS_PER_GROUP != 0) {
    error = String("hp_bar.num_leds must be a multiple of ") + ::boss_target::HP_BAR_LEDS_PER_GROUP +
            " for the 3-row grouped HP bar";
    return false;
  }
  if (config.hpBar.brightness < 1) {
    error = "hp_bar.brightness must be positive";
    return false;
  }
  if (config.hpBar.maxMa < 100 || config.hpBar.maxMa > 12000) {
    error = "hp_bar.max_ma must be 100..12000";
    return false;
  }
  if (config.gameplay.hpMax < 1 || config.gameplay.hpMax > kMaxHp) {
    error = String("gameplay.hp_max must be 1..") + kMaxHp;
    return false;
  }
  if (config.gameplay.damagePerHit < 1 || config.gameplay.damagePerHit > config.gameplay.hpMax) {
    error = "gameplay.damage_per_hit must be 1..hp_max";
    return false;
  }
  if (config.gameplay.phaseCount < 1 || config.gameplay.phaseCount > kMaxHpPhases) {
    error = String("gameplay.phase_count must be 1..") + kMaxHpPhases;
    return false;
  }
  if (config.gameplay.targetDurationMs < kMinTargetDurationMs ||
      config.gameplay.targetDurationMs > kMaxTargetDurationMs) {
    error = "gameplay.target_duration_ms out of range";
    return false;
  }
  if (config.gameplay.hitCooldownMs < kMinHitCooldownMs || config.gameplay.hitCooldownMs > kMaxHitCooldownMs) {
    error = "gameplay.hit_cooldown_ms out of range";
    return false;
  }
  if (config.gameplay.digitalIsrDebounceUs < kMinIsrDebounceUs ||
      config.gameplay.digitalIsrDebounceUs > kMaxIsrDebounceUs) {
    error = "gameplay.digital_isr_debounce_us out of range";
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
  if (config.otaCheckIntervalS < 30) config.otaCheckIntervalS = 30;
  if (!hardwareProfileMatchesCompiled(config)) {
    error = "hardware_profile does not match compiled boss-target board profile";
    return false;
  }
  return true;
}

void copyConnectivityConfig(RuntimeConfig& dest, const RuntimeConfig& source) {
  dest.configVersion = source.configVersion;
  dest.configured = source.configured;
  dest.deviceId = source.deviceId;
  dest.bossId = source.bossId;
  dest.targetId = source.targetId;
  dest.displayName = source.displayName;
  dest.group = source.group;
  dest.location = source.location;
  dest.debugAllowSimulateHit = source.debugAllowSimulateHit;
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

void writeHardwareProfile(JsonObject obj, const RuntimeConfig& config) {
  obj["max_targets"] = config.hardware.maxTargets;
  JsonArray rings = obj.createNestedArray("ring_pins");
  JsonArray piezos = obj.createNestedArray("piezo_do_pins");
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    rings.add(config.hardware.ringPins[i]);
    piezos.add(config.hardware.piezoDoPins[i]);
  }
  obj["hp_bar_pin"] = config.hardware.hpBarPin;
  obj["led_type"] = config.hardware.ledType;
  obj["color_order"] = config.hardware.colorOrder;
}

}  // namespace

RuntimeConfig makeDefaultRuntimeConfig(const DeviceIdentity& identity) {
  RuntimeConfig config;
  config.deviceId = identity.deviceId;
  config.bossId = identity.targetId;
  config.targetId = identity.targetId;
  config.displayName = identity.targetId;
  config.deviceMac = identity.mac;
  config.otaPublicManifestUrl = BB_BOSS_TARGET_LATEST_MANIFEST_URL;
  return config;
}

uint8_t activePhaseCount(const RuntimeConfig& config) {
  return constrain(config.gameplay.phaseCount, static_cast<uint8_t>(1), kMaxHpPhases);
}

uint32_t phaseColorRgb(const RuntimeConfig& config, uint8_t phaseIndex) {
  uint8_t count = activePhaseCount(config);
  if (phaseIndex >= count) phaseIndex = count - 1;
  return config.hpBar.palette[phaseIndex] & 0xFFFFFFUL;
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid json: ") + err.c_str();
    return false;
  }

  RuntimeConfig next = config;
  if (!doc.containsKey("config_version")) {
    error = "config_version is required";
    return false;
  }
  const uint32_t incomingVersion = getUIntOr(doc["config_version"], 0);
  if (incomingVersion == 0) {
    error = "config_version must be positive";
    return false;
  }
  if (incomingVersion < config.configVersion) {
    error = "stale config_version";
    return false;
  }
  next.configVersion = incomingVersion;
  next.schema = static_cast<uint16_t>(getUIntOr(doc["schema"], next.schema));
  next.configured = getBoolOr(doc["configured"], next.configured);
  const String type = getStringOr(doc["type"], "");
  if (type == "provision") next.configured = true;

  next.deviceId = getStringOr(doc["device_id"], next.deviceId);
  next.bossId = getStringOr(doc["boss_id"], next.bossId);
  next.targetId = getStringOr(doc["target_id"], next.targetId);
  next.displayName = getStringOr(doc["display_name"], next.displayName);
  next.displayName = getStringOr(doc["name"], next.displayName);
  next.group = getStringOr(doc["group"], next.group);
  next.stageId = getStringOr(doc["stage_id"], next.stageId);
  next.location = getStringOr(doc["location"], next.location);
  next.debugAllowSimulateHit = getBoolOr(doc["debug_allow_simulate_hit"], next.debugAllowSimulateHit);

  JsonObjectConst gameplay = doc["gameplay"].as<JsonObjectConst>();
  if (!gameplay.isNull()) {
    next.gameplay.hpMax = static_cast<uint16_t>(getUIntOr(gameplay["hp_max"], next.gameplay.hpMax));
    next.gameplay.damagePerHit = static_cast<uint16_t>(getUIntOr(gameplay["damage_per_hit"], next.gameplay.damagePerHit));
    next.gameplay.phaseCount = static_cast<uint8_t>(getUIntOr(gameplay["phase_count"], next.gameplay.phaseCount));
    next.gameplay.startResetsHp = getBoolOr(gameplay["start_resets_hp"], next.gameplay.startResetsHp);
    next.gameplay.targetDurationMs = getUIntOr(gameplay["target_duration_ms"], next.gameplay.targetDurationMs);
    next.gameplay.hitCooldownMs = getUIntOr(gameplay["hit_cooldown_ms"], next.gameplay.hitCooldownMs);
    next.gameplay.digitalIsrDebounceUs = getUIntOr(gameplay["digital_isr_debounce_us"], next.gameplay.digitalIsrDebounceUs);
  }

  JsonObjectConst target = doc["target"].as<JsonObjectConst>();
  if (!target.isNull()) {
    next.target.count = static_cast<uint8_t>(getUIntOr(target["count"], next.target.count));
    next.target.ringNumLeds = static_cast<uint16_t>(getUIntOr(target["ring_num_leds"], next.target.ringNumLeds));
    next.target.activeColor = parseColor(target["active_color"], next.target.activeColor);
    next.target.hitFlashColor = parseColor(target["hit_flash_color"], next.target.hitFlashColor);
  }

  JsonObjectConst hpBar = doc["hp_bar"].as<JsonObjectConst>();
  if (!hpBar.isNull()) {
    next.hpBar.numLeds = static_cast<uint16_t>(getUIntOr(hpBar["num_leds"], next.hpBar.numLeds));
    next.hpBar.brightness = static_cast<uint8_t>(getUIntOr(hpBar["brightness"], next.hpBar.brightness));
    next.hpBar.maxMa = static_cast<uint16_t>(getUIntOr(hpBar["max_ma"], next.hpBar.maxMa));
    next.hpBar.deadBlinkMs = getUIntOr(hpBar["dead_blink_ms"], next.hpBar.deadBlinkMs);
    JsonArrayConst palette = hpBar["palette"].as<JsonArrayConst>();
    if (!palette.isNull()) {
      uint8_t i = 0;
      for (JsonVariantConst value : palette) {
        if (i >= kMaxHpPhases) break;
        next.hpBar.palette[i] = parseColor(value, next.hpBar.palette[i]);
        ++i;
      }
    }
  }

  JsonObjectConst hw = doc["hardware_profile"].as<JsonObjectConst>();
  if (!hw.isNull()) {
    next.hardware.maxTargets = static_cast<uint8_t>(getUIntOr(hw["max_targets"], next.hardware.maxTargets));
    next.hardware.hpBarPin = static_cast<int8_t>(getIntOr(hw["hp_bar_pin"], next.hardware.hpBarPin));
    next.hardware.ledType = getStringOr(hw["led_type"], next.hardware.ledType);
    next.hardware.colorOrder = getStringOr(hw["color_order"], next.hardware.colorOrder);
    JsonArrayConst rings = hw["ring_pins"].as<JsonArrayConst>();
    if (!rings.isNull()) {
      uint8_t i = 0;
      for (JsonVariantConst value : rings) {
        if (i >= ::boss_target::kMaxTargets) break;
        next.hardware.ringPins[i] = static_cast<int8_t>(getIntOr(value, next.hardware.ringPins[i]));
        ++i;
      }
    }
    JsonArrayConst piezos = hw["piezo_ao_pins"].as<JsonArrayConst>();
    if (piezos.isNull()) piezos = hw["piezo_do_pins"].as<JsonArrayConst>();
    if (!piezos.isNull()) {
      uint8_t i = 0;
      for (JsonVariantConst value : piezos) {
        if (i >= ::boss_target::kMaxTargets) break;
        next.hardware.piezoDoPins[i] = static_cast<int8_t>(getIntOr(value, next.hardware.piezoDoPins[i]));
        ++i;
      }
    }
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
  DynamicJsonDocument doc(4096);
  doc["schema"] = config.schema;
  doc["config_version"] = config.configVersion;
  doc["configured"] = config.configured;
  doc["device_id"] = config.deviceId;
  doc["boss_id"] = config.bossId;
  doc["target_id"] = config.targetId;
  doc["display_name"] = config.displayName;
  doc["device_mac"] = config.deviceMac;
  doc["group"] = config.group;
  doc["stage_id"] = config.stageId;
  doc["location"] = config.location;
  doc["debug_allow_simulate_hit"] = config.debugAllowSimulateHit;

  JsonObject gameplay = doc.createNestedObject("gameplay");
  gameplay["hp_max"] = config.gameplay.hpMax;
  gameplay["damage_per_hit"] = config.gameplay.damagePerHit;
  gameplay["phase_count"] = config.gameplay.phaseCount;
  gameplay["start_resets_hp"] = config.gameplay.startResetsHp;
  gameplay["target_duration_ms"] = config.gameplay.targetDurationMs;
  gameplay["hit_cooldown_ms"] = config.gameplay.hitCooldownMs;
  gameplay["digital_isr_debounce_us"] = config.gameplay.digitalIsrDebounceUs;

  JsonObject target = doc.createNestedObject("target");
  target["count"] = config.target.count;
  target["ring_num_leds"] = config.target.ringNumLeds;
  target["active_color"] = colorString(config.target.activeColor);
  target["hit_flash_color"] = colorString(config.target.hitFlashColor);

  JsonObject hpBar = doc.createNestedObject("hp_bar");
  hpBar["num_leds"] = config.hpBar.numLeds;
  hpBar["brightness"] = config.hpBar.brightness;
  hpBar["max_ma"] = config.hpBar.maxMa;
  hpBar["dead_blink_ms"] = config.hpBar.deadBlinkMs;
  JsonArray palette = hpBar.createNestedArray("palette");
  for (uint8_t i = 0; i < activePhaseCount(config); ++i) palette.add(colorString(config.hpBar.palette[i]));

  JsonObject hw = doc.createNestedObject("hardware_profile");
  writeHardwareProfile(hw, config);

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

bool gameplayConfigChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.gameplay.hpMax != after.gameplay.hpMax ||
         before.gameplay.damagePerHit != after.gameplay.damagePerHit ||
         before.gameplay.phaseCount != after.gameplay.phaseCount ||
         before.gameplay.targetDurationMs != after.gameplay.targetDurationMs ||
         before.gameplay.hitCooldownMs != after.gameplay.hitCooldownMs ||
         before.gameplay.digitalIsrDebounceUs != after.gameplay.digitalIsrDebounceUs ||
         before.target.count != after.target.count ||
         before.target.ringNumLeds != after.target.ringNumLeds ||
         before.hpBar.numLeds != after.hpBar.numLeds;
}

bool sensorPinsChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    if (before.hardware.piezoDoPins[i] != after.hardware.piezoDoPins[i]) return true;
  }
  return false;
}

bool ledHardwareChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  if (before.hardware.hpBarPin != after.hardware.hpBarPin || before.hardware.ledType != after.hardware.ledType ||
      before.hardware.colorOrder != after.hardware.colorOrder) {
    return true;
  }
  for (uint8_t i = 0; i < ::boss_target::kMaxTargets; ++i) {
    if (before.hardware.ringPins[i] != after.hardware.ringPins[i]) return true;
  }
  return false;
}

bool RuntimeConfigStore::load(RuntimeConfig& config) {
  battlebang::esp::nvs::ScopedPreferences scopedPrefs;
  if (!scopedPrefs.begin(kConfigNamespace, true)) return false;
  Preferences& prefs = scopedPrefs.preferences();

  RuntimeConfig loaded = config;
  battlebang::esp::config::CommonRuntimeConfig common = toCommonRuntimeConfig(loaded);
  battlebang::esp::nvs::loadCommonRuntimeConfig(prefs, common, commonNvsKeys());
  applyCommonRuntimeConfig(loaded, common);

  loaded.bossId = prefs.getString("boss_id", loaded.bossId);
  loaded.targetId = prefs.getString("target_id", loaded.targetId);
  loaded.displayName = prefs.getString("display", loaded.displayName);
  loaded.gameplay.hpMax = prefs.getUShort("hp_max", loaded.gameplay.hpMax);
  loaded.gameplay.damagePerHit = prefs.getUShort("damage", loaded.gameplay.damagePerHit);
  loaded.gameplay.phaseCount = prefs.getUChar("phases", loaded.gameplay.phaseCount);
  loaded.gameplay.startResetsHp = prefs.getBool("start_reset", loaded.gameplay.startResetsHp);
  loaded.gameplay.targetDurationMs = prefs.getUInt("target_ms", loaded.gameplay.targetDurationMs);
  loaded.gameplay.hitCooldownMs = prefs.getUInt("cooldown", loaded.gameplay.hitCooldownMs);
  loaded.gameplay.digitalIsrDebounceUs = prefs.getUInt("isr_us", loaded.gameplay.digitalIsrDebounceUs);
  loaded.target.count = prefs.getUChar("tgt_count", loaded.target.count);
  loaded.target.ringNumLeds = prefs.getUShort("ring_leds", loaded.target.ringNumLeds);
  loaded.target.activeColor = prefs.getUInt("active_rgb", loaded.target.activeColor);
  loaded.target.hitFlashColor = prefs.getUInt("flash_rgb", loaded.target.hitFlashColor);
  loaded.hpBar.numLeds = prefs.getUShort("hp_leds", loaded.hpBar.numLeds);
  loaded.hpBar.brightness = prefs.getUChar("brightness", loaded.hpBar.brightness);
  loaded.hpBar.maxMa = prefs.getUShort("max_ma", loaded.hpBar.maxMa);
  loaded.hpBar.deadBlinkMs = prefs.getUInt("dead_blink", loaded.hpBar.deadBlinkMs);
  for (uint8_t i = 0; i < kMaxHpPhases; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "pal_%u", i);
    loaded.hpBar.palette[i] = prefs.getUInt(key, loaded.hpBar.palette[i]);
  }
  loaded.debugAllowSimulateHit = prefs.getBool("debug_sim", loaded.debugAllowSimulateHit);
  scopedPrefs.end();

  String error;
  if (!validateConfig(loaded, error)) {
    Serial.print("[boss_target][config] stored config invalid: ");
    Serial.println(error);

    RuntimeConfig salvaged = config;
    copyConnectivityConfig(salvaged, loaded);
    String salvageError;
    if (validateConfig(salvaged, salvageError)) {
      Serial.println("[boss_target][config] preserving connectivity with compiled defaults");
      config = salvaged;
      return true;
    }

    Serial.print("[boss_target][config] connectivity salvage failed: ");
    Serial.println(salvageError);
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
  ok &= battlebang::esp::nvs::saveCommonRuntimeConfig(
      prefs, toCommonRuntimeConfig(config), commonNvsKeys(), commonNvsSavePolicy());
  ok &= prefs.putString("boss_id", config.bossId) > 0;
  ok &= prefs.putString("target_id", config.targetId) > 0;
  ok &= prefs.putString("display", config.displayName) > 0;
  ok &= prefs.putUShort("hp_max", config.gameplay.hpMax) > 0;
  ok &= prefs.putUShort("damage", config.gameplay.damagePerHit) > 0;
  ok &= prefs.putUChar("phases", config.gameplay.phaseCount) > 0;
  ok &= prefs.putBool("start_reset", config.gameplay.startResetsHp) > 0;
  ok &= prefs.putUInt("target_ms", config.gameplay.targetDurationMs) > 0;
  ok &= prefs.putUInt("cooldown", config.gameplay.hitCooldownMs) > 0;
  ok &= prefs.putUInt("isr_us", config.gameplay.digitalIsrDebounceUs) > 0;
  ok &= prefs.putUChar("tgt_count", config.target.count) > 0;
  ok &= prefs.putUShort("ring_leds", config.target.ringNumLeds) > 0;
  ok &= prefs.putUInt("active_rgb", config.target.activeColor) > 0;
  ok &= prefs.putUInt("flash_rgb", config.target.hitFlashColor) > 0;
  ok &= prefs.putUShort("hp_leds", config.hpBar.numLeds) > 0;
  ok &= prefs.putUChar("brightness", config.hpBar.brightness) > 0;
  ok &= prefs.putUShort("max_ma", config.hpBar.maxMa) > 0;
  ok &= prefs.putUInt("dead_blink", config.hpBar.deadBlinkMs) > 0;
  for (uint8_t i = 0; i < kMaxHpPhases; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "pal_%u", i);
    ok &= prefs.putUInt(key, config.hpBar.palette[i]) > 0;
  }
  ok &= prefs.putBool("debug_sim", config.debugAllowSimulateHit) > 0;
  return ok;
}

bool RuntimeConfigStore::clear() {
  return battlebang::esp::nvs::clearNamespace(kConfigNamespace);
}


}  // namespace boss_target
}  // namespace battlebang
