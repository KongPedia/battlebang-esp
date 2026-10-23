#pragma once

#include <stdint.h>

namespace go2 {

// Volatile physical opt-in: never persist arming across reset or OTA.
class PiezoEnableSwitch {
 public:
  void begin(bool pressed, uint32_t now) {
    enabled_ = false;
    rawPressed_ = stablePressed_ = pressed;
    releaseRequired_ = pressed;
    changedAt_ = now;
  }

  bool update(bool pressed, uint32_t now) {
    if (pressed != rawPressed_) {
      rawPressed_ = pressed;
      changedAt_ = now;
    }
    if (pressed == stablePressed_ || uint32_t(now - changedAt_) < 50) return false;
    stablePressed_ = pressed;
    if (!pressed) {
      releaseRequired_ = false;
      return false;
    }
    if (releaseRequired_) return false;
    enabled_ = !enabled_;
    return true;
  }

  bool enabled() const { return enabled_; }
  bool pressed() const { return stablePressed_; }

 private:
  bool enabled_ = false;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool releaseRequired_ = false;
  uint32_t changedAt_ = 0;
};

}  // namespace go2
