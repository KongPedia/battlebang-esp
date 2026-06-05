#include "runtime_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "hit_target/app/firmware_info.h"
#include "hit_target/build_config.h"

namespace battlebang {
namespace hit_target {
namespace {
constexpr const char* kNamespace = "bb_hit_target";
constexpr size_t kRuntimeConfigJsonCapacity = 6144;
constexpr uint16_t kMaxTotalHits = 200;

String getStringOr(JsonVariantConst v, const String& fallback) {
  if (v.isNull()) return fallback;
  if (v.is<const char*>()) return String(v.as<const char*>());
  if (v.is<String>()) return v.as<String>();
  return fallback;
}

bool getBoolOr(JsonVariantConst v, bool fallback) {
  if (v.isNull()) return fallback;
  return v.as<bool>();
}

uint32_t getUIntOr(JsonVariantConst v, uint32_t fallback) {
  if (v.isNull()) return fallback;
  if (v.is<uint32_t>()) return v.as<uint32_t>();
  if (v.is<int>() && v.as<int>() >= 0) return static_cast<uint32_t>(v.as<int>());
  return fallback;
}

int getIntOr(JsonVariantConst v, int fallback) {
  if (v.isNull()) return fallback;
  if (v.is<int>()) return v.as<int>();
  return fallback;
}

String colorToHex(uint32_t rgb) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06lX", static_cast<unsigned long>(rgb & 0xFFFFFFUL));
  return String(buf);
}

bool parseHexColor(const String& input, uint32_t& color) {
  String s = input;
  s.trim();
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.startsWith("0x") || s.startsWith("0X")) s.remove(0, 2);
  if (s.length() != 6) return false;
  uint32_t value = 0;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s.charAt(i);
    value <<= 4;
    if (c >= '0' && c <= '9') {
      value |= static_cast<uint8_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= static_cast<uint8_t>(10 + c - 'a');
    } else if (c >= 'A' && c <= 'F') {
      value |= static_cast<uint8_t>(10 + c - 'A');
    } else {
      return false;
    }
  }
  color = value & 0xFFFFFFUL;
  return true;
}

uint32_t defaultPhaseColor(uint8_t phaseIndex, uint8_t phaseCount) {
  if (phaseCount <= 1) return 0x009600;
  if (phaseIndex == 0) return 0x009600;
  if (phaseIndex >= phaseCount - 1) return 0xBE0000;
  if (phaseCount == 3) return 0xBE8200;

  // Approximate green -> red through hue-like RGB interpolation for extra phases.
  const float t = static_cast<float>(phaseIndex) / static_cast<float>(phaseCount - 1);
  const uint8_t r = static_cast<uint8_t>(190.0f * t);
  const uint8_t g = static_cast<uint8_t>(150.0f * (1.0f - t) + 60.0f * (t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f));
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8);
}

void normalizePalette(RuntimeConfig& config) {
  const uint8_t phases = activePhaseCount(config);
  for (uint8_t i = 0; i < phases; ++i) {
    if ((config.hp.palette[i] & 0xFFFFFFUL) == 0) {
      config.hp.palette[i] = defaultPhaseColor(i, phases);
    }
  }
}

