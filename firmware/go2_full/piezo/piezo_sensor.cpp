#include "go2_full/piezo/piezo_sensor.h"

#include <Arduino.h>

#include "go2_full/debug_serial.h"

namespace go2 {

void PiezoSensor::begin(const PiezoSensorConfig& config, bool enabled, uint32_t now) {
  if ((config.channelEnableMask & PIEZO_CHANNEL_LEFT) != 0 && config.leftPin >= 0) {
    pinMode(config.leftPin, INPUT);
  }
  if ((config.channelEnableMask & PIEZO_CHANNEL_RIGHT) != 0 && config.rightPin >= 0) {
    pinMode(config.rightPin, INPUT);
  }
  if ((config.channelEnableMask & PIEZO_CHANNEL_FRONT) != 0 && config.frontPin >= 0) {
    pinMode(config.frontPin, INPUT);
  }
  analogReadResolution(12);
#if defined(ADC_11db)
  if ((config.channelEnableMask & PIEZO_CHANNEL_LEFT) != 0 && config.leftPin >= 0) {
    analogSetPinAttenuation(config.leftPin, ADC_11db);
  }
  if ((config.channelEnableMask & PIEZO_CHANNEL_RIGHT) != 0 && config.rightPin >= 0) {
    analogSetPinAttenuation(config.rightPin, ADC_11db);
  }
  if ((config.channelEnableMask & PIEZO_CHANNEL_FRONT) != 0 && config.frontPin >= 0) {
    analogSetPinAttenuation(config.frontPin, ADC_11db);
  }
#endif
  if (config.debugDoPin >= 0) pinMode(config.debugDoPin, INPUT_PULLDOWN);

  // Initialize once before reading so readSample can use the configured pins.
  core_.begin(config, enabled, PiezoSample{}, now);
  core_.setEnabled(enabled, readSample(), now);
  const PiezoDiagnostics state = core_.diagnostics();
  if (!state.available) {
    BB_DEBUG_SERIAL.println("[PIEZO AO] disabled: no enabled channel has a valid pin");
    return;
  }
  BB_DEBUG_SERIAL.printf("[PIEZO AO] ADC threshold mode mask=%u left=%d right=%d front=%d threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu debug_period_ms=%lu initial_raw=%d left_raw=%d right_raw=%d front_raw=%d do_pin=%d do=%d\n",
                         config.channelEnableMask,
                         config.leftPin,
                         config.rightPin,
                         config.frontPin,
                         config.thresholdRaw,
                         config.rearmRaw,
                         (unsigned long)config.captureWindowMs,
                         (unsigned long)config.hitCooldownMs,
                         (unsigned long)config.debugPeriodMs,
                         state.lastRaw,
                         state.left.raw,
                         state.right.raw,
                         state.front.raw,
                         config.debugDoPin,
                         readDebugDoLevel());
}

void PiezoSensor::setEnabled(bool enabled, uint32_t now) {
  // Match the original edge behavior: disabling resets with an empty sample;
  // enabling records current channel values but still requires quiet qualification.
  PiezoSample sample;
  if (enabled) {
    const bool wasEnabled = core_.enabled();
    if (!wasEnabled) core_.setEnabled(true, PiezoSample{}, now);
    sample = readSample();
  }
  core_.setEnabled(enabled, sample, now);
}

bool PiezoSensor::poll(uint32_t now, PiezoHitCandidate& candidate) {
  if (!core_.enabled() || !core_.available()) return false;
  const bool emitted = core_.processSample(readSample(), now, candidate);
  const PiezoProcessingEvents& events = core_.lastEvents();
  if ((events.qualifiedMask & PIEZO_CHANNEL_LEFT) != 0) {
    BB_DEBUG_SERIAL.printf("[PIEZO AO] channel qualified name=left raw=%d quiet_ms=%lu\n",
                           events.sample.left, (unsigned long)core_.config().rearmStableMs);
  }
  if ((events.qualifiedMask & PIEZO_CHANNEL_RIGHT) != 0) {
    BB_DEBUG_SERIAL.printf("[PIEZO AO] channel qualified name=right raw=%d quiet_ms=%lu\n",
                           events.sample.right, (unsigned long)core_.config().rearmStableMs);
  }
  if ((events.qualifiedMask & PIEZO_CHANNEL_FRONT) != 0) {
    BB_DEBUG_SERIAL.printf("[PIEZO AO] channel qualified name=front raw=%d quiet_ms=%lu\n",
                           events.sample.front, (unsigned long)core_.config().rearmStableMs);
  }
  if (events.rearmed) {
    BB_DEBUG_SERIAL.printf("[PIEZO AO] rearmed raw=%d quiet_ms=%lu\n",
                           events.sample.raw, (unsigned long)core_.config().rearmStableMs);
  }
  if (events.thresholdCrossed) {
    BB_DEBUG_SERIAL.printf("[PIEZO AO] threshold crossed source=%s raw=%d left=%d right=%d front=%d threshold=%d ms=%lu do=%d\n",
                           events.sample.source,
                           events.sample.raw,
                           events.sample.left,
                           events.sample.right,
                           events.sample.front,
                           core_.config().thresholdRaw,
                           (unsigned long)now,
                           readDebugDoLevel());
  }
  PiezoDiagnostics debug;
  if (core_.takeDebugSnapshot(now, debug)) {
    BB_DEBUG_SERIAL.printf("[PIEZO AO] ms=%lu raw=%d left=%d right=%d front=%d enabled=%u qualified=%u/%u/%u min=%d max=%d avg=%lu threshold=%d rearm=%d armed=%s capturing=%s do_pin=%d do=%d\n",
                           (unsigned long)now,
                           debug.lastRaw,
                           debug.left.raw,
                           debug.right.raw,
                           debug.front.raw,
                           debug.channelEnableMask,
                           debug.left.qualified ? 1U : 0U,
                           debug.right.qualified ? 1U : 0U,
                           debug.front.qualified ? 1U : 0U,
                           debug.minRaw,
                           debug.maxRaw,
                           (unsigned long)debug.averageRaw,
                           core_.config().thresholdRaw,
                           core_.config().rearmRaw,
                           debug.armed ? "true" : "false",
                           debug.capturing ? "true" : "false",
                           core_.config().debugDoPin,
                           readDebugDoLevel());
  }
  return emitted;
}

PiezoSample PiezoSensor::readSample() const {
  PiezoSample sample;
  if (!core_.enabled() || !core_.available()) return sample;
  const PiezoSensorConfig& config = core_.config();
  if (core_.channelEnabled(PIEZO_CHANNEL_LEFT) && config.leftPin >= 0) {
    sample.left = analogRead(config.leftPin);
  }
  if (core_.channelEnabled(PIEZO_CHANNEL_RIGHT) && config.rightPin >= 0) {
    sample.right = analogRead(config.rightPin);
  }
  if (core_.channelEnabled(PIEZO_CHANNEL_FRONT) && config.frontPin >= 0) {
    sample.front = analogRead(config.frontPin);
  }
  return sample;
}

int PiezoSensor::readDebugDoLevel() const {
  const int pin = core_.config().debugDoPin;
  return pin >= 0 ? digitalRead(pin) : -1;
}

}  // namespace go2
