#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#include "station/build_config.h"
#include "station/config/runtime_config.h"

namespace battlebang {
namespace station {

struct StationEvent {
  const char* name = "state";
  const char* source = "state";
  uint16_t peak = 0;
};

class StationController {
 public:
  using EventCallback = void (*)(const StationEvent& event, void* ctx);

  void begin(const RuntimeConfig& config, EventCallback callback, void* ctx);
  void applyConfig(const RuntimeConfig& config, bool resetState, const char* source);
  void loop(uint32_t now);
  void reset(const char* source);
  bool simulateHit(const char* source, uint16_t peak = ::station::PIEZO_AO_THRESHOLD);
  void prepareForOta();
  void recoverFromFailedOta(const char* source);
  bool isSafeForOta() const;
  bool deferAutomaticStatusWhileArmed() const;

  void appendStatus(JsonObject obj) const;
  String statusSignature() const;
  void printBootBanner() const;

 private:
  enum class Mode : uint8_t { UNCONFIGURED, WAITING, CAPTURED, OTA_PREPARED };

  uint16_t ledCount() const;
  CRGB colorFromRgb(uint32_t rgb) const;
  const char* modeString() const;
  bool canCapture(uint32_t now) const;
  bool capture(const char* source, uint16_t peak, uint32_t now);
  void pollSensor(uint32_t now);
  void emit(const char* name, const char* source, uint16_t peak);
  void render(uint32_t now);
  void clearLedTail();

  RuntimeConfig config_;
  Mode mode_ = Mode::UNCONFIGURED;
  bool captured_ = false;
  uint32_t captureSequence_ = 0;
  uint32_t lockedIgnoredHits_ = 0;
  bool piezoArmed_ = true;
  uint32_t lastHitMs_ = 0;
  uint32_t capturedAtMs_ = 0;
  uint32_t lastPiezoSampleMs_ = 0;
  uint32_t hitFlashUntilMs_ = 0;
  uint32_t lastShowMs_ = 0;
  uint16_t lastPeak_ = 0;
  String lastEvent_ = "boot";

  CRGB leds_[::station::MAX_LED_NUM_LEDS];

  EventCallback callback_ = nullptr;
  void* callbackCtx_ = nullptr;
};

}  // namespace station
}  // namespace battlebang
