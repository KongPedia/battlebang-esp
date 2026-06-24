#include "ota_manifest.h"

#include "boss_target/app/firmware_info.h"

namespace battlebang {
namespace boss_target {
namespace {

battlebang::esp::app::FirmwareIdentity firmwareIdentity() {
  battlebang::esp::app::FirmwareIdentity identity;
  identity.app = BB_BOSS_TARGET_APP_NAME;
  identity.hardware = BB_BOSS_TARGET_HARDWARE;
  identity.build = BB_BOSS_TARGET_BUILD;
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

}  // namespace boss_target
}  // namespace battlebang
