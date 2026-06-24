#include "reboot_marker.h"

#include <bb_esp_ota/reboot_marker.h>

namespace battlebang {
namespace boss_target {
namespace {
constexpr const char* kNamespace = "bb_boss_target";
constexpr const char* kKey = "ota_reboot";
}

bool writeOtaRebootMarker(bool value) {
  return battlebang::esp::ota::writeRebootMarker(kNamespace, kKey, value);
}

bool consumeOtaRebootMarker() {
  return battlebang::esp::ota::consumeRebootMarker(kNamespace, kKey);
}

}  // namespace boss_target
}  // namespace battlebang
