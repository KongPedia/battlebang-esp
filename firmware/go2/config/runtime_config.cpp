#include "runtime_config.h"

#include <ArduinoJson.h>
#include <Esp.h>

#include <stdio.h>
#include <bb_esp_core/config/build_time_config.h>
#include <bb_esp_core/config/runtime_config_json.h>
#include <bb_esp_core/mqtt/topic_utils.h>
#include <bb_esp_nvs/common_runtime_config_store.h>

#include "go2/build_config.h"
#include "go2/app/firmware_info.h"

namespace go2 {
namespace {

const char* kConfigNamespace = "go2";
const char* kHitTopicPrefixKey = "hit_topic";

battlebang::esp::nvs::CommonRuntimeConfigKeys commonNvsKeys() {
  return battlebang::esp::nvs::standardCommonRuntimeConfigKeys();
}

String efuseMacHex() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[13];
  snprintf(buf, sizeof(buf), "%012llx", static_cast<unsigned long long>(mac));
  return String(buf);
}

String defaultRuntimeRobotId() {
  return String("go2-") + efuseMacHex();
}

void readUInt16ConfigField(JsonObjectConst object, const char* key, uint16_t& value) {
  JsonVariantConst field = object[key];
  if (field.isNull()) return;
  uint32_t next = battlebang::esp::config::getUInt32Or(field, value);
  if (next > 65535UL) next = 65535UL;
  value = static_cast<uint16_t>(next);
}

void readHitTuningJson(JsonObjectConst object, HitRuntimeConfig& hit) {
  if (object.isNull()) return;
  battlebang::esp::config::readUInt32Field(object, "hit_cooldown_ms", hit.hitCooldownMs);
  readUInt16ConfigField(object, "offline_queue_capacity", hit.offlineQueueCapacity);
  battlebang::esp::config::readUInt32Field(
      object, "offline_queue_flush_interval_ms", hit.offlineQueueFlushIntervalMs);
  readUInt16ConfigField(object, "led_brightness", hit.ledBrightness);
  readUInt16ConfigField(object, "piezo_ao_threshold_raw", hit.piezoAoThresholdRaw);
  readUInt16ConfigField(object, "piezo_ao_rearm_raw", hit.piezoAoRearmRaw);
  battlebang::esp::config::readUInt32Field(
      object, "piezo_ao_capture_window_ms", hit.piezoAoCaptureWindowMs);
  battlebang::esp::config::readUInt32Field(
      object, "piezo_ao_debug_period_ms", hit.piezoAoDebugPeriodMs);
  battlebang::esp::config::readUInt32Field(
      object, "piezo_ao_rearm_stable_ms", hit.piezoAoRearmStableMs);
}

void normalizeHitRuntimeConfig(HitRuntimeConfig& hit) {
  hit.robotId.trim();
  hit.hitTopicPrefix = battlebang::esp::mqtt::trimSlashes(hit.hitTopicPrefix);
  if (hit.hitTopicPrefix.length() == 0) {
    hit.hitTopicPrefix = battlebang::esp::mqtt::trimSlashes(String(MQTT_TOPIC_PREFIX));
  }
  if (hit.offlineQueueCapacity == 0) hit.offlineQueueCapacity = OFFLINE_HIT_QUEUE_CAPACITY;
  if (hit.offlineQueueFlushIntervalMs == 0) {
    hit.offlineQueueFlushIntervalMs = OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;
  }
  // A threshold of 0 is an explicit bench-test mode: any ADC sample can trigger
  // a hit candidate.  Missing payload fields keep the previous/default value
  // because applyRuntimeConfigJson starts from the current config.
  if (hit.piezoAoCaptureWindowMs == 0) hit.piezoAoCaptureWindowMs = PIEZO_AO_CAPTURE_WINDOW_MS;
  if (hit.piezoAoDebugPeriodMs == 0) hit.piezoAoDebugPeriodMs = PIEZO_AO_DEBUG_PERIOD_MS;
  if (hit.piezoAoRearmStableMs == 0) hit.piezoAoRearmStableMs = HIT_REARM_STABLE_MS;
}

bool validateHitRuntimeConfig(const HitRuntimeConfig& hit, String& error) {
  if (!battlebang::esp::mqtt::isSafeTopicSegment(hit.robotId)) {
    error = "robot_id must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  if (!battlebang::esp::mqtt::isSafeTopicPath(hit.hitTopicPrefix)) {
    error = "hit_topic_prefix must be a safe slash-separated MQTT topic path";
    return false;
  }
  if (hit.offlineQueueCapacity == 0 || hit.offlineQueueCapacity > OFFLINE_HIT_QUEUE_CAPACITY) {
    error = String("offline_queue_capacity must be 1..") + String(OFFLINE_HIT_QUEUE_CAPACITY);
    return false;
  }
  if (hit.ledBrightness > 255) {
    error = "led_brightness must be 0..255";
    return false;
  }
  if (hit.piezoAoThresholdRaw == 0 || hit.piezoAoThresholdRaw > 4095) {
    error = "piezo_ao_threshold_raw must be 1..4095";
    return false;
  }
  if (hit.piezoAoRearmRaw >= hit.piezoAoThresholdRaw) {
    error = "piezo_ao_rearm_raw must be below piezo_ao_threshold_raw";
    return false;
  }
  if (hit.piezoAoCaptureWindowMs == 0) {
    error = "piezo_ao_capture_window_ms must be positive";
    return false;
  }
  if (hit.piezoAoDebugPeriodMs == 0) {
    error = "piezo_ao_debug_period_ms must be positive";
    return false;
  }
  if (hit.piezoAoRearmStableMs == 0) {
    error = "piezo_ao_rearm_stable_ms must be positive";
    return false;
  }
  return true;
}

void loadHitRuntimeConfigFromNvs(battlebang::esp::nvs::ScopedPreferences& prefs,
                                 HitRuntimeConfig& hit) {
  hit.robotId = prefs.preferences().getString("robot_id", hit.robotId);
  hit.hitTopicPrefix = prefs.preferences().getString(kHitTopicPrefixKey, hit.hitTopicPrefix);
  hit.hitCooldownMs = prefs.preferences().getUInt("hit_cd_ms", hit.hitCooldownMs);
  hit.offlineQueueCapacity = prefs.preferences().getUInt("offq_cap", hit.offlineQueueCapacity);
  hit.offlineQueueFlushIntervalMs = prefs.preferences().getUInt("offq_flush", hit.offlineQueueFlushIntervalMs);
  hit.ledBrightness = prefs.preferences().getUInt("led_bright", hit.ledBrightness);
  hit.piezoAoThresholdRaw = prefs.preferences().getUInt("piezo_thr", hit.piezoAoThresholdRaw);
  hit.piezoAoRearmRaw = prefs.preferences().getUInt("piezo_rearm", hit.piezoAoRearmRaw);
  if (hit.piezoAoThresholdRaw == 0) hit.piezoAoThresholdRaw = PIEZO_AO_THRESHOLD_RAW;
  hit.piezoAoCaptureWindowMs = prefs.preferences().getUInt("piezo_cap_ms", hit.piezoAoCaptureWindowMs);
  hit.piezoAoDebugPeriodMs = prefs.preferences().getUInt("piezo_dbg_ms", hit.piezoAoDebugPeriodMs);
  hit.piezoAoRearmStableMs = prefs.preferences().getUInt("piezo_arm_ms", hit.piezoAoRearmStableMs);
}

bool saveHitRuntimeConfigToNvs(battlebang::esp::nvs::ScopedPreferences& prefs,
                               const HitRuntimeConfig& hit) {
  bool ok = true;
  ok &= prefs.preferences().putString("robot_id", hit.robotId) > 0;
  ok &= prefs.preferences().putString(kHitTopicPrefixKey, hit.hitTopicPrefix) > 0;
  ok &= prefs.preferences().putUInt("hit_cd_ms", hit.hitCooldownMs) > 0;
  ok &= prefs.preferences().putUInt("offq_cap", hit.offlineQueueCapacity) > 0;
  ok &= prefs.preferences().putUInt("offq_flush", hit.offlineQueueFlushIntervalMs) > 0;
  ok &= prefs.preferences().putUInt("led_bright", hit.ledBrightness) > 0;
  ok &= prefs.preferences().putUInt("piezo_thr", hit.piezoAoThresholdRaw) > 0;
  ok &= prefs.preferences().putUInt("piezo_rearm", hit.piezoAoRearmRaw) > 0;
  ok &= prefs.preferences().putUInt("piezo_cap_ms", hit.piezoAoCaptureWindowMs) > 0;
  ok &= prefs.preferences().putUInt("piezo_dbg_ms", hit.piezoAoDebugPeriodMs) > 0;
  ok &= prefs.preferences().putUInt("piezo_arm_ms", hit.piezoAoRearmStableMs) > 0;
  return ok;
}

void writeHitRuntimeConfigJson(JsonObject root, const HitRuntimeConfig& hit) {
  root["robot_id"] = hit.robotId;
  root["hit_topic_prefix"] = hit.hitTopicPrefix;
  root["hit_cooldown_ms"] = hit.hitCooldownMs;
  root["offline_queue_capacity"] = hit.offlineQueueCapacity;
  root["offline_queue_flush_interval_ms"] = hit.offlineQueueFlushIntervalMs;
  root["led_brightness"] = hit.ledBrightness;
  root["piezo_ao_threshold_raw"] = hit.piezoAoThresholdRaw;
  root["piezo_ao_rearm_raw"] = hit.piezoAoRearmRaw;
  root["piezo_ao_capture_window_ms"] = hit.piezoAoCaptureWindowMs;
  root["piezo_ao_debug_period_ms"] = hit.piezoAoDebugPeriodMs;
  root["piezo_ao_rearm_stable_ms"] = hit.piezoAoRearmStableMs;

  JsonObject hitObject = root.createNestedObject("hit");
  hitObject["robot_id"] = hit.robotId;
  hitObject["topic_prefix"] = hit.hitTopicPrefix;
  hitObject["cooldown_ms"] = hit.hitCooldownMs;
  hitObject["offline_queue_capacity"] = hit.offlineQueueCapacity;
  hitObject["offline_queue_flush_interval_ms"] = hit.offlineQueueFlushIntervalMs;
  hitObject["led_brightness"] = hit.ledBrightness;
  hitObject["piezo_ao_threshold_raw"] = hit.piezoAoThresholdRaw;
  hitObject["piezo_ao_rearm_raw"] = hit.piezoAoRearmRaw;
  hitObject["piezo_ao_capture_window_ms"] = hit.piezoAoCaptureWindowMs;
  hitObject["piezo_ao_debug_period_ms"] = hit.piezoAoDebugPeriodMs;
  hitObject["piezo_ao_rearm_stable_ms"] = hit.piezoAoRearmStableMs;
}

void normalizeRuntimeConfig(RuntimeConfig& config) {
  battlebang::esp::config::normalizeCommonRuntimeConfig(config.common);
  config.common.deviceId.trim();
  config.common.group.trim();
  normalizeHitRuntimeConfig(config.hit);

  String buildRobotId = String(ROBOT_ID);
  buildRobotId.trim();
  if (config.hit.robotId.length() == 0) config.hit.robotId = config.common.deviceId;
  if (config.hit.robotId.length() == 0) config.hit.robotId = buildRobotId;
  if (config.hit.robotId.length() == 0) config.hit.robotId = defaultRuntimeRobotId();
  if (config.common.deviceId.length() == 0) config.common.deviceId = config.hit.robotId;
  if (config.common.group.length() == 0) config.common.group = FIRMWARE_NAME;
  config.common.mqttRoot = battlebang::esp::mqtt::normalizeRootOrDefault(config.common.mqttRoot);
}

bool validateRuntimeConfig(RuntimeConfig& config, String& error) {
  normalizeRuntimeConfig(config);
  if (!battlebang::esp::config::validateCommonRuntimeConfig(config.common, error)) return false;
  if (!validateHitRuntimeConfig(config.hit, error)) return false;
  return true;
}

}  // namespace

