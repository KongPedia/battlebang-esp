#pragma once

namespace battlebang {
namespace turret_fleet {

void writeOtaRebootMarker(bool active);
bool consumeOtaRebootMarker();

}  // namespace turret_fleet
}  // namespace battlebang
