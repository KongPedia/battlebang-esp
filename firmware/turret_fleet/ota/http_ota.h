#pragma once

#include <bb_esp_ota/http_ota.h>

#include "ota_manifest.h"

namespace battlebang {
namespace turret_fleet {

using OtaResult = battlebang::esp::ota::OtaResult;

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error);
OtaResult runHttpOta(const OtaManifest& manifest);

}  // namespace turret_fleet
}  // namespace battlebang
