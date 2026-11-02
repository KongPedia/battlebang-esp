#pragma once

#include <stdint.h>

#include "go2_full/piezo/piezo_sensor_core.h"

namespace go2 {

class PiezoSensor {
 public:
  void begin(const PiezoSensorConfig& config, bool enabled, uint32_t now);
  void setEnabled(bool enabled, uint32_t now);
  bool poll(uint32_t now, PiezoHitCandidate& candidate);

  bool enabled() const { return core_.enabled(); }
  bool available() const { return core_.available(); }
  bool channelEnabled(uint8_t channel) const { return core_.channelEnabled(channel); }
  int readDebugDoLevel() const;
  PiezoDiagnostics diagnostics() const { return core_.diagnostics(); }
  bool takeDebugSnapshot(uint32_t now, PiezoDiagnostics& snapshot) {
    return core_.takeDebugSnapshot(now, snapshot);
  }
  const PiezoProcessingEvents& lastEvents() const { return core_.lastEvents(); }
  const PiezoSensorConfig& config() const { return core_.config(); }

 private:
  PiezoSample readSample() const;

  PiezoSensorCore core_;
};

}  // namespace go2
