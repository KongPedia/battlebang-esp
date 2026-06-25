#include <bb_esp_ota/reboot_marker.h>

#include <Preferences.h>

namespace battlebang::esp::ota {

bool writeRebootMarker(const char* nvsNamespace, const char* key, bool value) {
  Preferences prefs;
  if (!prefs.begin(nvsNamespace, false)) return false;
  const bool ok = prefs.putBool(key, value) > 0;
  prefs.end();
  return ok;
}

bool consumeRebootMarker(const char* nvsNamespace, const char* key) {
  Preferences prefs;
  if (!prefs.begin(nvsNamespace, false)) return false;
  const bool value = prefs.getBool(key, false);
  if (value) prefs.putBool(key, false);
  prefs.end();
  return value;
}

}  // namespace battlebang::esp::ota
