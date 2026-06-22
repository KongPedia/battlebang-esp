#pragma once

#include <Arduino.h>

#include "heavy-blaster/config/runtime_config.h"

namespace battlebang {
namespace heavy_blaster {

class WifiManager {
 public:
  void begin(const RuntimeConfig& config);
  void stop();
  void loop(const RuntimeConfig& config);
  bool connected() const;
  String ip() const;
  int32_t rssi() const;

 private:
  uint32_t lastAttemptMs_ = 0;
  bool warnedMissingConfig_ = false;
};

}  // namespace heavy_blaster
}  // namespace battlebang
