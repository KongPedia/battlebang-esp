#include "reboot_marker.h"

#include <Preferences.h>

namespace battlebang {
namespace hit_target {
namespace {
constexpr const char* kNamespace = "bb_hit_target";
constexpr const char* kKey = "ota_reboot";
}

bool writeOtaRebootMarker(bool value) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool ok = prefs.putBool(kKey, value) > 0;
  prefs.end();
  return ok;
}

bool consumeOtaRebootMarker() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool value = prefs.getBool(kKey, false);
  if (value) prefs.putBool(kKey, false);
  prefs.end();
  return value;
}

}  // namespace hit_target
}  // namespace battlebang