bool validateConfig(RuntimeConfig& config, String& error) {
  if (config.schema == 0) {
    error = "schema must be positive";
    return false;
  }
  if (config.deviceId.length() == 0) {
    error = "device_id is required";
    return false;
  }
  if (config.targetId.length() == 0) {
    config.targetId = config.deviceId;
  }
  if (config.targetId.length() > 48) {
    error = "target_id too long";
    return false;
  }
  if (config.hp.phaseCount < 1 || config.hp.phaseCount > kMaxHpPhases) {
    error = "hp.phase_count must be 1..8";
    return false;
  }
  if (config.hp.hitsPerPhase < 1 || config.hp.hitsPerPhase > 50) {
    error = "hp.hits_per_phase must be 1..50";
    return false;
  }
  if (totalHits(config) < 1 || totalHits(config) > kMaxTotalHits) {
    error = "total HP hits must be 1..200";
    return false;
  }
  if (config.led.numLeds < 1 || config.led.numLeds > ::hit_target::NUM_LEDS) {
    error = String("led.num_leds must be 1..") + ::hit_target::NUM_LEDS;
    return false;
  }
  if (config.led.pin != ::hit_target::LED_PIN ||
      config.led.ledType != ::hit_target::LED_TYPE_NAME ||
      config.led.colorOrder != ::hit_target::COLOR_ORDER_NAME) {
    error = "led pin/type/color_order are hardware-profile build values";
    return false;
  }
  if (config.led.brightness < 1) {
    error = "led.brightness must be positive";
    return false;
  }
  if (config.led.maxMa < 100 || config.led.maxMa > 6000) {
    error = "led.max_ma must be 100..6000";
    return false;
  }
  if (config.sensor.piezoDoPin < 0 && config.sensor.piezoAoPin < 0) {
    error = "at least one piezo input pin is required";
    return false;
  }
  if (config.sensor.hitThreshold <= config.sensor.hitRearmThreshold) {
    error = "sensor.hit_threshold must be above hit_rearm_threshold";
    return false;
  }
  if (config.sensor.hitThreshold > 4095 || config.sensor.hitRearmThreshold > 4095) {
    error = "sensor thresholds must fit ESP32 ADC range";
    return false;
  }
  if (config.sensor.hitCooldownMs < 20 || config.sensor.hitCooldownMs > 3000) {
    error = "sensor.hit_cooldown_ms must be 20..3000";
    return false;
  }
  if (config.sensor.hitRearmStableMs < 10 || config.sensor.hitRearmStableMs > 3000) {
    error = "sensor.hit_rearm_stable_ms must be 10..3000";
    return false;
  }
  if (config.sensor.hitRearmCheckMs < 1 || config.sensor.hitRearmCheckMs > 250) {
    error = "sensor.hit_rearm_check_ms must be 1..250";
    return false;
  }
  if (config.sensor.digitalHitMinEdges < 1 || config.sensor.digitalHitMinEdges > 20) {
    error = "sensor.digital_hit_min_edges must be 1..20";
    return false;
  }
  if (config.sensor.digitalIsrDebounceUs < 500 || config.sensor.digitalIsrDebounceUs > 50000) {
    error = "sensor.digital_isr_debounce_us must be 500..50000";
    return false;
  }
  if (config.sensor.captureWindowMs < 10 || config.sensor.captureWindowMs > 500) {
    error = "sensor.capture_window_ms must be 10..500";
    return false;
  }
  if (config.visual.orbitStepMs < 1 || config.visual.orbitStepMs > 1000) {
    error = "visual.orbit_step_ms must be 1..1000";
    return false;
  }
  if (config.visual.orbitTailLeds >= config.led.numLeds) {
    config.visual.orbitTailLeds = config.led.numLeds > 0 ? config.led.numLeds - 1 : 0;
  }
  if (config.visual.hitFlashMs < 10 || config.visual.hitFlashMs > 250) {
    error = "visual.hit_flash_ms must be 10..250";
    return false;
  }
  if (config.visual.damageChipMs < 50 || config.visual.damageChipMs > 2000) {
    error = "visual.damage_chip_ms must be 50..2000";
    return false;
  }
  if (config.visual.phaseBackfillScale < 1) {
    error = "visual.phase_backfill_scale must be positive";
    return false;
  }
  if (config.visual.hpHitPulseMs < 10 || config.visual.hpHitPulseMs > 1000) {
    error = "visual.hp_hit_pulse_ms must be 10..1000";
    return false;
  }
  if (config.visual.defeatBlackoutMs > 1000) {
    error = "visual.defeat_blackout_ms must be <=1000";
    return false;
  }
  if (config.visual.defeatRainbowMs < 100 || config.visual.defeatRainbowMs > 5000) {
    error = "visual.defeat_rainbow_ms must be 100..5000";
    return false;
  }
  if (config.visual.defeatRainbowSpins < 1 || config.visual.defeatRainbowSpins > 12) {
    error = "visual.defeat_rainbow_spins must be 1..12";
    return false;
  }
  if (config.reset.buttonHoldMs < 100 || config.reset.buttonHoldMs > 10000) {
    error = "reset.button_hold_ms must be 100..10000";
    return false;
  }
  if (config.mqttPort == 0) {
    error = "mqtt.port must be positive";
    return false;
  }
  if (config.otaCheckIntervalS < 30) config.otaCheckIntervalS = 30;
  normalizePalette(config);
  error = "";
  return true;
}

void applyPalette(JsonVariantConst variant, RuntimeConfig& next, String& error) {
  if (variant.isNull() || error.length() > 0) return;
  JsonArrayConst arr = variant.as<JsonArrayConst>();
  if (arr.isNull()) {
    error = "hp.palette must be an array";
    return;
  }
  uint8_t index = 0;
  for (JsonVariantConst item : arr) {
    if (index >= kMaxHpPhases) break;
    uint32_t color = 0;
    if (item.is<const char*>()) {
      if (!parseHexColor(String(item.as<const char*>()), color)) {
        error = "hp.palette contains invalid color";
        return;
      }
    } else if (item.is<uint32_t>()) {
      color = item.as<uint32_t>() & 0xFFFFFFUL;
    } else {
      error = "hp.palette values must be hex strings or integers";
      return;
    }
    next.hp.palette[index++] = color;
  }
}

