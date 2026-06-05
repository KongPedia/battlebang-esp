#pragma once

#include <Arduino.h>

namespace battlebang {
namespace hit_target {

struct DeviceIdentity {
  String deviceId;
  String targetId;
  String mac;
};

DeviceIdentity buildDeviceIdentity(const char* targetPrefix = "hit_target");
String buildDeviceIdFromMac(const uint8_t macBytes[6]);
String formatMac(const uint8_t macBytes[6]);

}  // namespace hit_target
}  // namespace battlebang
