#pragma once

#include <Arduino.h>

#include "ota_manifest.h"

namespace battlebang {
namespace turret_fleet {

struct OtaResult {
  bool ok = false;
  String message;
};

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error);
OtaResult runHttpOta(const OtaManifest& manifest);

}  // namespace turret_fleet
}  // namespace battlebang
