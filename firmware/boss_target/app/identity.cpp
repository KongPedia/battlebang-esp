#include "identity.h"

#include <Esp.h>

namespace battlebang {
namespace boss_target {

String formatMac(const uint8_t macBytes[6]) {
  char buf[18];
  snprintf(buf,
           sizeof(buf),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           macBytes[0],
           macBytes[1],
           macBytes[2],
           macBytes[3],
           macBytes[4],
           macBytes[5]);
  return String(buf);
}

String buildDeviceIdFromMac(const uint8_t macBytes[6]) {
  char buf[24];
  snprintf(buf,
           sizeof(buf),
           "esp32-%02X%02X%02X%02X%02X%02X",
           macBytes[0],
           macBytes[1],
           macBytes[2],
           macBytes[3],
           macBytes[4],
           macBytes[5]);
  return String(buf);
}

DeviceIdentity buildDeviceIdentity(const char* targetPrefix) {
  const uint64_t efuseMac = ESP.getEfuseMac();
  // Keep this byte order aligned with esptool's printed MAC address.
  const uint8_t macBytes[6] = {
      static_cast<uint8_t>(efuseMac & 0xFF),
      static_cast<uint8_t>((efuseMac >> 8) & 0xFF),
      static_cast<uint8_t>((efuseMac >> 16) & 0xFF),
      static_cast<uint8_t>((efuseMac >> 24) & 0xFF),
      static_cast<uint8_t>((efuseMac >> 32) & 0xFF),
      static_cast<uint8_t>((efuseMac >> 40) & 0xFF),
  };

  char target[40];
  snprintf(target,
           sizeof(target),
           "%s_%02X%02X%02X%02X%02X%02X",
           targetPrefix,
           macBytes[0],
           macBytes[1],
           macBytes[2],
           macBytes[3],
           macBytes[4],
           macBytes[5]);

  DeviceIdentity identity;
  identity.mac = formatMac(macBytes);
  identity.deviceId = buildDeviceIdFromMac(macBytes);
  identity.targetId = String(target);
  return identity;
}

}  // namespace boss_target
}  // namespace battlebang
