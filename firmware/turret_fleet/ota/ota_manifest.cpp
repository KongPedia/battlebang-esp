#include "ota_manifest.h"

#include "../app/firmware_info.h"

namespace battlebang {
namespace turret_fleet {
namespace {

battlebang::esp::app::FirmwareIdentity firmwareIdentity() {
  battlebang::esp::app::FirmwareIdentity identity;
  identity.app = BB_TURRET_FLEET_APP_NAME;
  identity.hardware = BB_TURRET_FLEET_HARDWARE;
  identity.build = BB_TURRET_FLEET_BUILD;
  return identity;
}

}  // namespace

bool parseOtaManifestJson(const char* json, OtaManifest& manifest, String& error) {
  return battlebang::esp::ota::parseManifestJson(json, manifest, error);
}

bool shouldApplyOtaManifest(const OtaManifest& manifest, String& reason) {
  return battlebang::esp::ota::shouldApplyManifest(manifest, firmwareIdentity(), reason);
}

String otaManifestSummary(const OtaManifest& manifest) {
  return battlebang::esp::ota::manifestSummary(manifest);
}

}  // namespace turret_fleet
}  // namespace battlebang
