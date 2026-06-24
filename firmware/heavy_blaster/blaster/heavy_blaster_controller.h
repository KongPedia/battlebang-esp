#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#include "heavy-blaster/build_config.h"
#include "heavy-blaster/config/runtime_config.h"

namespace battlebang {
namespace heavy_blaster {

struct HeavyBlasterEvent {
  const char* name = "state";
  const char* source = "state";
  int slotIndex = -1;
};

class HeavyBlasterController {
 public:
  using EventCallback = void (*)(const HeavyBlasterEvent& event, void* ctx);

  void begin(const RuntimeConfig& config, EventCallback callback, void* ctx);
  void applyConfig(const RuntimeConfig& config, bool resetState, const char* source);
  void loop(uint32_t now);
  void reset(const char* source);
  void forceSafeOff(const char* source);
  void prepareForOta();
  void recoverFromFailedOta(const char* source);
  bool isSafeForOta() const;

  bool setSlot(uint8_t index, bool active, const char* source);
  bool setSlots(const bool slots[::heavy_blaster::kSlotCount], uint8_t count, const char* source);
  bool unlock(const char* source);

  void appendStatus(JsonObject obj) const;
  String statusSignature() const;
  void printBootBanner() const;

 private:
  enum class Mode : uint8_t { UNCONFIGURED, LOCKED, PARTIAL, UNLOCK_PRE_EFFECT, UNLOCKED, OTA_PREPARED };

  uint8_t slotCount() const;
  uint8_t requiredSlots() const;
  uint16_t matrixLedCount() const;
  CRGB colorFromRgb(uint32_t rgb) const;
  const char* modeString() const;
  const char* effectString() const;
  uint8_t activeSlotCount() const;
  bool allRequiredSlotsActive() const;
  Mode currentMode() const;

  void emit(const char* name, int slotIndex, const char* source);
  int relayOnLevel() const;
  int relayOffLevel() const;
  void relayWrite(bool on);
  void evaluateUnlockState(uint32_t now, const char* source);

  void setMatrix(uint8_t index, const CRGB& color);
  void showNormalState();
  void updatePreUnlockEffect(uint32_t now);
  void updateUnlockedEffect(uint32_t now);
  void render(uint32_t now);
  void drawMovingArrow(uint8_t matrixIndex, int frame);
  void drawRainbowTrail(uint8_t matrixIndex, int frame);
  void drawFullRainbow(uint8_t matrixIndex);
  int xy(int x, int y) const;
  void setVisualPixel(uint8_t matrixIndex, int col, int row, CRGB color);

  RuntimeConfig config_;
  bool slots_[::heavy_blaster::kSlotCount] = {false, false, false, false};
  bool relayOn_ = false;
  bool preUnlockActive_ = false;
  bool unlockedEffectActive_ = false;
  bool otaPrepared_ = false;
  uint8_t rainbowOffset_ = 0;
  uint32_t effectFrame_ = ::heavy_blaster::ARROW_FRAMES;
  uint32_t preUnlockStartMs_ = 0;
  uint32_t lastPreEffectUpdateMs_ = 0;
  uint32_t lastEffectUpdateMs_ = 0;
  uint32_t lastShowMs_ = 0;
  String lastEvent_ = "boot";
  int lastChangedSlot_ = -1;

  CRGB matrix1_[::heavy_blaster::MAX_MATRIX_NUM_LEDS];
  CRGB matrix2_[::heavy_blaster::MAX_MATRIX_NUM_LEDS];
  CRGB matrix3_[::heavy_blaster::MAX_MATRIX_NUM_LEDS];
  CRGB matrix4_[::heavy_blaster::MAX_MATRIX_NUM_LEDS];
  CRGB* matrices_[::heavy_blaster::kSlotCount] = {matrix1_, matrix2_, matrix3_, matrix4_};

  EventCallback callback_ = nullptr;
  void* callbackCtx_ = nullptr;
};

}  // namespace heavy_blaster
}  // namespace battlebang
