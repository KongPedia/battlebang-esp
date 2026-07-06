#pragma once

#include <Arduino.h>

namespace battlebang {
namespace station {

struct DeviceIdentity {
  String deviceId;
  String stationId;
  String mac;
};

DeviceIdentity buildDeviceIdentity(const char* stationPrefix = "station");
String buildDeviceIdFromMac(const uint8_t macBytes[6]);
String formatMac(const uint8_t macBytes[6]);

}  // namespace station
}  // namespace battlebang
