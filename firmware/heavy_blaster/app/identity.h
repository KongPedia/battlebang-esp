#pragma once

#include <Arduino.h>

namespace battlebang {
namespace heavy_blaster {

struct DeviceIdentity {
  String deviceId;
  String blasterId;
  String mac;
};

DeviceIdentity buildDeviceIdentity(const char* blasterPrefix = "heavy-blaster");
String buildDeviceIdFromMac(const uint8_t macBytes[6]);
String formatMac(const uint8_t macBytes[6]);

}  // namespace heavy_blaster
}  // namespace battlebang
