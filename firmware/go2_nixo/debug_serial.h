#pragma once

#include <Arduino.h>

#ifndef BB_GO2_NIXO_JETSON_USB_SERIAL
#define BB_GO2_NIXO_JETSON_USB_SERIAL 0
#endif

namespace battlebang::go2_nixo {

class NullDebugSerial final : public Print {
 public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t length) override { return length; }
};

inline Print& debugSerial() {
#if BB_GO2_NIXO_JETSON_USB_SERIAL
  static NullDebugSerial sink;
  return sink;
#else
  return Serial;
#endif
}

}  // namespace battlebang::go2_nixo

#define BB_DEBUG_SERIAL (::battlebang::go2_nixo::debugSerial())