void serializePalette(const RuntimeConfig& config, JsonArray arr) {
  const uint8_t phases = activePhaseCount(config);
  for (uint8_t i = 0; i < phases; ++i) {
    arr.add(colorToHex(phaseColorRgb(config, i)));
  }
}

}  // namespace

RuntimeConfig makeDefaultRuntimeConfig(const DeviceIdentity& identity) {
  RuntimeConfig config;
  config.deviceId = identity.deviceId;
  config.targetId = identity.targetId;
  config.deviceMac = identity.mac;
  config.hp.phaseCount = ::hit_target::HP_PHASE_COUNT;
  config.hp.hitsPerPhase = ::hit_target::HITS_PER_PHASE;
  for (uint8_t i = 0; i < kMaxHpPhases; ++i) config.hp.palette[i] = 0;
  normalizePalette(config);
  config.visual.orbitStepMs = ::hit_target::ORBIT_STEP_MS;
  config.visual.orbitTailLeds = ::hit_target::ORBIT_TAIL_LEDS;
  config.visual.hitFlashMs = ::hit_target::HIT_FLASH_MS;
  config.visual.damageChipMs = ::hit_target::DAMAGE_CHIP_MS;
  config.visual.phaseBackfillGapLeds = ::hit_target::PHASE_BACKFILL_GAP_LEDS;
  config.visual.phaseBackfillScale = ::hit_target::PHASE_BACKFILL_SCALE;
  config.visual.hpHitPulseMs = ::hit_target::HP_HIT_PULSE_MS;
  config.visual.defeatBlackoutMs = ::hit_target::DEFEAT_BLACKOUT_MS;
  config.visual.defeatRainbowMs = ::hit_target::DEFEAT_RAINBOW_MS;
  config.visual.defeatRainbowSpins = ::hit_target::DEFEAT_RAINBOW_SPINS;
  config.sensor.piezoDoPin = ::hit_target::PIEZO_DO_PIN;
  config.sensor.piezoAoPin = ::hit_target::PIEZO_AO_PIN;
  config.sensor.hitThreshold = ::hit_target::HIT_THRESHOLD;
  config.sensor.hitRearmThreshold = ::hit_target::HIT_REARM_THRESHOLD;
  config.sensor.hitCooldownMs = ::hit_target::HIT_COOLDOWN_MS;
  config.sensor.hitRearmStableMs = ::hit_target::HIT_REARM_STABLE_MS;
  config.sensor.hitRearmCheckMs = ::hit_target::HIT_REARM_CHECK_MS;
  config.sensor.digitalHitMinEdges = ::hit_target::DIGITAL_HIT_MIN_EDGES;
  config.sensor.digitalIsrDebounceUs = ::hit_target::DIGITAL_ISR_DEBOUNCE_US;
  config.sensor.captureWindowMs = ::hit_target::CAPTURE_WINDOW_MS;
  config.led.pin = ::hit_target::LED_PIN;
  config.led.numLeds = ::hit_target::NUM_LEDS;
  config.led.ledType = ::hit_target::LED_TYPE_NAME;
  config.led.colorOrder = ::hit_target::COLOR_ORDER_NAME;
  config.led.brightness = ::hit_target::LED_BRIGHTNESS;
  config.led.maxMa = ::hit_target::LED_MAX_MA;
  config.reset.buttonPin = ::hit_target::RESET_BUTTON_PIN;
  config.reset.buttonHoldMs = ::hit_target::RESET_BUTTON_HOLD_MS;
  config.otaPublicManifestUrl = BB_HIT_TARGET_LATEST_MANIFEST_URL;
  return config;
}

uint16_t totalHits(const RuntimeConfig& config) {
  return static_cast<uint16_t>(config.hp.phaseCount) * config.hp.hitsPerPhase;
}

uint8_t activePhaseCount(const RuntimeConfig& config) {
  return constrain(config.hp.phaseCount, static_cast<uint8_t>(1), static_cast<uint8_t>(kMaxHpPhases));
}

