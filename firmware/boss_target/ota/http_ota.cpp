#include "http_ota.h"

namespace battlebang {
namespace boss_target {

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error) {
  return battlebang::esp::ota::fetchHttpText(url, maxBytes, body, error);
}

OtaResult runHttpOta(const OtaManifest& manifest) {
  return battlebang::esp::ota::runHttpOta(manifest);
}

}  // namespace boss_target
}  // namespace battlebang
