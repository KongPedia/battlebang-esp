#include "identity.h"

#include <Esp.h>

namespace battlebang {
namespace turret_fleet {

String buildDeviceId() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "esp32-%012llx", static_cast<unsigned long long>(mac));
  return String(buf);
}

}  // namespace turret_fleet
}  // namespace battlebang