uint32_t phaseColorRgb(const RuntimeConfig& config, uint8_t phaseIndex) {
  const uint8_t phases = activePhaseCount(config);
  phaseIndex = constrain(phaseIndex, static_cast<uint8_t>(0), static_cast<uint8_t>(phases - 1));
  uint32_t color = config.hp.palette[phaseIndex] & 0xFFFFFFUL;
  if (color == 0) color = defaultPhaseColor(phaseIndex, phases);
  return color;
}

bool applyRuntimeConfigJson(const char* json, RuntimeConfig& config, String& error) {
  DynamicJsonDocument doc(kRuntimeConfigJsonCapacity);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid json: ") + err.c_str();
    return false;
  }

  const char* type = doc["type"] | "config";
  if (strcmp(type, "config") != 0 && strcmp(type, "provision") != 0) {
    error = String("unsupported type: ") + type;
    return false;
  }

  const uint32_t incomingVersion = doc["config_version"] | config.configVersion;
  if (incomingVersion < config.configVersion) {
    error = "stale config_version";
    return false;
  }

  RuntimeConfig next = config;
  next.schema = doc["schema"] | next.schema;
  next.configVersion = incomingVersion;
  next.configured = doc["configured"] | next.configured;
  if (strcmp(type, "provision") == 0) next.configured = true;
  next.targetId = getStringOr(doc["target_id"], next.targetId);
  next.group = getStringOr(doc["group"], next.group);
  next.location = getStringOr(doc["location"], next.location);
  next.debugAllowSimulateHit = getBoolOr(doc["debug_allow_simulate_hit"], next.debugAllowSimulateHit);

  JsonObjectConst hp = doc["hp"].as<JsonObjectConst>();
  if (!hp.isNull()) {
    next.hp.phaseCount = static_cast<uint8_t>(getUIntOr(hp["phase_count"], next.hp.phaseCount));
    next.hp.hitsPerPhase = static_cast<uint16_t>(getUIntOr(hp["hits_per_phase"], next.hp.hitsPerPhase));
    applyPalette(hp["palette"], next, error);
    if (error.length() > 0) return false;
  }

  JsonObjectConst visual = doc["visual"].as<JsonObjectConst>();
  if (!visual.isNull()) {
    next.visual.orbitStepMs = getUIntOr(visual["orbit_step_ms"], next.visual.orbitStepMs);
    next.visual.orbitTailLeds = static_cast<uint8_t>(getUIntOr(visual["orbit_tail_leds"], next.visual.orbitTailLeds));
    next.visual.hitFlashMs = getUIntOr(visual["hit_flash_ms"], next.visual.hitFlashMs);
    next.visual.damageChipMs = getUIntOr(visual["damage_chip_ms"], next.visual.damageChipMs);
    next.visual.phaseBackfillGapLeds = static_cast<uint8_t>(getUIntOr(visual["phase_backfill_gap_leds"], next.visual.phaseBackfillGapLeds));
    next.visual.phaseBackfillScale = static_cast<uint8_t>(getUIntOr(visual["phase_backfill_scale"], next.visual.phaseBackfillScale));
    next.visual.hpHitPulseMs = getUIntOr(visual["hp_hit_pulse_ms"], next.visual.hpHitPulseMs);
    next.visual.defeatBlackoutMs = getUIntOr(visual["defeat_blackout_ms"], next.visual.defeatBlackoutMs);
    next.visual.defeatRainbowMs = getUIntOr(visual["defeat_rainbow_ms"], next.visual.defeatRainbowMs);
    next.visual.defeatRainbowSpins = static_cast<uint8_t>(getUIntOr(visual["defeat_rainbow_spins"], next.visual.defeatRainbowSpins));
  }

  JsonObjectConst sensor = doc["sensor"].as<JsonObjectConst>();
  if (!sensor.isNull()) {
    next.sensor.piezoDoPin = static_cast<int8_t>(getIntOr(sensor["piezo_do_pin"], next.sensor.piezoDoPin));
    next.sensor.piezoAoPin = static_cast<int8_t>(getIntOr(sensor["piezo_ao_pin"], next.sensor.piezoAoPin));
    next.sensor.hitThreshold = static_cast<uint16_t>(getUIntOr(sensor["hit_threshold"], next.sensor.hitThreshold));
    next.sensor.hitRearmThreshold = static_cast<uint16_t>(getUIntOr(sensor["hit_rearm_threshold"], next.sensor.hitRearmThreshold));
    next.sensor.hitCooldownMs = getUIntOr(sensor["hit_cooldown_ms"], next.sensor.hitCooldownMs);
    next.sensor.hitRearmStableMs = getUIntOr(sensor["hit_rearm_stable_ms"], next.sensor.hitRearmStableMs);
    next.sensor.hitRearmCheckMs = getUIntOr(sensor["hit_rearm_check_ms"], next.sensor.hitRearmCheckMs);
    next.sensor.digitalHitMinEdges = static_cast<uint16_t>(getUIntOr(sensor["digital_hit_min_edges"], next.sensor.digitalHitMinEdges));
    next.sensor.digitalIsrDebounceUs = getUIntOr(sensor["digital_isr_debounce_us"], next.sensor.digitalIsrDebounceUs);
    next.sensor.captureWindowMs = getUIntOr(sensor["capture_window_ms"], next.sensor.captureWindowMs);
  }

  JsonObjectConst led = doc["led"].as<JsonObjectConst>();
  if (!led.isNull()) {
    next.led.pin = static_cast<int8_t>(getIntOr(led["pin"], next.led.pin));
    next.led.numLeds = static_cast<uint16_t>(getUIntOr(led["num_leds"], next.led.numLeds));
    next.led.ledType = getStringOr(led["type"], next.led.ledType);
    next.led.colorOrder = getStringOr(led["color_order"], next.led.colorOrder);
    next.led.brightness = static_cast<uint8_t>(getUIntOr(led["brightness"], next.led.brightness));
    next.led.maxMa = static_cast<uint16_t>(getUIntOr(led["max_ma"], next.led.maxMa));
  }

  JsonObjectConst reset = doc["reset"].as<JsonObjectConst>();
  if (!reset.isNull()) {
    next.reset.buttonPin = static_cast<int8_t>(getIntOr(reset["button_pin"], next.reset.buttonPin));
    next.reset.buttonHoldMs = getUIntOr(reset["button_hold_ms"], next.reset.buttonHoldMs);
  }

  JsonObjectConst wifi = doc["wifi"].as<JsonObjectConst>();
  if (!wifi.isNull()) {
    next.wifiSsid = getStringOr(wifi["ssid"], next.wifiSsid);
    next.wifiPassword = getStringOr(wifi["password"], next.wifiPassword);
  }

  JsonObjectConst network = doc["network"].as<JsonObjectConst>();
  if (!network.isNull()) {
    next.networkAutoStart = getBoolOr(network["auto_start"], next.networkAutoStart);
    next.networkStartDelayMs = getUIntOr(network["start_delay_ms"], next.networkStartDelayMs);
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
  DynamicJsonDocument doc(kRuntimeConfigJsonCapacity);
  doc["type"] = "config";
  doc["schema"] = config.schema;
  doc["config_version"] = config.configVersion;
  doc["configured"] = config.configured;
  doc["device_id"] = config.deviceId;
  doc["target_id"] = config.targetId;
  doc["device_mac"] = config.deviceMac;
  doc["group"] = config.group;
  doc["location"] = config.location;
  doc["debug_allow_simulate_hit"] = config.debugAllowSimulateHit;

  JsonObject hp = doc.createNestedObject("hp");
  hp["phase_count"] = config.hp.phaseCount;
  hp["hits_per_phase"] = config.hp.hitsPerPhase;
  hp["total_hits"] = totalHits(config);
  serializePalette(config, hp.createNestedArray("palette"));

  JsonObject visual = doc.createNestedObject("visual");
  visual["orbit_step_ms"] = config.visual.orbitStepMs;
  visual["orbit_tail_leds"] = config.visual.orbitTailLeds;
  visual["hit_flash_ms"] = config.visual.hitFlashMs;
  visual["damage_chip_ms"] = config.visual.damageChipMs;
  visual["phase_backfill_gap_leds"] = config.visual.phaseBackfillGapLeds;
  visual["phase_backfill_scale"] = config.visual.phaseBackfillScale;
  visual["hp_hit_pulse_ms"] = config.visual.hpHitPulseMs;
  visual["defeat_blackout_ms"] = config.visual.defeatBlackoutMs;
  visual["defeat_rainbow_ms"] = config.visual.defeatRainbowMs;
  visual["defeat_rainbow_spins"] = config.visual.defeatRainbowSpins;

  JsonObject sensor = doc.createNestedObject("sensor");
  sensor["piezo_do_pin"] = config.sensor.piezoDoPin;
  sensor["piezo_ao_pin"] = config.sensor.piezoAoPin;
  sensor["hit_threshold"] = config.sensor.hitThreshold;
  sensor["hit_rearm_threshold"] = config.sensor.hitRearmThreshold;
  sensor["hit_cooldown_ms"] = config.sensor.hitCooldownMs;
  sensor["hit_rearm_stable_ms"] = config.sensor.hitRearmStableMs;
  sensor["hit_rearm_check_ms"] = config.sensor.hitRearmCheckMs;
  sensor["digital_hit_min_edges"] = config.sensor.digitalHitMinEdges;
  sensor["digital_isr_debounce_us"] = config.sensor.digitalIsrDebounceUs;
  sensor["capture_window_ms"] = config.sensor.captureWindowMs;

  JsonObject led = doc.createNestedObject("led");
  led["pin"] = config.led.pin;
  led["num_leds"] = config.led.numLeds;
  led["capacity_leds"] = ::hit_target::NUM_LEDS;
  led["type"] = config.led.ledType;
  led["color_order"] = config.led.colorOrder;
  led["brightness"] = config.led.brightness;
  led["max_ma"] = config.led.maxMa;

  JsonObject reset = doc.createNestedObject("reset");
  reset["button_pin"] = config.reset.buttonPin;
  reset["button_hold_ms"] = config.reset.buttonHoldMs;

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = config.wifiSsid;
  wifi["password"] = includeSecrets ? config.wifiPassword : "***";

  JsonObject network = doc.createNestedObject("network");
  network["auto_start"] = config.networkAutoStart;
  network["start_delay_ms"] = config.networkStartDelayMs;

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = config.mqttHost;
  mqtt["port"] = config.mqttPort;
  mqtt["username"] = config.mqttUsername;
  mqtt["password"] = includeSecrets ? config.mqttPassword : "***";
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
  if (before.hp.phaseCount != after.hp.phaseCount || before.hp.hitsPerPhase != after.hp.hitsPerPhase) return true;
  if (before.led.numLeds != after.led.numLeds) return true;
  for (uint8_t i = 0; i < kMaxHpPhases; ++i) {
    if ((before.hp.palette[i] & 0xFFFFFFUL) != (after.hp.palette[i] & 0xFFFFFFUL)) return true;
  }
  return false;
}

