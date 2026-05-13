#include "turret_control.h"

namespace battlebang {
namespace turret_fleet {

void TurretControl::begin(const RuntimeConfig& config) {
  applyConfig(config);
  mode_ = config.configured ? "IDLE" : "UNCONFIGURED";
  Serial.print("[fleet][control] mode=");
  Serial.println(mode_);
}

void TurretControl::applyConfig(const RuntimeConfig& config) {
  turretId_ = config.turretId;
  Serial.print("[fleet][control] config applied turret_id=");
  Serial.print(config.turretId);
  Serial.print(" xyz_cm=(");
  Serial.print(config.xCm, 2);
  Serial.print(", ");
  Serial.print(config.yCm, 2);
  Serial.print(", ");
  Serial.print(config.zCm, 2);
  Serial.println(")");
}

void TurretControl::loop() {
  // Future work: port src/turret/runtime/control.inc behavior here module-by-module.
}

void TurretControl::handleCommandJson(JsonDocument& doc, const char* source) {
  const char* command = doc["command"] | "";
  if (strcmp(command, "idle") == 0) {
    mode_ = "IDLE";
  } else if (strcmp(command, "dead") == 0) {
    mode_ = "DEAD";
  } else if (strcmp(command, "target") == 0) {
    mode_ = "TARGET";
  } else if (strcmp(command, "fire") == 0) {
    mode_ = "FIRE";
  } else {
    Serial.print("[fleet][control] unsupported command from ");
    Serial.print(source);
    Serial.print(": ");
    Serial.println(command);
    return;
  }

  Serial.print("[fleet][control] command from ");
  Serial.print(source);
  Serial.print(" -> mode=");
  Serial.println(mode_);
}

const char* TurretControl::mode() const {
  return mode_.c_str();
}

}  // namespace turret_fleet
}  // namespace battlebang
