#include "go2_full/piezo/piezo_sensor.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <driver/rtc_io.h>

#include "go2_full/debug_serial.h"

namespace go2 {
namespace {

// ADC1 channel for a GPIO, or -1 when the pin is not on ADC1 (ADC2 or none).
int8_t adc1ChannelForPin(int pin) {
  if (pin < 0) return -1;
  const int8_t channel = digitalPinToAnalogChannel(static_cast<uint8_t>(pin));
  return (channel >= 0 && channel < SOC_ADC_MAX_CHANNEL_NUM) ? channel : -1;
}

}  // namespace

PiezoSensor::Input PiezoSensor::configureInput(const PiezoSensorConfig& config,
                                               uint8_t channel,
                                               int pin) {
  Input input;
  if ((config.channelEnableMask & channel) == 0 || pin < 0) return input;
  input.adc1Channel = adc1ChannelForPin(pin);
  if (input.adc1Channel < 0) {
    // Not an ADC1 pin: keep the previous Arduino analogRead path.
    pinMode(pin, INPUT);
#if defined(ADC_11db)
    analogSetPinAttenuation(pin, ADC_11db);
#endif
    return input;
  }
  // adc1_config_channel_atten() reinitializes the RTC pad and clears pulls, so
  // the pull-down must be applied after it. Sampling then uses adc1_get_raw(),
  // which leaves the pad pulls untouched (analogRead/pinMode would clear them).
  adc1_config_channel_atten(static_cast<adc1_channel_t>(input.adc1Channel), ADC_ATTEN_DB_12);  // == ADC_11db (3)
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  // GPIO34-39 are input-only pads without internal pull resistors.
  if (pin < 34 && rtc_gpio_is_valid_gpio(gpio)) {
    rtc_gpio_pullup_dis(gpio);
    input.pulldown = rtc_gpio_pulldown_en(gpio) == ESP_OK;
  }
  return input;
}

int PiezoSensor::readInput(const Input& input, int pin) {
  if (input.adc1Channel >= 0) {
    return adc1_get_raw(static_cast<adc1_channel_t>(input.adc1Channel));
  }
  return analogRead(pin);
}

void PiezoSensor::begin(const PiezoSensorConfig& config, bool enabled, uint32_t now) {
  analogReadResolution(12);
  adc1_config_width(ADC_WIDTH_BIT_12);
  left_ = configureInput(config, PIEZO_CHANNEL_LEFT, config.leftPin);
  right_ = configureInput(config, PIEZO_CHANNEL_RIGHT, config.rightPin);
  front_ = configureInput(config, PIEZO_CHANNEL_FRONT, config.frontPin);
  if (config.debugDoPin >= 0) pinMode(config.debugDoPin, INPUT_PULLDOWN);

  // Initialize once before reading so readSample can use the configured pins.
  core_.begin(config, enabled, PiezoSample{}, now);
  core_.setEnabled(enabled, readSample(), now);
  const PiezoDiagnostics state = core_.diagnostics();
  if (!state.available) {
    BB_DEBUG_SERIAL.println("[PIEZO AO] disabled: no enabled channel has a valid pin");
    return;
  }
  BB_DEBUG_SERIAL.printf("[PIEZO AO] ADC threshold mode mask=%u left=%d right=%d front=%d pulldown=%u/%u/%u threshold=%d rearm_raw=%d capture_window_ms=%lu cooldown_ms=%lu debug_period_ms=%lu initial_raw=%d left_raw=%d right_raw=%d front_raw=%d do_pin=%d do=%d\n",
                         config.channelEnableMask,
                         config.leftPin,
                         config.rightPin,
                         config.frontPin,
                         left_.pulldown ? 1U : 0U,
                         right_.pulldown ? 1U : 0U,
                         front_.pulldown ? 1U : 0U,
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
    sample.left = readInput(left_, config.leftPin);
  }
  if (core_.channelEnabled(PIEZO_CHANNEL_RIGHT) && config.rightPin >= 0) {
    sample.right = readInput(right_, config.rightPin);
  }
  if (core_.channelEnabled(PIEZO_CHANNEL_FRONT) && config.frontPin >= 0) {
    sample.front = readInput(front_, config.frontPin);
  }
  return sample;
}

int PiezoSensor::readDebugDoLevel() const {
  const int pin = core_.config().debugDoPin;
  return pin >= 0 ? digitalRead(pin) : -1;
}

}  // namespace go2
