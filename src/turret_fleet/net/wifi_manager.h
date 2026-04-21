#pragma once

#include <Arduino.h>

#include "../config/runtime_config.h"

namespace battlebang {
namespace turret_fleet {

class WifiManager {
 public:
  void begin(const RuntimeConfig& config);
  void loop(const RuntimeConfig& config);
  bool connected() const;
  String ip() const;
  String gateway() const;
  String subnet() const;
  String dns() const;
  int32_t rssi() const;
  String statusName() const;
  String statusSummary(const RuntimeConfig& config) const;
  void printStatus(const RuntimeConfig& config, const char* reason) const;

 private:
  unsigned long lastAttemptMs_ = 0;
  bool warnedMissingConfig_ = false;
  mutable int lastStatus_ = -1;
  bool printedConnected_ = false;
};

}  // namespace turret_fleet
}  // namespace battlebang
