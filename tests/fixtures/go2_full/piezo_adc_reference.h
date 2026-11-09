#pragma once

// Executable characterization of the ADC algorithm captured from PR #59 in
// .omx/baselines/go2-full/main-before-modules.cpp. This is a test oracle, not
// firmware or a legacy runtime.

#include <cstdint>

namespace go2_full_reference {

enum Channel : uint8_t { Left = 0x01, Right = 0x02, Front = 0x04 };

struct Config {
  uint8_t mask = Left | Right;
  int threshold = 2400;
  int rearm = 1800;
  uint32_t capture_ms = 30;
  uint32_t rearm_stable_ms = 30;
  uint32_t cooldown_ms = 0;
};

struct Sample {
  Sample() = default;
  Sample(int left_value, int right_value, int front_value)
      : left(left_value), right(right_value), front(front_value) {}
  int left = -1;
  int right = -1;
  int front = -1;
  int raw = -1;
  int target = 1;
};

struct Candidate {
  Candidate() = default;
  Candidate(int target_value, int peak_value, uint32_t event_value)
      : target(target_value), peak(peak_value), event_ms(event_value) {}
  int target = 0;
  int peak = 0;
  uint32_t event_ms = 0;
};

struct ChannelState {
  bool qualified = false;
  bool low_dwell = false;
  uint32_t low_since = 0;
};

class PiezoReference {
 public:
  explicit PiezoReference(Config config) : config_(config) {}

  void setEnabled(bool enabled) {
    enabled_ = enabled;
    reset();
  }

  bool enabled() const { return enabled_; }
  bool armed() const { return armed_; }
  bool capturing() const { return capturing_; }
  bool qualified(Channel channel) const {
    return state(channel).qualified;
  }

  bool process(Sample sample, uint32_t now, Candidate& candidate) {
    if (!enabled_) return false;
    qualify(left_, enabled(Left), sample.left, now);
    qualify(right_, enabled(Right), sample.right, now);
    qualify(front_, enabled(Front), sample.front, now);
    select(sample);

    if (capturing_) {
      if (sample.raw > peak_) {
        peak_ = sample.raw;
        peak_target_ = sample.target;
      }
      if (now - capture_started_ >= config_.capture_ms) {
        candidate = Candidate{peak_target_, peak_, capture_started_};
        capturing_ = false;
        last_candidate_ = now;
        quiet_since_ = 0;
        return true;
      }
      return false;
    }

    rearm(now, sample.raw);
    if (armed_ && sample.raw >= config_.threshold) {
      armed_ = false;
      capturing_ = true;
      capture_started_ = now;
      peak_ = sample.raw;
      peak_target_ = sample.target;
      quiet_since_ = 0;
    }
    return false;
  }

 private:
  bool enabled(Channel channel) const {
    return (config_.mask & channel) != 0;
  }

  const ChannelState& state(Channel channel) const {
    if (channel == Left) return left_;
    if (channel == Right) return right_;
    return front_;
  }

  void qualify(ChannelState& state, bool channel_enabled, int raw, uint32_t now) {
    if (!channel_enabled || raw < 0) {
      state = ChannelState{};
      return;
    }
    if (state.qualified) return;
    if (raw > config_.rearm) {
      state.low_dwell = false;
      state.low_since = 0;
      return;
    }
    if (!state.low_dwell) {
      state.low_dwell = true;
      state.low_since = now;
      return;
    }
    if (now - state.low_since < config_.rearm_stable_ms) return;
    state.qualified = true;
    state.low_dwell = false;
  }

  void select(Sample& sample) const {
    sample.raw = -1;
    if (left_.qualified && sample.left >= 0) {
      sample.raw = sample.left;
      sample.target = 1;
    }
    if (right_.qualified && sample.right > sample.raw) {
      sample.raw = sample.right;
      sample.target = 2;
    }
    if (front_.qualified && sample.front > sample.raw) {
      sample.raw = sample.front;
      sample.target = 3;
    }
  }

  void rearm(uint32_t now, int raw) {
    if (armed_ || capturing_) return;
    if (now - last_candidate_ < config_.cooldown_ms || raw < 0) return;
    if (raw > config_.rearm) {
      quiet_since_ = 0;
      return;
    }
    if (quiet_since_ == 0) {
      quiet_since_ = now;
      return;
    }
    if (now - quiet_since_ >= config_.rearm_stable_ms) {
      armed_ = true;
      quiet_since_ = 0;
    }
  }

  void reset() {
    armed_ = false;
    capturing_ = false;
    capture_started_ = 0;
    peak_ = 0;
    peak_target_ = 1;
    last_candidate_ = 0;
    quiet_since_ = 0;
    left_ = ChannelState{};
    right_ = ChannelState{};
    front_ = ChannelState{};
  }

  Config config_;
  bool enabled_ = false;
  bool armed_ = false;
  bool capturing_ = false;
  uint32_t capture_started_ = 0;
  int peak_ = 0;
  int peak_target_ = 1;
  uint32_t last_candidate_ = 0;
  uint32_t quiet_since_ = 0;
  ChannelState left_;
  ChannelState right_;
  ChannelState front_;
};

}  // namespace go2_full_reference
