#include "station_controller.h"

namespace battlebang {
namespace station {

void StationController::begin(const RuntimeConfig& config, EventCallback callback, void* ctx) {
  callback_ = callback;
  callbackCtx_ = ctx;
  config_ = config;

  analogReadResolution(12);
  pinMode(::station::PIEZO_AO_PIN, INPUT);
  analogSetPinAttenuation(::station::PIEZO_AO_PIN, ADC_11db);
  FastLED.addLeds<WS2812B, ::station::LED_PIN, RGB>(leds_, ::station::MAX_LED_NUM_LEDS);
  FastLED.setBrightness(config_.led.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::station::LED_MAX_VOLTS, config_.led.maxMa);
  reset("boot");
}

void StationController::applyConfig(const RuntimeConfig& config, bool resetState, const char* source) {
  config_ = config;
  FastLED.setBrightness(config_.led.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(::station::LED_MAX_VOLTS, config_.led.maxMa);
  analogSetPinAttenuation(::station::PIEZO_AO_PIN, ADC_11db);
  if (resetState) {
    reset(source);
  } else if (!config_.configured) {
    mode_ = Mode::UNCONFIGURED;
  } else if (mode_ == Mode::UNCONFIGURED) {
    mode_ = captured_ ? Mode::CAPTURED : Mode::WAITING;
  }
  clearLedTail();
  lastShowMs_ = 0;
}

void StationController::reset(const char* source) {
  captured_ = false;
  lockedIgnoredHits_ = 0;
  mode_ = config_.configured ? Mode::WAITING : Mode::UNCONFIGURED;
  piezoArmed_ = true;
  lastHitMs_ = 0;
  capturedAtMs_ = 0;
  lastPeak_ = 0;
  hitFlashUntilMs_ = 0;
  lastEvent_ = "reset";
  clearLedTail();
  FastLED.show();
  emit("reset", source, 0);
}

bool StationController::simulateHit(const char* source, uint16_t peak) {
  return capture(source, peak, millis());
}

void StationController::prepareForOta() {
  mode_ = Mode::OTA_PREPARED;
  clearLedTail();
  FastLED.show();
}

void StationController::recoverFromFailedOta(const char* source) {
  mode_ = config_.configured ? (captured_ ? Mode::CAPTURED : Mode::WAITING) : Mode::UNCONFIGURED;
  lastShowMs_ = 0;
  render(millis());
  FastLED.show();
  emit("ota_failed", source, 0);
}

bool StationController::isSafeForOta() const {
  return mode_ == Mode::UNCONFIGURED || mode_ == Mode::WAITING || mode_ == Mode::CAPTURED || mode_ == Mode::OTA_PREPARED;
}

bool StationController::deferStateChangeStatusWhileArmed() const {
  return config_.configured && mode_ == Mode::WAITING && !captured_;
}

void StationController::loop(uint32_t now) {
  if (mode_ != Mode::OTA_PREPARED) pollSensor(now);
  if (captured_ && config_.gameplay.autoResetMs > 0 && now - capturedAtMs_ >= config_.gameplay.autoResetMs) {
    reset("auto_reset");
  }
  render(now);
}

uint16_t StationController::ledCount() const {
  return constrain(config_.led.numLeds, static_cast<uint16_t>(1), static_cast<uint16_t>(::station::MAX_LED_NUM_LEDS));
}

CRGB StationController::colorFromRgb(uint32_t rgb) const {
  return CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

const char* StationController::modeString() const {
  switch (mode_) {
    case Mode::UNCONFIGURED: return "UNCONFIGURED";
    case Mode::WAITING: return "WAITING";
    case Mode::CAPTURED: return "CAPTURED";
    case Mode::OTA_PREPARED: return "OTA_PREPARED";
  }
  return "UNKNOWN";
}

bool StationController::canCapture(uint32_t now) const {
  if (!config_.configured) return false;
  if (mode_ == Mode::OTA_PREPARED) return false;
  if (captured_ && config_.gameplay.lockAfterHit) return false;
  if (lastHitMs_ != 0 && now - lastHitMs_ < config_.sensor.hitCooldownMs) return false;
  return true;
}

bool StationController::capture(const char* source, uint16_t peak, uint32_t now) {
  if (captured_ && config_.gameplay.lockAfterHit) {
    ++lockedIgnoredHits_;
    lastPeak_ = peak;
    lastEvent_ = "locked_ignored";
    emit("locked_ignored", source, peak);
    return false;
  }
  if (!canCapture(now)) return false;
  captured_ = true;
  mode_ = Mode::CAPTURED;
  ++captureSequence_;
  lastHitMs_ = now;
  capturedAtMs_ = now;
  lastPeak_ = peak;
  hitFlashUntilMs_ = now + ::station::HIT_FLASH_MS;
  lastEvent_ = "captured";
  emit("captured", source, peak);
  return true;
}

void StationController::pollSensor(uint32_t now) {
  if (!config_.configured) return;
  if (config_.sensor.sampleIntervalMs > 0 && now - lastPiezoSampleMs_ < config_.sensor.sampleIntervalMs) return;
  lastPiezoSampleMs_ = now;
  if (config_.sensor.settleUs > 0) delayMicroseconds(config_.sensor.settleUs);
  const uint16_t value = static_cast<uint16_t>(analogRead(::station::PIEZO_AO_PIN));
  if (!piezoArmed_) {
    if (value <= config_.sensor.releaseThreshold) piezoArmed_ = true;
    return;
  }
  if (value >= config_.sensor.hitThreshold) {
    piezoArmed_ = false;
    capture("piezo", value, now);
  }
}

void StationController::emit(const char* name, const char* source, uint16_t peak) {
  if (callback_ == nullptr) return;
  StationEvent event;
  event.name = name;
  event.source = source;
  event.peak = peak;
  callback_(event, callbackCtx_);
}

void StationController::render(uint32_t now) {
  if (now - lastShowMs_ < ::station::LED_SHOW_PERIOD_MS) return;
  lastShowMs_ = now;

  const uint16_t count = ledCount();
  CRGB color = CRGB::Black;
  if (mode_ == Mode::UNCONFIGURED) {
    color = CRGB::Blue;
    color.nscale8_video(24);
  } else if (mode_ == Mode::OTA_PREPARED) {
    color = CRGB::Purple;
  } else if (captured_) {
    color = colorFromRgb(config_.led.capturedColor);
    if (static_cast<int32_t>(hitFlashUntilMs_ - now) > 0) color = colorFromRgb(config_.led.hitFlashColor);
  } else {
    color = colorFromRgb(config_.led.waitingColor);
    const uint8_t breath = beatsin8(config_.led.waitingBreathBpm,
                                    config_.led.waitingBreathMin,
                                    config_.led.waitingBreathMax,
                                    0,
                                    0);
    color.nscale8_video(breath);
  }

  fill_solid(leds_, count, color);
  for (uint16_t i = count; i < ::station::MAX_LED_NUM_LEDS; ++i) leds_[i] = CRGB::Black;
  FastLED.show();
}

void StationController::clearLedTail() {
  fill_solid(leds_, ::station::MAX_LED_NUM_LEDS, CRGB::Black);
}

void StationController::appendStatus(JsonObject obj) const {
  obj["station_id"] = config_.stationId;
  obj["device_id"] = config_.deviceId.length() > 0 ? config_.deviceId : config_.stationId;
  obj["device_mac"] = config_.deviceMac;
  obj["display_name"] = config_.displayName;
  obj["name"] = config_.displayName;
  obj["device_type"] = "station";
  obj["group"] = config_.group;
  obj["stage_id"] = config_.stageId;
  obj["location"] = config_.location;
  obj["configured"] = config_.configured;
  obj["config_version"] = config_.configVersion;
  obj["mode"] = modeString();
  obj["active"] = captured_;
  obj["captured"] = captured_;
  obj["pressed"] = captured_;
  obj["triggered"] = captured_;
  obj["capture_sequence"] = captureSequence_;
  obj["locked_ignored_hits"] = lockedIgnoredHits_;
  obj["last_peak"] = lastPeak_;
  obj["last_event"] = lastEvent_;
  obj["ota_safe"] = isSafeForOta();
  obj["debug_allow_simulate_hit"] = config_.debugAllowSimulateHit;
  obj["sensor_hit_threshold"] = config_.sensor.hitThreshold;
  obj["sensor_release_threshold"] = config_.sensor.releaseThreshold;
  obj["led_count"] = ledCount();
  JsonObject station = obj.createNestedObject("station");
  station["station_id"] = config_.stationId;
  station["active"] = captured_;
  station["captured"] = captured_;
  station["mode"] = modeString();
  station["capture_sequence"] = captureSequence_;
  station["locked_ignored_hits"] = lockedIgnoredHits_;
}

String StationController::statusSignature() const {
  String s;
  s.reserve(96);
  s += modeString();
  s += '|';
  s += captured_ ? '1' : '0';
  s += '|';
  s += String(captureSequence_);
  s += '|';
  s += String(lockedIgnoredHits_);
  s += '|';
  s += lastEvent_;
  return s;
}

void StationController::printBootBanner() const {
  Serial.print("[station] app=");
  Serial.print(::station::FIRMWARE_NAME);
  Serial.print(" station_id=");
  Serial.print(config_.stationId);
  Serial.print(" device_id=");
  Serial.print(config_.deviceId);
  Serial.print(" led_pin=");
  Serial.print(::station::LED_PIN);
  Serial.print(" piezo_pin=");
  Serial.println(::station::PIEZO_AO_PIN);
}

}  // namespace station
}  // namespace battlebang