RuntimeConfig runtimeConfigFromBuild() {
  RuntimeConfig config;
  battlebang::esp::config::BuildTimeCommonRuntimeConfigDefaults commonDefaults;
  String buildDeviceId = String(ROBOT_ID);
  buildDeviceId.trim();
  if (buildDeviceId.length() == 0) buildDeviceId = defaultRuntimeRobotId();
  commonDefaults.deviceId = buildDeviceId.c_str();
  commonDefaults.group = FIRMWARE_NAME;
  commonDefaults.wifiSsid = WIFI_SSID;
  commonDefaults.wifiPassword = WIFI_PASSWORD;
  commonDefaults.mqttHost = MQTT_HOST;
  commonDefaults.mqttPort = MQTT_PORT;
  commonDefaults.mqttRoot = "battlebang";
  commonDefaults.otaCommandCenterControlled = true;
  commonDefaults.otaAutoCheckEnabled = false;
  commonDefaults.otaChannel = "go2";
  commonDefaults.otaPublicManifestUrl = BB_GO2_LATEST_MANIFEST_URL;
  config.common = battlebang::esp::config::makeBuildTimeCommonRuntimeConfig(commonDefaults);
  config.hit.robotId = ROBOT_ID;
  config.hit.hitTopicPrefix = battlebang::esp::mqtt::trimSlashes(String(MQTT_TOPIC_PREFIX));
  config.hit.hitCooldownMs = HIT_COOLDOWN_MS;
  config.hit.offlineQueueCapacity = OFFLINE_HIT_QUEUE_CAPACITY;
  config.hit.offlineQueueFlushIntervalMs = OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;
  config.hit.ledBrightness = HP_BAR_LED_BRIGHTNESS;
  config.hit.piezoAoThresholdRaw = PIEZO_AO_THRESHOLD_RAW;
  config.hit.piezoAoRearmRaw = PIEZO_AO_REARM_RAW;
  config.hit.piezoAoCaptureWindowMs = PIEZO_AO_CAPTURE_WINDOW_MS;
  config.hit.piezoAoDebugPeriodMs = PIEZO_AO_DEBUG_PERIOD_MS;
  config.hit.piezoAoRearmStableMs = HIT_REARM_STABLE_MS;
  normalizeRuntimeConfig(config);
  return config;
}

