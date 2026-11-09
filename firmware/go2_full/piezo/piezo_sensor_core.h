#pragma once

#include <stdint.h>

namespace go2 {

enum PiezoChannelMask : uint8_t {
  PIEZO_CHANNEL_LEFT = 0x01,
  PIEZO_CHANNEL_RIGHT = 0x02,
  PIEZO_CHANNEL_FRONT = 0x04,
};

struct PiezoSensorConfig {
  int leftPin = -1;
  int rightPin = -1;
  int frontPin = -1;
  int debugDoPin = -1;
  uint8_t channelEnableMask = 0;
  int thresholdRaw = 0;
  int rearmRaw = 0;
  uint32_t captureWindowMs = 0;
  uint32_t rearmStableMs = 0;
  uint32_t hitCooldownMs = 0;
  uint32_t debugPeriodMs = 0;
};

struct PiezoSample {
  int raw = -1;
  int left = -1;
  int right = -1;
  int front = -1;
  int targetId = 1;
  const char* source = "piezo:none";
};

struct PiezoHitCandidate {
  int targetId = 1;
  int peakRaw = 0;
  int thresholdRaw = 0;
  uint32_t eventTsMs = 0;
  const char* source = "piezo:none";
};

struct PiezoChannelDiagnostics {
  bool enabled = false;
  bool qualified = false;
  int raw = -1;
};

struct PiezoDiagnostics {
  bool enabled = false;
  bool available = false;
  bool armed = false;
  bool capturing = false;
  uint8_t channelEnableMask = 0;
  int lastRaw = 0;
  int minRaw = 4095;
  int maxRaw = 0;
  uint32_t averageRaw = 0;
  uint32_t sampleCount = 0;
  PiezoChannelDiagnostics left;
  PiezoChannelDiagnostics right;
  PiezoChannelDiagnostics front;
};

struct PiezoProcessingEvents {
  uint8_t qualifiedMask = 0;
  bool rearmed = false;
  bool thresholdCrossed = false;
  PiezoSample sample;
};

class PiezoSensorCore {
 public:
  void begin(const PiezoSensorConfig& config,
             bool enabled,
             const PiezoSample& initialSample,
             uint32_t now);
  void setEnabled(bool enabled, const PiezoSample& initialSample, uint32_t now);
  bool processSample(PiezoSample sample, uint32_t now, PiezoHitCandidate& candidate);
  bool takeDebugSnapshot(uint32_t now, PiezoDiagnostics& snapshot);

  bool enabled() const { return enabled_; }
  bool available() const;
  bool channelEnabled(uint8_t channel) const;
  const PiezoSensorConfig& config() const { return config_; }
  PiezoDiagnostics diagnostics() const;
  const PiezoProcessingEvents& lastEvents() const { return lastEvents_; }

 private:
  struct ChannelState {
    bool qualified = false;
    bool lowDwellActive = false;
    uint32_t lowDwellStartedMs = 0;
  };

  void resetState(const PiezoSample& sample, uint32_t now);
  bool qualifyChannel(ChannelState& state,
                      bool enabled,
                      int raw,
                      uint32_t now,
                      uint8_t channel);
  void selectQualifiedPeak(PiezoSample& sample) const;
  void updateStats(const PiezoSample& sample);
  void resetStats(const PiezoSample& sample);
  void rearmWhenQuiet(uint32_t now, int raw);

  PiezoSensorConfig config_;
  bool enabled_ = false;
  bool armed_ = false;
  bool captureActive_ = false;
  uint32_t captureStartedMs_ = 0;
  int capturePeakRaw_ = 0;
  int captureTargetId_ = 1;
  const char* captureSource_ = "piezo:none";
  uint32_t lastCandidateMs_ = 0;
  uint32_t quietStartedMs_ = 0;
  uint32_t lastDebugMs_ = 0;
  int lastRaw_ = 0;
  int lastLeftRaw_ = 0;
  int lastRightRaw_ = 0;
  int lastFrontRaw_ = 0;
  int minRaw_ = 4095;
  int maxRaw_ = 0;
  uint32_t sumRaw_ = 0;
  uint32_t sampleCount_ = 0;
  ChannelState left_;
  ChannelState right_;
  ChannelState front_;
  PiezoProcessingEvents lastEvents_;
};

}  // namespace go2
