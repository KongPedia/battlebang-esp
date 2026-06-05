#pragma once

#include <Arduino.h>

#include "hit_target/config/runtime_config.h"

namespace battlebang {
namespace hit_target {

class WifiManager {
 public:
  void begin(const RuntimeConfig& config);
  void stop();
  void loop(const RuntimeConfig& config);
  bool connected() const;
  String ip() const;
  int32_t rssi() const;

 private:
  unsigned long lastAttemptMs_ = 0;
  bool warnedMissingConfig_ = false;
};

}  // namespace hit_target
}  // namespace battlebang
