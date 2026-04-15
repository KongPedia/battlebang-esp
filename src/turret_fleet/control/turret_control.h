#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../config/runtime_config.h"

namespace battlebang {
namespace turret_fleet {

class TurretControl {
 public:
  void begin(const RuntimeConfig& config);
  void applyConfig(const RuntimeConfig& config);
  void loop();
  void handleCommandJson(JsonDocument& doc, const char* source);
  const char* mode() const;

 private:
  String turretId_;
  String mode_ = "BOOT";
};

}  // namespace turret_fleet
}  // namespace battlebang