bool sensorPinsChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.sensor.piezoDoPin != after.sensor.piezoDoPin ||
         before.sensor.piezoAoPin != after.sensor.piezoAoPin ||
         before.reset.buttonPin != after.reset.buttonPin;
}

bool ledHardwareChanged(const RuntimeConfig& before, const RuntimeConfig& after) {
  return before.led.pin != after.led.pin ||
         before.led.ledType != after.led.ledType ||
         before.led.colorOrder != after.led.colorOrder;
}

bool RuntimeConfigStore::load(RuntimeConfig& config) {
  RuntimeConfig fallback = config;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  config.schema = prefs.getUShort("schema", config.schema);
  config.configVersion = prefs.getUInt("cfg_ver", config.configVersion);
  config.configured = prefs.getBool("configured", config.configured);
  config.targetId = prefs.getString("target_id", config.targetId);
  config.group = prefs.getString("group", config.group);
  config.location = prefs.getString("location", config.location);
  config.debugAllowSimulateHit = prefs.getBool("sim_hit", config.debugAllowSimulateHit);
  config.hp.phaseCount = prefs.getUChar("hp_phases", config.hp.phaseCount);
  config.hp.hitsPerPhase = prefs.getUShort("hp_per", config.hp.hitsPerPhase);
  for (uint8_t i = 0; i < kMaxHpPhases; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "pal%u", i);
    config.hp.palette[i] = prefs.getUInt(key, config.hp.palette[i]);
  }
  config.visual.orbitStepMs = prefs.getUInt("orb_step", config.visual.orbitStepMs);
  config.visual.orbitTailLeds = prefs.getUChar("orb_tail", config.visual.orbitTailLeds);
  config.visual.hitFlashMs = prefs.getUInt("hit_flash", config.visual.hitFlashMs);
  config.visual.damageChipMs = prefs.getUInt("dmg_chip", config.visual.damageChipMs);
  config.visual.phaseBackfillGapLeds = prefs.getUChar("back_gap", config.visual.phaseBackfillGapLeds);
  config.visual.phaseBackfillScale = prefs.getUChar("back_scl", config.visual.phaseBackfillScale);
  config.visual.hpHitPulseMs = prefs.getUInt("hp_pulse", config.visual.hpHitPulseMs);
  config.visual.defeatBlackoutMs = prefs.getUInt("def_blk", config.visual.defeatBlackoutMs);
  config.visual.defeatRainbowMs = prefs.getUInt("def_rain", config.visual.defeatRainbowMs);
  config.visual.defeatRainbowSpins = prefs.getUChar("def_spin", config.visual.defeatRainbowSpins);
  config.sensor.piezoDoPin = prefs.getChar("piezo_do", config.sensor.piezoDoPin);
  config.sensor.piezoAoPin = prefs.getChar("piezo_ao", config.sensor.piezoAoPin);
  config.sensor.hitThreshold = prefs.getUShort("hit_thr", config.sensor.hitThreshold);
  config.sensor.hitRearmThreshold = prefs.getUShort("rearm_thr", config.sensor.hitRearmThreshold);
  config.sensor.hitCooldownMs = prefs.getUInt("cool_ms", config.sensor.hitCooldownMs);
  config.sensor.hitRearmStableMs = prefs.getUInt("rearm_st", config.sensor.hitRearmStableMs);
  config.sensor.hitRearmCheckMs = prefs.getUInt("rearm_chk", config.sensor.hitRearmCheckMs);
  config.sensor.digitalHitMinEdges = prefs.getUShort("dig_edges", config.sensor.digitalHitMinEdges);
  config.sensor.digitalIsrDebounceUs = prefs.getUInt("dig_db_us", config.sensor.digitalIsrDebounceUs);
  config.sensor.captureWindowMs = prefs.getUInt("cap_win", config.sensor.captureWindowMs);
  config.led.pin = prefs.getChar("led_pin", config.led.pin);
  config.led.numLeds = prefs.getUShort("num_leds", config.led.numLeds);
  config.led.ledType = prefs.getString("led_type", config.led.ledType);
  config.led.colorOrder = prefs.getString("color_ord", config.led.colorOrder);
  config.led.brightness = prefs.getUChar("bright", config.led.brightness);
  config.led.maxMa = prefs.getUShort("max_ma", config.led.maxMa);
  config.reset.buttonPin = prefs.getChar("rst_pin", config.reset.buttonPin);
  config.reset.buttonHoldMs = prefs.getUInt("rst_hold", config.reset.buttonHoldMs);
  config.wifiSsid = prefs.getString("wifi_ssid", config.wifiSsid);
  config.wifiPassword = prefs.getString("wifi_pass", config.wifiPassword);
  config.networkAutoStart = prefs.getBool("net_auto", config.networkAutoStart);
  config.networkStartDelayMs = prefs.getUInt("net_delay", config.networkStartDelayMs);
  config.mqttHost = prefs.getString("mqtt_host", config.mqttHost);
  config.mqttPort = prefs.getUShort("mqtt_port", config.mqttPort);
  config.mqttUsername = prefs.getString("mqtt_user", config.mqttUsername);
  config.mqttPassword = prefs.getString("mqtt_pass", config.mqttPassword);
  config.mqttRoot = prefs.getString("mqtt_root", config.mqttRoot);
  config.otaCommandCenterControlled = prefs.getBool("ota_cc", config.otaCommandCenterControlled);
  config.otaAutoCheckEnabled = prefs.getBool("ota_auto", config.otaAutoCheckEnabled);
  config.otaChannel = prefs.getString("ota_channel", config.otaChannel);
  config.otaDesiredBuild = prefs.getUInt("ota_build", config.otaDesiredBuild);
  config.otaPublicManifestUrl = prefs.getString("ota_pub_url", config.otaPublicManifestUrl);
  config.otaLocalMirrorUrl = prefs.getString("ota_mir_url", config.otaLocalMirrorUrl);
  config.otaCheckIntervalS = prefs.getUInt("ota_int_s", config.otaCheckIntervalS);
  config.otaApplyOnlyInSafeState = prefs.getBool("ota_safe", config.otaApplyOnlyInSafeState);
  prefs.end();

  String error;
  if (!validateConfig(config, error)) {
    Serial.print("[hit_target][config] NVS config invalid, using safe defaults: ");
    Serial.println(error);
    config = fallback;
    return false;
  }
  return true;
}

