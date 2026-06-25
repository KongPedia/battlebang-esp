#pragma once

#include <Arduino.h>
#include <bb_esp_net/wifi_manager.h>

#include "heavy_blaster/config/runtime_config.h"

namespace battlebang {
namespace heavy_blaster {

class WifiManager {
 public:
  WifiManager();

  void begin(const RuntimeConfig& config);
  void stop();
  void loop(const RuntimeConfig& config);
  bool connected() const;
  String ip() const;
  int32_t rssi() const;

 private:
  battlebang::esp::net::WifiManager wifi_;

  static battlebang::esp::net::WifiCredentials credentials(const RuntimeConfig& config);
};

}  // namespace heavy_blaster
}  // namespace battlebang
