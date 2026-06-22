#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#include "boss_target/build_config.h"
#include "boss_target/config/runtime_config.h"

namespace battlebang {
namespace boss_target {

struct BossTargetEvent {
  const char* name = "state";
  const char* source = "state";
  uint8_t targetIndex = 255;
  uint16_t peak = 0;
  uint16_t digitalEdges = 0;
};

class BossTargetController {
 public:
  using EventCallback = void (*)(const BossTargetEvent& event, void* ctx);

  void begin(const RuntimeConfig& config, EventCallback callback, void* ctx);
  void applyConfig(const RuntimeConfig& config, bool resetState, const char* source);
  void loop(uint32_t now);
  void reset(const char* source);
  void start(const char* source, bool resetHp = true);
  void simulateHit(const char* source, int targetIndex = -1);
  void prepareForOta();
  void recoverFromFailedOta(const char* source);
  bool isSafeForOta() const;
  bool destroyed() const { return hpRemaining_ <= 0; }
  bool hitEnabled() const { return hitEnabled_; }
  void setHitEnabled(bool enabled) { hitEnabled_ = enabled; }
  bool vulnerableNow(uint32_t now) const;

  void appendStatus(JsonObject obj) const;
  String statusSignature() const;
  void printBootBanner() const;

 private:
  uint8_t targetCount() const;
  uint16_t ringLedCount() const;
  uint16_t hpLedCount() const;
  uint16_t hpGroupCount() const;
  CRGB colorFromRgb(uint32_t rgb) const;
  CRGB hpColor() const;
  CRGB hpColorForBand(uint8_t band) const;
  uint16_t hpLitGroupCount() const;
  uint8_t hpPhase() const;
  uint8_t hpBandForValue(int hp) const;
  uint16_t hpLitGroupCountForValue(int hp) const;
  const char* modeString() const;
  const char* commandState() const;

  void configurePins(const RuntimeConfig& previous, const RuntimeConfig& next);
  void pollPiezoAo(uint32_t now);
  uint16_t popPiezoEdges(uint8_t index);
  uint16_t popPiezoPeak(uint8_t index);
  void clearPiezoEdges();
  void selectNewTarget(uint32_t now);
  void clearActiveTarget();
  void applyDamage(uint8_t targetIndex, const char* source, uint16_t edges, uint16_t peak, uint32_t now);
  void recordWrongHit(uint8_t targetIndex, const char* source, uint16_t edges, uint16_t peak, uint32_t now);
  void emit(const char* name, uint8_t targetIndex, uint16_t peak, const char* source, uint16_t edges);

  void fillRing(uint8_t index, const CRGB& color);
  void setHpBarGroup(uint16_t group0Based, const CRGB& color);
  void setHpBarAll(const CRGB& color);
  void renderStartIntro(uint32_t now);
  void renderTargets(uint32_t now);
  void renderHpBar(uint32_t now);
  void clearAllLeds();
  void renderLeds(uint32_t now);

  enum class Mode : uint8_t { UNCONFIGURED, READY, INTRO, ACTIVE, DEFEATED };

  RuntimeConfig config_;
  Mode mode_ = Mode::UNCONFIGURED;
  int hpRemaining_ = 0;
  uint32_t sequence_ = 0;
  int activeTarget_ = -1;
  uint32_t startIntroStartedMs_ = 0;
  uint32_t startIntroUntilMs_ = 0;
  uint32_t targetStartedMs_ = 0;
  uint32_t nextTargetSelectionMs_ = 0;
  uint32_t lastAcceptedHitMs_ = 0;
  uint32_t lastShowMs_ = 0;
  uint32_t lastDeadBlinkMs_ = 0;
  bool deadBlinkOn_ = false;
  bool hitEnabled_ = true;
  bool otaPrepared_ = false;
  bool pinsConfigured_ = false;
  bool targetTransitionPending_ = false;

  int lastHitTargetIndex_ = -1;
  int lastWrongTargetIndex_ = -1;
  String lastEvent_ = "boot";
  uint32_t lastTargetHitMs_[::boss_target::kMaxTargets] = {0, 0, 0, 0};
  bool lastTargetHitOk_[::boss_target::kMaxTargets] = {false, false, false, false};
  uint32_t targetFlashUntilMs_[::boss_target::kMaxTargets] = {0, 0, 0, 0};

  uint16_t piezoEdgeCount_[::boss_target::kMaxTargets] = {0, 0, 0, 0};
  uint16_t piezoPeak_[::boss_target::kMaxTargets] = {0, 0, 0, 0};
  bool piezoArmed_[::boss_target::kMaxTargets] = {true, true, true, true};
  uint32_t lastPiezoSampleMs_ = 0;

  CRGB ring1_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB ring2_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB ring3_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB ring4_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB hpBar_[::boss_target::MAX_HP_BAR_NUM_LEDS];
  CRGB* rings_[::boss_target::kMaxTargets] = {ring1_, ring2_, ring3_, ring4_};

  EventCallback callback_ = nullptr;
  void* callbackCtx_ = nullptr;
};

}  // namespace boss_target
}  // namespace battlebang
