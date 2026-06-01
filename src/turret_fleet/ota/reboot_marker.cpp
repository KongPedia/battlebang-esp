#include "reboot_marker.h"

#include <Arduino.h>
#include <Preferences.h>

namespace battlebang {
namespace turret_fleet {
namespace {

const char* kSafetyPrefsNamespace = "bb_fleet";
const char* kOtaRebootMarkerKey = "ota_reboot";

}  // namespace

void writeOtaRebootMarker(bool active) {
  Preferences prefs;
  if (!prefs.begin(kSafetyPrefsNamespace, false)) {
    Serial.println("[fleet][ota] reboot marker NVS open failed");
    return;
  }
  prefs.putBool(kOtaRebootMarkerKey, active);
  prefs.end();
}

bool consumeOtaRebootMarker() {
  Preferences prefs;
  if (!prefs.begin(kSafetyPrefsNamespace, false)) return false;
  const bool active = prefs.getBool(kOtaRebootMarkerKey, false);
  if (active) prefs.putBool(kOtaRebootMarkerKey, false);
  prefs.end();
  return active;
}

}  // namespace turret_fleet
}  // namespace battlebang