bool RuntimeConfigStore::save(const RuntimeConfig& config) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = true;
  ok &= prefs.putUShort("schema", config.schema) > 0;
  ok &= prefs.putUInt("cfg_ver", config.configVersion) > 0;
  ok &= prefs.putBool("configured", config.configured) > 0;
  ok &= prefs.putString("target_id", config.targetId) >= 0;
  ok &= prefs.putString("group", config.group) >= 0;
  ok &= prefs.putString("location", config.location) >= 0;
  ok &= prefs.putBool("sim_hit", config.debugAllowSimulateHit) > 0;
  ok &= prefs.putUChar("hp_phases", config.hp.phaseCount) > 0;
  ok &= prefs.putUShort("hp_per", config.hp.hitsPerPhase) > 0;
  for (uint8_t i = 0; i < kMaxHpPhases; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "pal%u", i);
    ok &= prefs.putUInt(key, config.hp.palette[i]) > 0;
  }
  ok &= prefs.putUInt("orb_step", config.visual.orbitStepMs) > 0;
  ok &= prefs.putUChar("orb_tail", config.visual.orbitTailLeds) > 0;
  ok &= prefs.putUInt("hit_flash", config.visual.hitFlashMs) > 0;
  ok &= prefs.putUInt("dmg_chip", config.visual.damageChipMs) > 0;
  ok &= prefs.putUChar("back_gap", config.visual.phaseBackfillGapLeds) > 0;
  ok &= prefs.putUChar("back_scl", config.visual.phaseBackfillScale) > 0;
  ok &= prefs.putUInt("hp_pulse", config.visual.hpHitPulseMs) > 0;
  ok &= prefs.putUInt("def_blk", config.visual.defeatBlackoutMs) > 0;
  ok &= prefs.putUInt("def_rain", config.visual.defeatRainbowMs) > 0;
  ok &= prefs.putUChar("def_spin", config.visual.defeatRainbowSpins) > 0;
  ok &= prefs.putChar("piezo_do", config.sensor.piezoDoPin) > 0;
  ok &= prefs.putChar("piezo_ao", config.sensor.piezoAoPin) > 0;
  ok &= prefs.putUShort("hit_thr", config.sensor.hitThreshold) > 0;
  ok &= prefs.putUShort("rearm_thr", config.sensor.hitRearmThreshold) > 0;
  ok &= prefs.putUInt("cool_ms", config.sensor.hitCooldownMs) > 0;
  ok &= prefs.putUInt("rearm_st", config.sensor.hitRearmStableMs) > 0;
  ok &= prefs.putUInt("rearm_chk", config.sensor.hitRearmCheckMs) > 0;
  ok &= prefs.putUShort("dig_edges", config.sensor.digitalHitMinEdges) > 0;
  ok &= prefs.putUInt("dig_db_us", config.sensor.digitalIsrDebounceUs) > 0;
  ok &= prefs.putUInt("cap_win", config.sensor.captureWindowMs) > 0;
  ok &= prefs.putChar("led_pin", config.led.pin) > 0;
  ok &= prefs.putUShort("num_leds", config.led.numLeds) > 0;
  ok &= prefs.putString("led_type", config.led.ledType) >= 0;
  ok &= prefs.putString("color_ord", config.led.colorOrder) >= 0;
  ok &= prefs.putUChar("bright", config.led.brightness) > 0;
  ok &= prefs.putUShort("max_ma", config.led.maxMa) > 0;
  ok &= prefs.putChar("rst_pin", config.reset.buttonPin) > 0;
  ok &= prefs.putUInt("rst_hold", config.reset.buttonHoldMs) > 0;
  ok &= prefs.putString("wifi_ssid", config.wifiSsid) >= 0;
  ok &= prefs.putString("wifi_pass", config.wifiPassword) >= 0;
  ok &= prefs.putBool("net_auto", config.networkAutoStart) > 0;
  ok &= prefs.putUInt("net_delay", config.networkStartDelayMs) > 0;
  ok &= prefs.putString("mqtt_host", config.mqttHost) >= 0;
  ok &= prefs.putUShort("mqtt_port", config.mqttPort) > 0;
  ok &= prefs.putString("mqtt_user", config.mqttUsername) >= 0;
  ok &= prefs.putString("mqtt_pass", config.mqttPassword) >= 0;
  ok &= prefs.putString("mqtt_root", config.mqttRoot) >= 0;
  ok &= prefs.putBool("ota_cc", config.otaCommandCenterControlled) > 0;
  ok &= prefs.putBool("ota_auto", config.otaAutoCheckEnabled) > 0;
  ok &= prefs.putString("ota_channel", config.otaChannel) >= 0;
  ok &= prefs.putUInt("ota_build", config.otaDesiredBuild) > 0;
  ok &= prefs.putString("ota_pub_url", config.otaPublicManifestUrl) >= 0;
  ok &= prefs.putString("ota_mir_url", config.otaLocalMirrorUrl) >= 0;
  ok &= prefs.putUInt("ota_int_s", config.otaCheckIntervalS) > 0;
  ok &= prefs.putBool("ota_safe", config.otaApplyOnlyInSafeState) > 0;
  prefs.end();
  return ok;
}

bool RuntimeConfigStore::clear() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.clear();
  prefs.end();
  return ok;
}

}  // namespace hit_target
}  // namespace battlebang