bool loadRuntimeConfigFromNvs(RuntimeConfig& config) {
  battlebang::esp::nvs::ScopedPreferences prefs;
  if (!prefs.begin(kConfigNamespace, true)) return false;

  const bool hasStoredConfig = prefs.preferences().getBool("configured", false);
  battlebang::esp::nvs::loadCommonRuntimeConfig(prefs.preferences(), config.common, commonNvsKeys());
  loadHitRuntimeConfigFromNvs(prefs, config.hit);
  config.common.configured = hasStoredConfig || config.common.configured;
  normalizeRuntimeConfig(config);
  return hasStoredConfig;
}

bool saveRuntimeConfigToNvs(const RuntimeConfig& config) {
  RuntimeConfig normalized = config;
  normalizeRuntimeConfig(normalized);

  battlebang::esp::nvs::ScopedPreferences prefs;
  if (!prefs.begin(kConfigNamespace, false)) return false;

  bool ok = battlebang::esp::nvs::saveCommonRuntimeConfig(
      prefs.preferences(), normalized.common, commonNvsKeys());
  ok &= saveHitRuntimeConfigToNvs(prefs, normalized.hit);
  return ok;
}

bool clearRuntimeConfigNvs() {
  return battlebang::esp::nvs::clearNamespace(kConfigNamespace);
}

