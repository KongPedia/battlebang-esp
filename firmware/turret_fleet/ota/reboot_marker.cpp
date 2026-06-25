#include "reboot_marker.h"

#include <Arduino.h>
#include <bb_esp_ota/reboot_marker.h>

namespace battlebang {
namespace turret_fleet {
namespace {

const char* kSafetyPrefsNamespace = "bb_fleet";
const char* kOtaRebootMarkerKey = "ota_reboot";

}  // namespace

void writeOtaRebootMarker(bool active) {
  if (!battlebang::esp::ota::writeRebootMarker(kSafetyPrefsNamespace, kOtaRebootMarkerKey, active)) {
    Serial.println("[fleet][ota] reboot marker NVS open failed");
  }
}

bool consumeOtaRebootMarker() {
  return battlebang::esp::ota::consumeRebootMarker(kSafetyPrefsNamespace, kOtaRebootMarkerKey);
}

}  // namespace turret_fleet
}  // namespace battlebang
