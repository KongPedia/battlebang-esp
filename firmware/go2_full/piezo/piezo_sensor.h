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
  struct Input {
    int8_t adc1Channel = -1;  // -1: not ADC1, sampled with analogRead()
    bool pulldown = false;    // internal RTC pull-down enabled on the pad
  };

  static Input configureInput(const PiezoSensorConfig& config, uint8_t channel, int pin);
  static int readInput(const Input& input, int pin);
  PiezoSample readSample() const;

  PiezoSensorCore core_;
  Input left_;
  Input right_;
  Input front_;
};

}  // namespace go2
