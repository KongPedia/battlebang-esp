#include "runtime_config.h"

#include <ArduinoJson.h>
#include <Esp.h>

#include <stdio.h>
#include <bb_esp_core/config/build_time_config.h>
#include <bb_esp_core/config/runtime_config_json.h>
#include <bb_esp_core/mqtt/topic_utils.h>
#include <bb_esp_nvs/common_runtime_config_store.h>

#include "go2_nixo/build_config.h"
#include "go2_nixo/app/firmware_info.h"

namespace go2 {
namespace {

const char* kConfigNamespace = "go2_nixo";
const char* kHitTopicPrefixKey = "hit_topic";
const char* kNixoCommandTopicPrefixKey = "nixo_cmd_topic";

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
  return String("go2-nixo-") + efuseMacHex();
}

String defaultRuntimeNixoId(const String& robotId) {
  return String("nixo_") + robotId;
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
  readUInt16ConfigField(object, "ring_brightness", hit.ringBrightness);
  readUInt16ConfigField(object, "piezo_ao_threshold_raw", hit.piezoAoThresholdRaw);
  readUInt16ConfigField(object, "piezo_ao_rearm_raw", hit.piezoAoRearmRaw);
  battlebang::esp::config::readUInt32Field(
      object, "piezo_ao_capture_window_ms", hit.piezoAoCaptureWindowMs);
  battlebang::esp::config::readUInt32Field(
      object, "piezo_ao_debug_period_ms", hit.piezoAoDebugPeriodMs);
  battlebang::esp::config::readUInt32Field(
      object, "piezo_ao_rearm_stable_ms", hit.piezoAoRearmStableMs);
}

void readNixoTuningJson(JsonObjectConst object, NixoRuntimeConfig& nixo) {
  if (object.isNull()) return;
  battlebang::esp::config::readUInt32Field(
      object, "nixo_fire_default_duration_ms", nixo.fireDefaultDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "nixo_fire_min_duration_ms", nixo.fireMinDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "nixo_fire_max_duration_ms", nixo.fireMaxDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "nixo_fire_cooldown_ms", nixo.fireCooldownMs);
  battlebang::esp::config::readUInt32Field(
      object, "nixo_prefire_delay_ms", nixo.prefireDelayMs);
  battlebang::esp::config::readUInt32Field(
      object, "nixo_relay_delay1_ms", nixo.relayDelay1Ms);

  battlebang::esp::config::readUInt32Field(
      object, "fire_default_duration_ms", nixo.fireDefaultDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "fire_min_duration_ms", nixo.fireMinDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "fire_max_duration_ms", nixo.fireMaxDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "fire_cooldown_ms", nixo.fireCooldownMs);
  battlebang::esp::config::readUInt32Field(
      object, "default_duration_ms", nixo.fireDefaultDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "min_duration_ms", nixo.fireMinDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "max_duration_ms", nixo.fireMaxDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "cooldown_ms", nixo.fireCooldownMs);
  battlebang::esp::config::readUInt32Field(
      object, "prefire_delay_ms", nixo.prefireDelayMs);
  battlebang::esp::config::readUInt32Field(
      object, "relay_delay1_ms", nixo.relayDelay1Ms);
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
  if (hit.ringBrightness > 255) {
    error = "ring_brightness must be 0..255";
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

void normalizeNixoRuntimeConfig(NixoRuntimeConfig& nixo) {
  nixo.nixoId.trim();
  nixo.commandTopicPrefix = battlebang::esp::mqtt::trimSlashes(nixo.commandTopicPrefix);
  if (nixo.fireMinDurationMs == 0) nixo.fireMinDurationMs = NIXO_FIRE_MIN_DURATION_MS;
  if (nixo.fireMaxDurationMs == 0) nixo.fireMaxDurationMs = NIXO_FIRE_MAX_DURATION_MS;
  if (nixo.fireDefaultDurationMs == 0) {
    nixo.fireDefaultDurationMs = NIXO_FIRE_DEFAULT_DURATION_MS;
  }
}

bool validateNixoRuntimeConfig(const NixoRuntimeConfig& nixo, String& error) {
  if (!battlebang::esp::mqtt::isSafeTopicSegment(nixo.nixoId)) {
    error = "nixo_id must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  if (!battlebang::esp::mqtt::isSafeTopicPath(nixo.commandTopicPrefix)) {
    error = "nixo.command_topic_prefix must be a safe slash-separated MQTT topic path";
    return false;
  }
  if (nixo.fireMinDurationMs == 0) {
    error = "nixo.fire_min_duration_ms must be positive";
    return false;
  }
  if (nixo.fireMaxDurationMs < nixo.fireMinDurationMs) {
    error = "nixo.fire_max_duration_ms must be >= fire_min_duration_ms";
    return false;
  }
  if (nixo.fireDefaultDurationMs < nixo.fireMinDurationMs ||
      nixo.fireDefaultDurationMs > nixo.fireMaxDurationMs) {
    error = "nixo.fire_default_duration_ms must be within min/max duration";
    return false;
  }
  if (nixo.fireMaxDurationMs > 60000UL) {
    error = "nixo.fire_max_duration_ms must be <= 60000";
    return false;
  }
  if (nixo.fireCooldownMs > 60000UL) {
    error = "nixo.fire_cooldown_ms must be <= 60000";
    return false;
  }
  if (nixo.prefireDelayMs > 60000UL) {
    error = "nixo.prefire_delay_ms must be <= 60000";
    return false;
  }
  if (nixo.relayDelay1Ms > 60000UL) {
    error = "nixo.relay_delay1_ms must be <= 60000";
    return false;
  }
  return true;
}

void normalizeRuntimeConfig(RuntimeConfig& config) {
  battlebang::esp::config::normalizeCommonRuntimeConfig(config.common);
  config.common.deviceId.trim();
  config.common.group.trim();
  normalizeHitRuntimeConfig(config.hit);
  normalizeNixoRuntimeConfig(config.nixo);

  String buildRobotId = String(ROBOT_ID);
  buildRobotId.trim();
  if (config.hit.robotId.length() == 0) config.hit.robotId = config.common.deviceId;
  if (config.hit.robotId.length() == 0) config.hit.robotId = buildRobotId;
  if (config.hit.robotId.length() == 0) config.hit.robotId = defaultRuntimeRobotId();
  if (config.common.deviceId.length() == 0) config.common.deviceId = config.hit.robotId;
  if (config.common.group.length() == 0) config.common.group = FIRMWARE_NAME;

  String buildNixoId = String(NIXO_ID_VALUE);
  buildNixoId.trim();
  if (config.nixo.nixoId.length() == 0) config.nixo.nixoId = buildNixoId;
  if (config.nixo.nixoId.length() == 0) config.nixo.nixoId = defaultRuntimeNixoId(config.hit.robotId);
  if (config.nixo.commandTopicPrefix.length() == 0) {
    config.nixo.commandTopicPrefix =
        battlebang::esp::mqtt::trimSlashes(String(NIXO_MQTT_TOPIC_PREFIX_VALUE));
  }
  config.common.mqttRoot = battlebang::esp::mqtt::normalizeRootOrDefault(config.common.mqttRoot);
}

bool validateRuntimeConfig(RuntimeConfig& config, String& error) {
  normalizeRuntimeConfig(config);
  if (!battlebang::esp::config::validateCommonRuntimeConfig(config.common, error)) return false;
  if (!validateHitRuntimeConfig(config.hit, error)) return false;
  if (!validateNixoRuntimeConfig(config.nixo, error)) return false;
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
  hit.ringBrightness = prefs.preferences().getUInt("ring_bright", hit.ringBrightness);
  hit.piezoAoThresholdRaw = prefs.preferences().getUInt("piezo_thr", hit.piezoAoThresholdRaw);
  hit.piezoAoRearmRaw = prefs.preferences().getUInt("piezo_rearm", hit.piezoAoRearmRaw);
  if (hit.piezoAoThresholdRaw == 0) hit.piezoAoThresholdRaw = PIEZO_AO_THRESHOLD_RAW;
  hit.piezoAoCaptureWindowMs = prefs.preferences().getUInt("piezo_cap_ms", hit.piezoAoCaptureWindowMs);
  hit.piezoAoDebugPeriodMs = prefs.preferences().getUInt("piezo_dbg_ms", hit.piezoAoDebugPeriodMs);
  hit.piezoAoRearmStableMs = prefs.preferences().getUInt("piezo_arm_ms", hit.piezoAoRearmStableMs);
}

void loadNixoRuntimeConfigFromNvs(battlebang::esp::nvs::ScopedPreferences& prefs,
                                  NixoRuntimeConfig& nixo) {
  nixo.nixoId = prefs.preferences().getString("nixo_id", nixo.nixoId);
  nixo.commandTopicPrefix =
      prefs.preferences().getString(kNixoCommandTopicPrefixKey, nixo.commandTopicPrefix);
  nixo.fireDefaultDurationMs = prefs.preferences().getUInt("fire_def_ms", nixo.fireDefaultDurationMs);
  nixo.fireMinDurationMs = prefs.preferences().getUInt("fire_min_ms", nixo.fireMinDurationMs);
  nixo.fireMaxDurationMs = prefs.preferences().getUInt("fire_max_ms", nixo.fireMaxDurationMs);
  nixo.fireCooldownMs = prefs.preferences().getUInt("fire_cd_ms", nixo.fireCooldownMs);
  nixo.prefireDelayMs = prefs.preferences().getUInt("prefire_ms", nixo.prefireDelayMs);
  nixo.relayDelay1Ms = prefs.preferences().getUInt("relay_dly1_ms", nixo.relayDelay1Ms);
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
  ok &= prefs.preferences().putUInt("ring_bright", hit.ringBrightness) > 0;
  ok &= prefs.preferences().putUInt("piezo_thr", hit.piezoAoThresholdRaw) > 0;
  ok &= prefs.preferences().putUInt("piezo_rearm", hit.piezoAoRearmRaw) > 0;
  ok &= prefs.preferences().putUInt("piezo_cap_ms", hit.piezoAoCaptureWindowMs) > 0;
  ok &= prefs.preferences().putUInt("piezo_dbg_ms", hit.piezoAoDebugPeriodMs) > 0;
  ok &= prefs.preferences().putUInt("piezo_arm_ms", hit.piezoAoRearmStableMs) > 0;
  return ok;
}

bool saveNixoRuntimeConfigToNvs(battlebang::esp::nvs::ScopedPreferences& prefs,
                                const NixoRuntimeConfig& nixo) {
  bool ok = true;
  ok &= prefs.preferences().putString("nixo_id", nixo.nixoId) > 0;
  ok &= prefs.preferences().putString(kNixoCommandTopicPrefixKey, nixo.commandTopicPrefix) > 0;
  ok &= prefs.preferences().putUInt("fire_def_ms", nixo.fireDefaultDurationMs) > 0;
  ok &= prefs.preferences().putUInt("fire_min_ms", nixo.fireMinDurationMs) > 0;
  ok &= prefs.preferences().putUInt("fire_max_ms", nixo.fireMaxDurationMs) > 0;
  ok &= prefs.preferences().putUInt("fire_cd_ms", nixo.fireCooldownMs) > 0;
  ok &= prefs.preferences().putUInt("prefire_ms", nixo.prefireDelayMs) > 0;
  ok &= prefs.preferences().putUInt("relay_dly1_ms", nixo.relayDelay1Ms) > 0;
  return ok;
}

void writeHitRuntimeConfigJson(JsonObject root, const HitRuntimeConfig& hit) {
  root["robot_id"] = hit.robotId;
  root["hit_topic_prefix"] = hit.hitTopicPrefix;
  root["hit_cooldown_ms"] = hit.hitCooldownMs;
  root["offline_queue_capacity"] = hit.offlineQueueCapacity;
  root["offline_queue_flush_interval_ms"] = hit.offlineQueueFlushIntervalMs;
  root["led_brightness"] = hit.ledBrightness;
  root["ring_brightness"] = hit.ringBrightness;
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
  hitObject["ring_brightness"] = hit.ringBrightness;
  hitObject["piezo_ao_threshold_raw"] = hit.piezoAoThresholdRaw;
  hitObject["piezo_ao_rearm_raw"] = hit.piezoAoRearmRaw;
  hitObject["piezo_ao_capture_window_ms"] = hit.piezoAoCaptureWindowMs;
  hitObject["piezo_ao_debug_period_ms"] = hit.piezoAoDebugPeriodMs;
  hitObject["piezo_ao_rearm_stable_ms"] = hit.piezoAoRearmStableMs;
}

void writeNixoRuntimeConfigJson(JsonObject root, const NixoRuntimeConfig& nixo) {
  root["nixo_id"] = nixo.nixoId;
  root["nixo_command_topic_prefix"] = nixo.commandTopicPrefix;
  root["nixo_fire_default_duration_ms"] = nixo.fireDefaultDurationMs;
  root["nixo_fire_min_duration_ms"] = nixo.fireMinDurationMs;
  root["nixo_fire_max_duration_ms"] = nixo.fireMaxDurationMs;
  root["nixo_fire_cooldown_ms"] = nixo.fireCooldownMs;
  root["nixo_prefire_delay_ms"] = nixo.prefireDelayMs;
  root["nixo_relay_delay1_ms"] = nixo.relayDelay1Ms;

  JsonObject nixoObject = root.createNestedObject("nixo");
  nixoObject["id"] = nixo.nixoId;
  nixoObject["command_topic_prefix"] = nixo.commandTopicPrefix;
  nixoObject["fire_default_duration_ms"] = nixo.fireDefaultDurationMs;
  nixoObject["fire_min_duration_ms"] = nixo.fireMinDurationMs;
  nixoObject["fire_max_duration_ms"] = nixo.fireMaxDurationMs;
  nixoObject["fire_cooldown_ms"] = nixo.fireCooldownMs;
  nixoObject["prefire_delay_ms"] = nixo.prefireDelayMs;
  nixoObject["relay_delay1_ms"] = nixo.relayDelay1Ms;
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
  commonDefaults.otaChannel = BB_GO2_NIXO_OTA_CHANNEL;
  commonDefaults.otaPublicManifestUrl = BB_GO2_NIXO_LATEST_MANIFEST_URL;
  config.common = battlebang::esp::config::makeBuildTimeCommonRuntimeConfig(commonDefaults);
  config.hit.robotId = ROBOT_ID;
  config.hit.hitTopicPrefix = battlebang::esp::mqtt::trimSlashes(String(MQTT_TOPIC_PREFIX));
  config.hit.hitCooldownMs = HIT_COOLDOWN_MS;
  config.hit.offlineQueueCapacity = OFFLINE_HIT_QUEUE_CAPACITY;
  config.hit.offlineQueueFlushIntervalMs = OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;
  config.hit.ledBrightness = HP_BAR_LED_BRIGHTNESS;
  config.hit.ringBrightness = RING_LED_BRIGHTNESS;
  config.hit.piezoAoThresholdRaw = PIEZO_AO_THRESHOLD_RAW;
  config.hit.piezoAoRearmRaw = PIEZO_AO_REARM_RAW;
  config.hit.piezoAoCaptureWindowMs = PIEZO_AO_CAPTURE_WINDOW_MS;
  config.hit.piezoAoDebugPeriodMs = PIEZO_AO_DEBUG_PERIOD_MS;
  config.hit.piezoAoRearmStableMs = HIT_REARM_STABLE_MS;
  config.nixo.nixoId = NIXO_ID_VALUE;
  config.nixo.commandTopicPrefix = battlebang::esp::mqtt::trimSlashes(String(NIXO_MQTT_TOPIC_PREFIX_VALUE));
  config.nixo.fireDefaultDurationMs = NIXO_FIRE_DEFAULT_DURATION_MS;
  config.nixo.fireMinDurationMs = NIXO_FIRE_MIN_DURATION_MS;
  config.nixo.fireMaxDurationMs = NIXO_FIRE_MAX_DURATION_MS;
  config.nixo.fireCooldownMs = NIXO_FIRE_COOLDOWN_MS;
  config.nixo.prefireDelayMs = NIXO_PREFIRE_DELAY_MS;
  config.nixo.relayDelay1Ms = NIXO_RELAY_DELAY1_MS;
  normalizeRuntimeConfig(config);
  return config;
}

bool loadRuntimeConfigFromNvs(RuntimeConfig& config) {
  battlebang::esp::nvs::ScopedPreferences prefs;
  if (!prefs.begin(kConfigNamespace, true)) return false;

  const bool hasStoredConfig = prefs.preferences().getBool("configured", false);
  battlebang::esp::nvs::loadCommonRuntimeConfig(prefs.preferences(), config.common, commonNvsKeys());
  loadHitRuntimeConfigFromNvs(prefs, config.hit);
  loadNixoRuntimeConfigFromNvs(prefs, config.nixo);
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
  ok &= saveNixoRuntimeConfigToNvs(prefs, normalized.nixo);
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
  battlebang::esp::config::readStringField(root, "nixo_id", next.nixo.nixoId);
  battlebang::esp::config::readStringField(root, "nixo_command_topic_prefix", next.nixo.commandTopicPrefix);
  readHitTuningJson(root, next.hit);
  readNixoTuningJson(root, next.nixo);

  JsonObjectConst hit = root["hit"].as<JsonObjectConst>();
  if (!hit.isNull()) {
    battlebang::esp::config::readStringField(hit, "robot_id", next.hit.robotId);
    battlebang::esp::config::readStringField(hit, "topic_prefix", next.hit.hitTopicPrefix);
    battlebang::esp::config::readStringField(hit, "hit_topic_prefix", next.hit.hitTopicPrefix);
    battlebang::esp::config::readUInt32Field(hit, "cooldown_ms", next.hit.hitCooldownMs);
    readHitTuningJson(hit, next.hit);
  }

  JsonObjectConst nixo = root["nixo"].as<JsonObjectConst>();
  if (!nixo.isNull()) {
    battlebang::esp::config::readStringField(nixo, "id", next.nixo.nixoId);
    battlebang::esp::config::readStringField(nixo, "nixo_id", next.nixo.nixoId);
    battlebang::esp::config::readStringField(nixo, "command_topic_prefix", next.nixo.commandTopicPrefix);
    readNixoTuningJson(nixo, next.nixo);
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
  writeNixoRuntimeConfigJson(root, config.nixo);

  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace go2
