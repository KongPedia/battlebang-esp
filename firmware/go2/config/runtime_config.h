#pragma once

#include <Arduino.h>

#include <bb_esp_core/config/common_runtime_config.h>

#include "go2/build_config.h"

namespace go2 {

struct HitRuntimeConfig {
  String robotId;
  String hitTopicPrefix;
  uint32_t hitCooldownMs = HIT_COOLDOWN_MS;
  uint16_t offlineQueueCapacity = OFFLINE_HIT_QUEUE_CAPACITY;
  uint32_t offlineQueueFlushIntervalMs = OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS;
  uint16_t ledBrightness = HP_BAR_LED_BRIGHTNESS;
  uint16_t piezoAoThresholdRaw = PIEZO_AO_THRESHOLD_RAW;
  uint16_t piezoAoRearmRaw = PIEZO_AO_REARM_RAW;
  uint32_t piezoAoCaptureWindowMs = PIEZO_AO_CAPTURE_WINDOW_MS;
  uint32_t piezoAoDebugPeriodMs = PIEZO_AO_DEBUG_PERIOD_MS;
  uint32_t piezoAoRearmStableMs = HIT_REARM_STABLE_MS;
};

struct RuntimeConfig {
  battlebang::esp::config::CommonRuntimeConfig common;
  HitRuntimeConfig hit;
};

RuntimeConfig runtimeConfigFromBuild();
RuntimeConfig runtimeConfigFromNvsOrBuild();
bool loadRuntimeConfigFromNvs(RuntimeConfig& config);
bool saveRuntimeConfigToNvs(const RuntimeConfig& config);
bool clearRuntimeConfigNvs();
bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error);
String runtimeConfigToJson(const RuntimeConfig& config, bool includeSecrets = false);

}  // namespace go2
