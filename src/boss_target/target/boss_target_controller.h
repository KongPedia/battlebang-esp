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
  bool isSafeForOta() const;
  bool destroyed() const { return hpRemaining_ <= 0; }
  bool hitEnabled() const { return hitEnabled_; }
  void setHitEnabled(bool enabled) { hitEnabled_ = enabled; }
  bool vulnerableNow(uint32_t now) const;

  void appendStatus(JsonObject obj) const;
  String statusSignature() const;
  void printBootBanner() const;

 private:
  static void IRAM_ATTR piezoIsr0();
  static void IRAM_ATTR piezoIsr1();
  static void IRAM_ATTR piezoIsr2();
  static void IRAM_ATTR piezoIsr3();
  void IRAM_ATTR onPiezoIsr(uint8_t index);

  uint8_t targetCount() const;
  uint16_t ringLedCount() const;
  uint16_t hpLedCount() const;
  uint16_t hpGroupCount() const;
  CRGB colorFromRgb(uint32_t rgb) const;
  CRGB hpColor() const;
  CRGB hpColorForBand(uint8_t band) const;
  CRGB nextHpColorForBand(uint8_t band) const;
  uint16_t hpLitGroupCount() const;
  uint8_t hpPhase() const;
  uint8_t hpBandForValue(int hp) const;
  uint16_t hpLitGroupCountForValue(int hp) const;
  const char* modeString() const;
  const char* commandState() const;

  void configurePins(const RuntimeConfig& previous, const RuntimeConfig& next);
  uint16_t popPiezoEdges(uint8_t index);
  void clearPiezoEdges();
  void selectNewTarget(uint32_t now);
  void clearActiveTarget();
  void applyDamage(uint8_t targetIndex, const char* source, uint16_t edges, uint32_t now);
  void recordWrongHit(uint8_t targetIndex, const char* source, uint16_t edges, uint32_t now);
  void emit(const char* name, uint8_t targetIndex, uint16_t peak, const char* source, uint16_t edges);

  void fillRing(uint8_t index, const CRGB& color);
  void setHpBarGroup(uint16_t group0Based, const CRGB& color);
  void setHpBarAll(const CRGB& color);
  void renderTargets(uint32_t now);
  void renderHpBar(uint32_t now);
  void clearHpBlinkMask();
  void addHpBlinkSegment(int oldHp, int newHp);
  void clearAllLeds();
  void renderLeds(uint32_t now);

  enum class Mode : uint8_t { UNCONFIGURED, READY, ACTIVE, DEFEATED };

  RuntimeConfig config_;
  Mode mode_ = Mode::UNCONFIGURED;
  int hpRemaining_ = 0;
  uint32_t sequence_ = 0;
  int activeTarget_ = -1;
  uint32_t targetStartedMs_ = 0;
  uint32_t lastAcceptedHitMs_ = 0;
  uint32_t lastShowMs_ = 0;
  uint32_t hpFlashUntilMs_ = 0;
  uint32_t lastHpBlinkMs_ = 0;
  uint32_t lastDeadBlinkMs_ = 0;
  bool hpBlinkOn_ = false;
  bool deadBlinkOn_ = false;
  bool hitEnabled_ = true;
  bool otaPrepared_ = false;
  bool pinsConfigured_ = false;

  int lastHitTargetIndex_ = -1;
  int lastWrongTargetIndex_ = -1;
  String lastEvent_ = "boot";
  uint32_t lastTargetHitMs_[::boss_target::kMaxTargets] = {0, 0, 0, 0};
  bool lastTargetHitOk_[::boss_target::kMaxTargets] = {false, false, false, false};
  uint32_t targetFlashUntilMs_[::boss_target::kMaxTargets] = {0, 0, 0, 0};

  volatile uint16_t piezoEdgeCount_[::boss_target::kMaxTargets] = {0, 0, 0, 0};
  volatile uint32_t lastIsrUs_[::boss_target::kMaxTargets] = {0, 0, 0, 0};

  CRGB ring1_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB ring2_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB ring3_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB ring4_[::boss_target::MAX_RING_NUM_LEDS];
  CRGB hpBar_[::boss_target::MAX_HP_BAR_NUM_LEDS];
  bool hpBlinkMask_[::boss_target::MAX_HP_BAR_GROUP_COUNT] = {};
  CRGB* rings_[::boss_target::kMaxTargets] = {ring1_, ring2_, ring3_, ring4_};

  EventCallback callback_ = nullptr;
  void* callbackCtx_ = nullptr;

  static BossTargetController* isrInstance_;
};

}  // namespace boss_target
}  // namespace battlebang
