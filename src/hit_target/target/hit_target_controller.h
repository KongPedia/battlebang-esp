#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#include "hit_target/build_config.h"
#include "hit_target/config/runtime_config.h"

namespace battlebang {
namespace hit_target {

struct HitTargetEvent {
  const char* name = "state";
  const char* source = "state";
  uint16_t peak = 0;
  uint16_t digitalEdges = 0;
};

class HitTargetController {
 public:
  using EventCallback = void (*)(const HitTargetEvent& event, void* ctx);

  void begin(const RuntimeConfig& config, EventCallback callback, void* ctx);
  void applyConfig(const RuntimeConfig& config, bool resetState, const char* source);
  void loop(uint32_t now);
  void reset(const char* source);
  void simulateHit(const char* source);
  void prepareForOta();
  bool isSafeForOta() const;
  bool destroyed() const { return target_.destroyed; }
  bool hitEnabled() const { return hitEnabled_; }
  void setHitEnabled(bool enabled) { hitEnabled_ = enabled; }

  void appendStatus(JsonObject obj) const;
  String statusSignature() const;
  void printBootBanner() const;

 private:
  struct TargetState {
    uint32_t sequence = 0;
    int hpRemaining = 0;
    bool damaged = false;
    bool destroyed = false;
  };

  struct CaptureState {
    bool active = false;
    uint32_t startedMs = 0;
    uint16_t peak = 0;
    uint16_t digitalEdges = 0;
    bool fromDigital = false;
  };

  struct SensorState {
    volatile uint16_t digitalEdgeCount = 0;
    volatile uint32_t lastIsrUs = 0;
    bool armed = true;
    uint32_t quietStartedMs = 0;
    uint32_t lastRearmCheckMs = 0;
  };

  struct ResetButtonState {
    bool pressed = false;
    bool consumed = false;
    uint32_t pressedSinceMs = 0;
  };

  struct EffectTimers {
    uint32_t lockoutUntilMs = 0;
    uint32_t hitFlashUntilMs = 0;
    uint32_t hpPulseUntilMs = 0;
    uint32_t defeatStartedMs = 0;
    uint32_t defeatUntilMs = 0;
    uint32_t lastShowMs = 0;
  };

  struct DamageChipState {
    uint32_t startedMs = 0;
    uint32_t visibleUntilMs = 0;
    int firstLed = 0;
    int endLed = 0;
    bool phaseTransition = false;
    int previousPhaseIndex = 0;
  };

  static void IRAM_ATTR piezoDoIsrStatic();
  void onPiezoDoIsr();

  int ledCount() const;
  int maxHits() const;
  int phaseIndexForHp(int hp) const;
  int phaseHitsRemaining(int hp) const;
  int phaseLitCount(int hp) const;
  CRGB phaseColor(int phaseIndex) const;
  CRGB addBlend(const CRGB& base, const CRGB& overlay) const;
  CRGB scaleColor(const CRGB& color, uint8_t scale) const;
  CRGB applyHpHitPulse(const CRGB& color, uint32_t now) const;

  void clearRuntimeState();
  void resetSensorRuntime();
  void resetEffects();
  void resetDamageChip();
  void releaseResetButton();
  bool isLockedOut(uint32_t now) const;
  bool resetButtonPressed() const;
  bool phaseRevealPending(uint32_t now) const;
  bool damageVisible(uint32_t now) const;
  int damageLength() const;
  int damageVisibleCount(uint32_t now) const;
  int damageExpiredCount(uint32_t now) const;
  void captureDamageChip(int previousHp, int currentHp, uint32_t now);
  void delayDamageChipUntil(uint32_t startMs);

  void clearLayer(CRGB* layer);
  void renderPhaseBackfill(int phaseIndex, int lit);
  void renderPhaseTransitionReveal(uint32_t now);
  void renderHpLayer(uint32_t now);
  void renderDamageLayer(uint32_t now);
  void renderOrbitLayer(uint32_t now);
  void renderDefeatRainbow(uint32_t now);
  void renderLeds(uint32_t now);

  uint16_t popPiezoDoEdges();
  void clearPiezoDoFlag();
  uint16_t readPiezoAnalog() const;
  void startCapture(uint32_t now, bool fromDigital, uint16_t initialPeak, uint16_t initialDigitalEdges);
  void finishCaptureIfDue(uint32_t now);
  bool piezoQuiet() const;
  void updateSensorRearm(uint32_t now);
  void pollPiezo(uint32_t now);
  void pollResetButton(uint32_t now);
  void registerHit(uint32_t now, uint16_t peak, const char* source, uint16_t digitalEdges = 0);
  void emit(const char* name, uint16_t peak, const char* source, uint16_t digitalEdges = 0);
  void configurePins(const RuntimeConfig& previous, const RuntimeConfig& next);

  RuntimeConfig config_;
  TargetState target_;
  CaptureState capture_;
  SensorState sensor_;
  ResetButtonState resetButton_;
  EffectTimers timers_;
  DamageChipState damageChip_;
  bool hitEnabled_ = true;
  bool pinsConfigured_ = false;
  EventCallback callback_ = nullptr;
  void* callbackCtx_ = nullptr;

  CRGB leds_[::hit_target::NUM_LEDS];
  CRGB hpLayer_[::hit_target::NUM_LEDS];
  CRGB damageLayer_[::hit_target::NUM_LEDS];
  CRGB orbitLayer_[::hit_target::NUM_LEDS];

  static HitTargetController* isrInstance_;
};

}  // namespace hit_target
}  // namespace battlebang
