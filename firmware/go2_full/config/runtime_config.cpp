#include "runtime_config.h"

#include <ArduinoJson.h>
#include <Esp.h>

#include <stdio.h>
#include <bb_esp_core/config/build_time_config.h>
#include <bb_esp_core/config/runtime_config_json.h>
#include <bb_esp_core/mqtt/topic_utils.h>
#include <bb_esp_nvs/common_runtime_config_store.h>

#include "go2_full/build_config.h"
#include "go2_full/app/firmware_info.h"

namespace go2 {
namespace {

const char* kConfigNamespace = "go2_full";
const char* kHardwareNamespace = "go2_full_hw";
// Read-only import boundary. No old firmware mode is retained.
const char* kPreviousConfigNamespace = "go2_nixo";
const char* kPreviousHardwareNamespace = "go2_nixo_hw";
const char* kMigrationCompleteKey = "migration_done";
const char* kHitTopicPrefixKey = "hit_topic";
const char* kBlasterCommandTopicPrefixKey = "blaster_topic";

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

String defaultRuntimeBlasterId(const String& robotId) {
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
  uint16_t channelEnableMask = hit.piezoChannelEnableMask;
  readUInt16ConfigField(object, "piezo_channel_enable_mask", channelEnableMask);
  hit.piezoChannelEnableMask = static_cast<uint8_t>(
      channelEnableMask > 0xffU ? 0xffU : channelEnableMask);
  uint16_t barGroupCount = hit.hpBarGroupCount;
  readUInt16ConfigField(object, "hp_bar_group_count", barGroupCount);
  hit.hpBarGroupCount = static_cast<uint8_t>(barGroupCount > 0xffU ? 0xffU : barGroupCount);
  uint16_t barColorOrder = hit.hpBarColorOrder;
  readUInt16ConfigField(object, "hp_bar_color_order", barColorOrder);
  hit.hpBarColorOrder = static_cast<uint8_t>(barColorOrder > 0xffU ? 0xffU : barColorOrder);
  readUInt16ConfigField(object, "max_hits", hit.maxHits);
  readUInt16ConfigField(object, "hits_to_down", hit.maxHits);
  battlebang::esp::config::readUInt32Field(object, "hit_flash_ms", hit.hitFlashMs);
}

void readBlasterTuningJson(JsonObjectConst object, BlasterRuntimeConfig& blaster) {
  if (object.isNull()) return;
  battlebang::esp::config::readUInt32Field(
      object, "blaster_fire_default_duration_ms", blaster.fireDefaultDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "blaster_fire_min_duration_ms", blaster.fireMinDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "blaster_fire_max_duration_ms", blaster.fireMaxDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "blaster_fire_cooldown_ms", blaster.fireCooldownMs);
  battlebang::esp::config::readUInt32Field(
      object, "blaster_prefire_delay_ms", blaster.prefireDelayMs);
  battlebang::esp::config::readUInt32Field(
      object, "blaster_relay_delay1_ms", blaster.relayDelay1Ms);

  battlebang::esp::config::readUInt32Field(
      object, "fire_default_duration_ms", blaster.fireDefaultDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "fire_min_duration_ms", blaster.fireMinDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "fire_max_duration_ms", blaster.fireMaxDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "fire_cooldown_ms", blaster.fireCooldownMs);
  battlebang::esp::config::readUInt32Field(
      object, "default_duration_ms", blaster.fireDefaultDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "min_duration_ms", blaster.fireMinDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "max_duration_ms", blaster.fireMaxDurationMs);
  battlebang::esp::config::readUInt32Field(
      object, "cooldown_ms", blaster.fireCooldownMs);
  battlebang::esp::config::readUInt32Field(
      object, "prefire_delay_ms", blaster.prefireDelayMs);
  battlebang::esp::config::readUInt32Field(
      object, "relay_delay1_ms", blaster.relayDelay1Ms);
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
  if (hit.maxHits == 0) hit.maxHits = MAX_HITS;
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
  if ((hit.piezoChannelEnableMask & ~0x07U) != 0) {
    error = "piezo_channel_enable_mask must use only bits 0..2";
    return false;
  }
  if (hit.hpBarGroupCount == 0 || hit.hpBarGroupCount > HP_BAR_MAX_GROUP_COUNT) {
    error = "hp_bar_group_count must be 1..29";
    return false;
  }
  if (hit.hpBarColorOrder > 5) {
    error = "hp_bar_color_order must be 0..5 (RGB, GRB, RBG, GBR, BRG, BGR)";
    return false;
  }
  if (hit.maxHits == 0 || hit.maxHits > 1000) {
    error = "max_hits must be 1..1000";
    return false;
  }
  if (hit.hitFlashMs > 60000UL) {
    error = "hit_flash_ms must be <= 60000";
    return false;
  }
  return true;
}

void normalizeBlasterRuntimeConfig(BlasterRuntimeConfig& blaster) {
  blaster.blasterId.trim();
  blaster.commandTopicPrefix = battlebang::esp::mqtt::trimSlashes(blaster.commandTopicPrefix);
  if (blaster.fireMinDurationMs == 0) blaster.fireMinDurationMs = BLASTER_FIRE_MIN_DURATION_MS;
  if (blaster.fireMaxDurationMs == 0) blaster.fireMaxDurationMs = BLASTER_FIRE_MAX_DURATION_MS;
  if (blaster.fireDefaultDurationMs == 0) {
    blaster.fireDefaultDurationMs = BLASTER_FIRE_DEFAULT_DURATION_MS;
  }
  blaster.fireCooldownMs = 0;  // BTB-801: Jetson/gamepad owns fire rate limiting.
}

bool validateBlasterRuntimeConfig(const BlasterRuntimeConfig& blaster, String& error) {
  if (!battlebang::esp::mqtt::isSafeTopicSegment(blaster.blasterId)) {
    error = "blaster_id must use only A-Z, a-z, 0-9, '_', '-', or '.'";
    return false;
  }
  if (!battlebang::esp::mqtt::isSafeTopicPath(blaster.commandTopicPrefix)) {
    error = "blaster.command_topic_prefix must be a safe slash-separated MQTT topic path";
    return false;
  }
  if (blaster.fireMinDurationMs == 0) {
    error = "blaster.fire_min_duration_ms must be positive";
    return false;
  }
  if (blaster.fireMaxDurationMs < blaster.fireMinDurationMs) {
    error = "blaster.fire_max_duration_ms must be >= fire_min_duration_ms";
    return false;
  }
  if (blaster.fireDefaultDurationMs < blaster.fireMinDurationMs ||
      blaster.fireDefaultDurationMs > blaster.fireMaxDurationMs) {
    error = "blaster.fire_default_duration_ms must be within min/max duration";
    return false;
  }
  if (blaster.fireMaxDurationMs > 60000UL) {
    error = "blaster.fire_max_duration_ms must be <= 60000";
    return false;
  }
  if (blaster.fireCooldownMs > 60000UL) {
    error = "blaster.fire_cooldown_ms must be <= 60000";
    return false;
  }
  if (blaster.prefireDelayMs > 60000UL) {
    error = "blaster.prefire_delay_ms must be <= 60000";
    return false;
  }
  if (blaster.relayDelay1Ms > 60000UL) {
    error = "blaster.relay_delay1_ms must be <= 60000";
    return false;
  }
  return true;
}

void normalizeRuntimeConfig(RuntimeConfig& config) {
  battlebang::esp::config::normalizeCommonRuntimeConfig(config.common);
  config.common.deviceId.trim();
  config.common.group.trim();
  normalizeHitRuntimeConfig(config.hit);
  normalizeBlasterRuntimeConfig(config.blaster);

  String buildRobotId = String(ROBOT_ID);
  buildRobotId.trim();
  if (config.hit.robotId.length() == 0) config.hit.robotId = config.common.deviceId;
  if (config.hit.robotId.length() == 0) config.hit.robotId = buildRobotId;
  if (config.hit.robotId.length() == 0) config.hit.robotId = defaultRuntimeRobotId();
  if (config.common.deviceId.length() == 0) config.common.deviceId = config.hit.robotId;
  if (config.common.group.length() == 0) config.common.group = "go2_nixo";  // Deployed MQTT routing group.

  String buildBlasterId = String(BLASTER_ID_VALUE);
  buildBlasterId.trim();
  if (config.blaster.blasterId.length() == 0) config.blaster.blasterId = buildBlasterId;
  if (config.blaster.blasterId.length() == 0) config.blaster.blasterId = defaultRuntimeBlasterId(config.hit.robotId);
  if (config.blaster.commandTopicPrefix.length() == 0) {
    config.blaster.commandTopicPrefix =
        battlebang::esp::mqtt::trimSlashes(String(BLASTER_MQTT_TOPIC_PREFIX_VALUE));
  }
  config.common.mqttRoot = battlebang::esp::mqtt::normalizeRootOrDefault(config.common.mqttRoot);
}

bool validateRuntimeConfig(RuntimeConfig& config, String& error) {
  normalizeRuntimeConfig(config);
  if (!battlebang::esp::config::validateCommonRuntimeConfig(config.common, error)) return false;
  if (!validateHitRuntimeConfig(config.hit, error)) return false;
  if (!validateBlasterRuntimeConfig(config.blaster, error)) return false;
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
  // Missing key preserves the build default, so NVS written by older firmware
  // remains compatible (left/right enabled, front disabled by default).
  hit.piezoChannelEnableMask = static_cast<uint8_t>(
      prefs.preferences().getUChar("piezo_mask", hit.piezoChannelEnableMask));
  if ((hit.piezoChannelEnableMask & ~0x07U) != 0) {
    hit.piezoChannelEnableMask = PIEZO_CHANNEL_ENABLE_MASK;
  }
  hit.hpBarGroupCount = prefs.preferences().getUChar("bar_groups", hit.hpBarGroupCount);
  if (hit.hpBarGroupCount == 0 || hit.hpBarGroupCount > HP_BAR_MAX_GROUP_COUNT) {
    hit.hpBarGroupCount = HP_BAR_GROUP_COUNT;
  }
  hit.hpBarColorOrder = prefs.preferences().getUChar("bar_color", hit.hpBarColorOrder);
  if (hit.hpBarColorOrder > 5) hit.hpBarColorOrder = 0;
  hit.maxHits = prefs.preferences().getUInt("max_hits", hit.maxHits);
  hit.hitFlashMs = prefs.preferences().getUInt("hit_flash", hit.hitFlashMs);
}

void loadBlasterRuntimeConfigFromNvs(battlebang::esp::nvs::ScopedPreferences& prefs,
                                  BlasterRuntimeConfig& blaster, bool previousNames = false) {
  blaster.blasterId = prefs.preferences().getString(
      previousNames ? "nixo_id" : "blaster_id", blaster.blasterId);
  blaster.commandTopicPrefix = prefs.preferences().getString(
      previousNames ? "nixo_cmd_topic" : kBlasterCommandTopicPrefixKey,
      blaster.commandTopicPrefix);
  blaster.fireDefaultDurationMs = prefs.preferences().getUInt("fire_def_ms", blaster.fireDefaultDurationMs);
  blaster.fireMinDurationMs = prefs.preferences().getUInt("fire_min_ms", blaster.fireMinDurationMs);
  blaster.fireMaxDurationMs = prefs.preferences().getUInt("fire_max_ms", blaster.fireMaxDurationMs);
  blaster.fireCooldownMs = prefs.preferences().getUInt("fire_cd_ms", blaster.fireCooldownMs);
  blaster.prefireDelayMs = prefs.preferences().getUInt("prefire_ms", blaster.prefireDelayMs);
  blaster.relayDelay1Ms = prefs.preferences().getUInt("relay_dly1_ms", blaster.relayDelay1Ms);
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
  ok &= prefs.preferences().putUChar("piezo_mask", hit.piezoChannelEnableMask) > 0;
  ok &= prefs.preferences().putUChar("bar_groups", hit.hpBarGroupCount) > 0;
  ok &= prefs.preferences().putUChar("bar_color", hit.hpBarColorOrder) > 0;
  ok &= prefs.preferences().putUInt("max_hits", hit.maxHits) > 0;
  ok &= prefs.preferences().putUInt("hit_flash", hit.hitFlashMs) > 0;
  return ok;
}

bool saveBlasterRuntimeConfigToNvs(battlebang::esp::nvs::ScopedPreferences& prefs,
                                const BlasterRuntimeConfig& blaster) {
  bool ok = true;
  ok &= prefs.preferences().putString("blaster_id", blaster.blasterId) > 0;
  ok &= prefs.preferences().putString(kBlasterCommandTopicPrefixKey, blaster.commandTopicPrefix) > 0;
  ok &= prefs.preferences().putUInt("fire_def_ms", blaster.fireDefaultDurationMs) > 0;
  ok &= prefs.preferences().putUInt("fire_min_ms", blaster.fireMinDurationMs) > 0;
  ok &= prefs.preferences().putUInt("fire_max_ms", blaster.fireMaxDurationMs) > 0;
  ok &= prefs.preferences().putUInt("fire_cd_ms", blaster.fireCooldownMs) > 0;
  ok &= prefs.preferences().putUInt("prefire_ms", blaster.prefireDelayMs) > 0;
  ok &= prefs.preferences().putUInt("relay_dly1_ms", blaster.relayDelay1Ms) > 0;
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
  root["piezo_channel_enable_mask"] = hit.piezoChannelEnableMask;
  root["hp_bar_group_count"] = hit.hpBarGroupCount;
  root["hp_bar_color_order"] = hit.hpBarColorOrder;
  root["max_hits"] = hit.maxHits;
  root["hit_flash_ms"] = hit.hitFlashMs;

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
  hitObject["piezo_channel_enable_mask"] = hit.piezoChannelEnableMask;
  hitObject["hp_bar_group_count"] = hit.hpBarGroupCount;
  hitObject["hp_bar_color_order"] = hit.hpBarColorOrder;
  hitObject["max_hits"] = hit.maxHits;
  hitObject["hits_to_down"] = hit.maxHits;
  hitObject["hit_flash_ms"] = hit.hitFlashMs;
}

void writeBlasterRuntimeConfigJson(JsonObject root, const BlasterRuntimeConfig& blaster) {
  root["blaster_id"] = blaster.blasterId;
  root["blaster_command_topic_prefix"] = blaster.commandTopicPrefix;
  root["blaster_fire_default_duration_ms"] = blaster.fireDefaultDurationMs;
  root["blaster_fire_min_duration_ms"] = blaster.fireMinDurationMs;
  root["blaster_fire_max_duration_ms"] = blaster.fireMaxDurationMs;
  root["blaster_fire_cooldown_ms"] = blaster.fireCooldownMs;
  root["blaster_prefire_delay_ms"] = blaster.prefireDelayMs;
  root["blaster_relay_delay1_ms"] = blaster.relayDelay1Ms;

  JsonObject blasterObject = root.createNestedObject("blaster");
  blasterObject["id"] = blaster.blasterId;
  blasterObject["command_topic_prefix"] = blaster.commandTopicPrefix;
  blasterObject["fire_default_duration_ms"] = blaster.fireDefaultDurationMs;
  blasterObject["fire_min_duration_ms"] = blaster.fireMinDurationMs;
  blasterObject["fire_max_duration_ms"] = blaster.fireMaxDurationMs;
  blasterObject["fire_cooldown_ms"] = blaster.fireCooldownMs;
  blasterObject["prefire_delay_ms"] = blaster.prefireDelayMs;
  blasterObject["relay_delay1_ms"] = blaster.relayDelay1Ms;
}

}  // namespace

RuntimeConfig runtimeConfigFromBuild() {
  RuntimeConfig config;
  battlebang::esp::config::BuildTimeCommonRuntimeConfigDefaults commonDefaults;
  String buildDeviceId = String(ROBOT_ID);
  buildDeviceId.trim();
  if (buildDeviceId.length() == 0) buildDeviceId = defaultRuntimeRobotId();
  commonDefaults.deviceId = buildDeviceId.c_str();
  commonDefaults.group = "go2_nixo";  // Deployed MQTT routing group.
  commonDefaults.wifiSsid = WIFI_SSID;
  commonDefaults.wifiPassword = WIFI_PASSWORD;
  commonDefaults.mqttHost = MQTT_HOST;
  commonDefaults.mqttPort = MQTT_PORT;
  commonDefaults.mqttRoot = "battlebang";
  commonDefaults.otaCommandCenterControlled = true;
  commonDefaults.otaAutoCheckEnabled = false;
  commonDefaults.otaChannel = BB_GO2_FULL_OTA_CHANNEL;
  commonDefaults.otaPublicManifestUrl = BB_GO2_FULL_LATEST_MANIFEST_URL;
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
  config.hit.piezoChannelEnableMask = PIEZO_CHANNEL_ENABLE_MASK;
  config.hit.hpBarGroupCount = HP_BAR_GROUP_COUNT;
  config.hit.hpBarColorOrder = 0;
  config.hit.maxHits = MAX_HITS;
  config.hit.hitFlashMs = HIT_FLASH_MS;
  config.blaster.blasterId = BLASTER_ID_VALUE;
  config.blaster.commandTopicPrefix = battlebang::esp::mqtt::trimSlashes(String(BLASTER_MQTT_TOPIC_PREFIX_VALUE));
  config.blaster.fireDefaultDurationMs = BLASTER_FIRE_DEFAULT_DURATION_MS;
  config.blaster.fireMinDurationMs = BLASTER_FIRE_MIN_DURATION_MS;
  config.blaster.fireMaxDurationMs = BLASTER_FIRE_MAX_DURATION_MS;
  config.blaster.fireCooldownMs = BLASTER_FIRE_COOLDOWN_MS;
  config.blaster.prefireDelayMs = BLASTER_PREFIRE_DELAY_MS;
  config.blaster.relayDelay1Ms = BLASTER_RELAY_DELAY1_MS;
  normalizeRuntimeConfig(config);
  return config;
}

static bool loadStoredRuntimeConfig(RuntimeConfig& config,
                                    const char* configNamespace,
                                    const char* hardwareNamespace,
                                    bool previousNames) {
  bool hasStoredConfig = false;
  {
    battlebang::esp::nvs::ScopedPreferences prefs;
    if (prefs.begin(configNamespace, true)) {
      hasStoredConfig = prefs.preferences().getBool("configured", false);
      battlebang::esp::nvs::loadCommonRuntimeConfig(prefs.preferences(), config.common, commonNvsKeys());
      loadHitRuntimeConfigFromNvs(prefs, config.hit);
      loadBlasterRuntimeConfigFromNvs(prefs, config.blaster, previousNames);
    }
  }
  // Older NVS images have no factory namespace; keep the shared-config values.
  battlebang::esp::nvs::ScopedPreferences hardware;
  if (hardware.begin(hardwareNamespace, true)) {
    if (hardware.preferences().isKey("piezo_mask")) {
      config.hit.piezoChannelEnableMask = hardware.preferences().getUChar("piezo_mask", config.hit.piezoChannelEnableMask);
    }
    if (hardware.preferences().isKey("bar_groups")) {
      config.hit.hpBarGroupCount = hardware.preferences().getUChar("bar_groups", config.hit.hpBarGroupCount);
    }
    if (hardware.preferences().isKey("bar_color")) {
      config.hit.hpBarColorOrder = hardware.preferences().getUChar("bar_color", config.hit.hpBarColorOrder);
    }
  }
  if ((config.hit.piezoChannelEnableMask & ~0x07U) != 0) config.hit.piezoChannelEnableMask = PIEZO_CHANNEL_ENABLE_MASK;
  if (config.hit.hpBarGroupCount == 0 || config.hit.hpBarGroupCount > HP_BAR_MAX_GROUP_COUNT) {
    config.hit.hpBarGroupCount = HP_BAR_GROUP_COUNT;
  }
  if (config.hit.hpBarColorOrder > 5) config.hit.hpBarColorOrder = 0;
  config.common.configured = hasStoredConfig || config.common.configured;
  normalizeRuntimeConfig(config);
  return hasStoredConfig;
}

static bool canonicalStorageReady() {
  battlebang::esp::nvs::ScopedPreferences hardware;
  return hardware.begin(kHardwareNamespace, true) &&
         hardware.preferences().getBool(kMigrationCompleteKey, false);
}

bool loadRuntimeConfigFromNvs(RuntimeConfig& config) {
  config = runtimeConfigFromBuild();
  if (canonicalStorageReady()) {
    // Includes an intentionally cleared config: never resurrect the old store.
    return loadStoredRuntimeConfig(config, kConfigNamespace, kHardwareNamespace, false);
  }

  const auto currentOtaDefaults = config.common;
  const bool configured = loadStoredRuntimeConfig(
      config, kPreviousConfigNamespace, kPreviousHardwareNamespace, true);
  // Preserve identity/network/calibration, not old firmware release targets.
  config.common.otaChannel = currentOtaDefaults.otaChannel;
  config.common.otaPublicManifestUrl = currentOtaDefaults.otaPublicManifestUrl;
  config.common.otaLocalMirrorUrl = "";
  config.common.otaDesiredBuild = 0;
  config.common.otaAutoCheckEnabled = false;
  config.common.otaCommandCenterControlled = true;
  config.common.otaApplyOnlyInSafeState = true;

  // An interrupted import may have left partial destination data. Only these
  // new namespaces are cleared; the source remains intact for retry/rollback.
  if (battlebang::esp::nvs::clearNamespace(kConfigNamespace) &&
      battlebang::esp::nvs::clearNamespace(kHardwareNamespace)) {
    saveRuntimeConfigToNvs(config);  // failure leaves marker absent; retry next boot
  }
  return configured;
}

bool saveRuntimeConfigToNvs(const RuntimeConfig& config) {
  RuntimeConfig normalized = config;
  normalizeRuntimeConfig(normalized);

  bool ok = false;
  {
    battlebang::esp::nvs::ScopedPreferences prefs;
    if (!prefs.begin(kConfigNamespace, false)) return false;
    ok = battlebang::esp::nvs::saveCommonRuntimeConfig(
        prefs.preferences(), normalized.common, commonNvsKeys());
    ok &= saveHitRuntimeConfigToNvs(prefs, normalized.hit);
    ok &= saveBlasterRuntimeConfigToNvs(prefs, normalized.blaster);
  }
  if (!ok) return false;
  battlebang::esp::nvs::ScopedPreferences hardware;
  if (!hardware.begin(kHardwareNamespace, false)) return false;
  ok &= hardware.preferences().putUChar("piezo_mask", normalized.hit.piezoChannelEnableMask) > 0;
  ok &= hardware.preferences().putUChar("bar_groups", normalized.hit.hpBarGroupCount) > 0;
  ok &= hardware.preferences().putUChar("bar_color", normalized.hit.hpBarColorOrder) > 0;
  hardware.end();
  if (!ok) return false;

  RuntimeConfig verified = runtimeConfigFromBuild();
  loadStoredRuntimeConfig(verified, kConfigNamespace, kHardwareNamespace, false);
  const String expectedJson = runtimeConfigToJson(normalized, true);
  const String verifiedJson = runtimeConfigToJson(verified, true);
  if (expectedJson.length() == 0 || verifiedJson.length() == 0 || verifiedJson != expectedJson) return false;
  if (!hardware.begin(kHardwareNamespace, false)) return false;
  // Commit only after both stores have been fully written and read back.
  return hardware.preferences().putBool(kMigrationCompleteKey, true) > 0;
}

bool clearRuntimeConfigNvs() {
  const RuntimeConfig current = runtimeConfigFromNvsOrBuild();
  battlebang::esp::nvs::ScopedPreferences hardware;
  if (!hardware.begin(kHardwareNamespace, false)) return false;
  if (hardware.preferences().putUChar("piezo_mask", current.hit.piezoChannelEnableMask) == 0 ||
      hardware.preferences().putUChar("bar_groups", current.hit.hpBarGroupCount) == 0 ||
      hardware.preferences().putUChar("bar_color", current.hit.hpBarColorOrder) == 0 ||
      hardware.preferences().putBool(kMigrationCompleteKey, true) == 0) return false;
  hardware.end();
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

  // Input-only migration aliases. Serialization and NVS writes use blaster.
  JsonObject mutableRoot = doc.as<JsonObject>();
  JsonObjectConst previous = mutableRoot["nixo"].as<JsonObjectConst>();
  JsonObject canonical = mutableRoot["blaster"].as<JsonObject>();
  if (!previous.isNull() && canonical.isNull()) canonical = mutableRoot.createNestedObject("blaster");
  const char* suffixes[] = {"id", "command_topic_prefix", "fire_default_duration_ms",
                           "fire_min_duration_ms", "fire_max_duration_ms",
                           "fire_cooldown_ms", "prefire_delay_ms", "relay_delay1_ms"};
  const char* shortKeys[] = {"id", "command_topic_prefix", "default_duration_ms",
                            "min_duration_ms", "max_duration_ms", "cooldown_ms",
                            "prefire_delay_ms", "relay_delay1_ms"};
  for (size_t index = 0; index < sizeof(suffixes) / sizeof(suffixes[0]); ++index) {
    const char* suffix = suffixes[index];
    const String newKey = String("blaster_") + suffix;
    const String oldKey = String("nixo_") + suffix;
    // Any canonical root or nested spelling overrides every previous spelling.
    if (!mutableRoot[newKey].isNull() ||
        (!canonical.isNull() && (!canonical[suffix].isNull() || !canonical[newKey].isNull() ||
                                !canonical[shortKeys[index]].isNull()))) continue;
    JsonVariantConst value = previous[suffix];
    if (value.isNull()) value = previous[shortKeys[index]];
    if (value.isNull()) value = previous[oldKey];
    if (value.isNull()) value = mutableRoot[oldKey];
    if (!value.isNull()) mutableRoot[newKey].set(value);
  }
  if (doc.overflowed()) {
    error = "config JSON exceeds capacity";
    return false;
  }
  root = doc.as<JsonObjectConst>();

  RuntimeConfig next = config;
  if (!battlebang::esp::config::applyCommonRuntimeConfigJson(root, next.common, error)) return false;

  battlebang::esp::config::readStringField(root, "robot_id", next.hit.robotId);
  battlebang::esp::config::readStringField(root, "hit_topic_prefix", next.hit.hitTopicPrefix);
  battlebang::esp::config::readStringField(root, "blaster_id", next.blaster.blasterId);
  battlebang::esp::config::readStringField(root, "blaster_command_topic_prefix", next.blaster.commandTopicPrefix);
  readHitTuningJson(root, next.hit);
  readBlasterTuningJson(root, next.blaster);

  JsonObjectConst hit = root["hit"].as<JsonObjectConst>();
  if (!hit.isNull()) {
    battlebang::esp::config::readStringField(hit, "robot_id", next.hit.robotId);
    battlebang::esp::config::readStringField(hit, "topic_prefix", next.hit.hitTopicPrefix);
    battlebang::esp::config::readStringField(hit, "hit_topic_prefix", next.hit.hitTopicPrefix);
    battlebang::esp::config::readUInt32Field(hit, "cooldown_ms", next.hit.hitCooldownMs);
    readHitTuningJson(hit, next.hit);
  }

  JsonObjectConst blaster = root["blaster"].as<JsonObjectConst>();
  if (!blaster.isNull()) {
    battlebang::esp::config::readStringField(blaster, "id", next.blaster.blasterId);
    battlebang::esp::config::readStringField(blaster, "blaster_id", next.blaster.blasterId);
    battlebang::esp::config::readStringField(blaster, "command_topic_prefix", next.blaster.commandTopicPrefix);
    readBlasterTuningJson(blaster, next.blaster);
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
  writeBlasterRuntimeConfigJson(root, config.blaster);
  // Empty means serialization failed; migration read-back must never compare
  // two truncated documents and mark an incomplete store committed.
  if (doc.overflowed()) return String();

  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace go2
