#include "go2_full/piezo/piezo_sensor_core.h"

namespace go2 {
namespace {

int safeRaw(int raw) { return raw < 0 ? 0 : raw; }

}  // namespace

void PiezoSensorCore::begin(const PiezoSensorConfig& config,
                            bool enabled,
                            const PiezoSample& initialSample,
                            uint32_t now) {
  config_ = config;
  enabled_ = enabled;
  lastDebugMs_ = 0;
  resetState(initialSample, now);
}

void PiezoSensorCore::setEnabled(bool enabled,
                                 const PiezoSample& initialSample,
                                 uint32_t now) {
  enabled_ = enabled;
  resetState(initialSample, now);
}

bool PiezoSensorCore::available() const {
  return channelEnabled(PIEZO_CHANNEL_LEFT) ||
         channelEnabled(PIEZO_CHANNEL_RIGHT) ||
         channelEnabled(PIEZO_CHANNEL_FRONT);
}

bool PiezoSensorCore::channelEnabled(uint8_t channel) const {
  if ((config_.channelEnableMask & channel) == 0) return false;
  if (channel == PIEZO_CHANNEL_LEFT) return config_.leftPin >= 0;
  if (channel == PIEZO_CHANNEL_RIGHT) return config_.rightPin >= 0;
  if (channel == PIEZO_CHANNEL_FRONT) return config_.frontPin >= 0;
  return false;
}

void PiezoSensorCore::resetState(const PiezoSample& sample, uint32_t now) {
  (void)now;
  armed_ = false;
  captureActive_ = false;
  captureStartedMs_ = 0;
  capturePeakRaw_ = 0;
  captureTargetId_ = 1;
  captureSource_ = "piezo:none";
  lastCandidateMs_ = 0;
  quietStartedMs_ = 0;
  left_ = ChannelState{};
  right_ = ChannelState{};
  front_ = ChannelState{};
  lastEvents_ = PiezoProcessingEvents{};
  resetStats(sample);
}

bool PiezoSensorCore::qualifyChannel(ChannelState& state,
                                     bool enabled,
                                     int raw,
                                     uint32_t now,
                                     uint8_t channel) {
  if (!enabled || raw < 0) {
    state = ChannelState{};
    return false;
  }
  if (state.qualified) return false;
  if (raw > config_.rearmRaw) {
    state.lowDwellActive = false;
    state.lowDwellStartedMs = 0;
    return false;
  }
  if (!state.lowDwellActive) {
    state.lowDwellActive = true;
    state.lowDwellStartedMs = now;
    return false;
  }
  if (uint32_t(now - state.lowDwellStartedMs) < config_.rearmStableMs) return false;
  state.qualified = true;
  state.lowDwellActive = false;
  lastEvents_.qualifiedMask |= channel;
  return true;
}

void PiezoSensorCore::selectQualifiedPeak(PiezoSample& sample) const {
  sample.raw = -1;
  sample.targetId = 1;
  sample.source = "piezo:none";
  if (left_.qualified && sample.left >= 0) {
    sample.raw = sample.left;
    sample.targetId = 1;
    sample.source = "piezo:left";
  }
  if (right_.qualified && sample.right > sample.raw) {
    sample.raw = sample.right;
    sample.targetId = 2;
    sample.source = "piezo:right";
  }
  if (front_.qualified && sample.front > sample.raw) {
    sample.raw = sample.front;
    sample.targetId = 3;
    sample.source = "piezo:front";
  }
}

void PiezoSensorCore::resetStats(const PiezoSample& sample) {
  const int raw = safeRaw(sample.raw);
  lastRaw_ = sample.raw;
  lastLeftRaw_ = sample.left;
  lastRightRaw_ = sample.right;
  lastFrontRaw_ = sample.front;
  minRaw_ = raw;
  maxRaw_ = raw;
  sumRaw_ = 0;
  sampleCount_ = 0;
}

void PiezoSensorCore::updateStats(const PiezoSample& sample) {
  const int raw = safeRaw(sample.raw);
  lastRaw_ = sample.raw;
  lastLeftRaw_ = sample.left;
  lastRightRaw_ = sample.right;
  lastFrontRaw_ = sample.front;
  if (raw < minRaw_) minRaw_ = raw;
  if (raw > maxRaw_) maxRaw_ = raw;
  sumRaw_ += static_cast<uint32_t>(raw);
  ++sampleCount_;
}

void PiezoSensorCore::rearmWhenQuiet(uint32_t now, int raw) {
  if (armed_ || captureActive_) return;
  if (uint32_t(now - lastCandidateMs_) < config_.hitCooldownMs) return;
  if (raw < 0) return;
  if (raw > config_.rearmRaw) {
    quietStartedMs_ = 0;
    return;
  }
  if (quietStartedMs_ == 0) {
    quietStartedMs_ = now;
    return;
  }
  if (uint32_t(now - quietStartedMs_) < config_.rearmStableMs) return;
  armed_ = true;
  quietStartedMs_ = 0;
  lastEvents_.rearmed = true;
}

bool PiezoSensorCore::processSample(PiezoSample sample,
                                    uint32_t now,
                                    PiezoHitCandidate& candidate) {
  lastEvents_ = PiezoProcessingEvents{};
  if (!enabled_ || !available()) return false;

  qualifyChannel(left_, channelEnabled(PIEZO_CHANNEL_LEFT), sample.left, now, PIEZO_CHANNEL_LEFT);
  qualifyChannel(right_, channelEnabled(PIEZO_CHANNEL_RIGHT), sample.right, now, PIEZO_CHANNEL_RIGHT);
  qualifyChannel(front_, channelEnabled(PIEZO_CHANNEL_FRONT), sample.front, now, PIEZO_CHANNEL_FRONT);
  selectQualifiedPeak(sample);
  lastEvents_.sample = sample;
  updateStats(sample);

  if (captureActive_) {
    if (sample.raw > capturePeakRaw_) {
      capturePeakRaw_ = sample.raw;
      captureTargetId_ = sample.targetId;
      captureSource_ = sample.source;
    }
    if (uint32_t(now - captureStartedMs_) >= config_.captureWindowMs) {
      candidate.targetId = captureTargetId_;
      candidate.peakRaw = capturePeakRaw_;
      candidate.thresholdRaw = config_.thresholdRaw;
      candidate.eventTsMs = captureStartedMs_;
      candidate.source = captureSource_;
      captureActive_ = false;
      lastCandidateMs_ = now;
      quietStartedMs_ = 0;
      return true;
    }
    return false;
  }

  rearmWhenQuiet(now, sample.raw);
  if (armed_ && sample.raw >= config_.thresholdRaw) {
    armed_ = false;
    captureActive_ = true;
    captureStartedMs_ = now;
    capturePeakRaw_ = sample.raw;
    captureTargetId_ = sample.targetId;
    captureSource_ = sample.source;
    quietStartedMs_ = 0;
    lastEvents_.thresholdCrossed = true;
  }
  return false;
}

PiezoDiagnostics PiezoSensorCore::diagnostics() const {
  PiezoDiagnostics result;
  result.enabled = enabled_;
  result.available = available();
  result.armed = armed_;
  result.capturing = captureActive_;
  result.channelEnableMask = config_.channelEnableMask;
  result.lastRaw = lastRaw_;
  result.minRaw = minRaw_;
  result.maxRaw = maxRaw_;
  result.averageRaw = sampleCount_ > 0 ? sumRaw_ / sampleCount_ : static_cast<uint32_t>(lastRaw_);
  result.sampleCount = sampleCount_;
  result.left.enabled = channelEnabled(PIEZO_CHANNEL_LEFT);
  result.left.qualified = left_.qualified;
  result.left.raw = lastLeftRaw_;
  result.right.enabled = channelEnabled(PIEZO_CHANNEL_RIGHT);
  result.right.qualified = right_.qualified;
  result.right.raw = lastRightRaw_;
  result.front.enabled = channelEnabled(PIEZO_CHANNEL_FRONT);
  result.front.qualified = front_.qualified;
  result.front.raw = lastFrontRaw_;
  return result;
}

bool PiezoSensorCore::takeDebugSnapshot(uint32_t now, PiezoDiagnostics& snapshot) {
  if (uint32_t(now - lastDebugMs_) < config_.debugPeriodMs) return false;
  lastDebugMs_ = now;
  snapshot = diagnostics();
  PiezoSample lastSample;
  lastSample.raw = lastRaw_;
  lastSample.left = lastLeftRaw_;
  lastSample.right = lastRightRaw_;
  lastSample.front = lastFrontRaw_;
  resetStats(lastSample);
  return true;
}

}  // namespace go2
