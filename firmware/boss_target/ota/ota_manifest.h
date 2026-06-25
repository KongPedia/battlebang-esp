#pragma once

#include <bb_esp_ota/ota_manifest.h>

namespace battlebang {
namespace boss_target {

using OtaManifest = battlebang::esp::ota::OtaManifest;

bool parseOtaManifestJson(const char* json, OtaManifest& manifest, String& error);
bool shouldApplyOtaManifest(const OtaManifest& manifest, String& reason);
String otaManifestSummary(const OtaManifest& manifest);

}  // namespace boss_target
}  // namespace battlebang