RuntimeConfig runtimeConfigFromNvsOrBuild() {
  RuntimeConfig config = runtimeConfigFromBuild();
  loadRuntimeConfigFromNvs(config);
  return config;
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid JSON: ") + err.c_str();
    return false;
  }
  JsonObjectConst root = doc.as<JsonObjectConst>();

  RuntimeConfig next = config;
  if (!battlebang::esp::config::applyCommonRuntimeConfigJson(root, next.common, error)) return false;

  battlebang::esp::config::readStringField(root, "robot_id", next.hit.robotId);
  battlebang::esp::config::readStringField(root, "hit_topic_prefix", next.hit.hitTopicPrefix);
  readHitTuningJson(root, next.hit);

  JsonObjectConst hit = root["hit"].as<JsonObjectConst>();
  if (!hit.isNull()) {
    battlebang::esp::config::readStringField(hit, "robot_id", next.hit.robotId);
    battlebang::esp::config::readStringField(hit, "topic_prefix", next.hit.hitTopicPrefix);
    battlebang::esp::config::readStringField(hit, "hit_topic_prefix", next.hit.hitTopicPrefix);
    battlebang::esp::config::readUInt32Field(hit, "cooldown_ms", next.hit.hitCooldownMs);
    readHitTuningJson(hit, next.hit);
  }

  if (!validateRuntimeConfig(next, error)) return false;
  config = next;
  return true;
}

String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets) {
  DynamicJsonDocument doc(4096);
  JsonObject root = doc.to<JsonObject>();
  battlebang::esp::config::writeCommonRuntimeConfigJson(root, config.common, includeSecrets);
  writeHitRuntimeConfigJson(root, config.hit);

  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace go2
