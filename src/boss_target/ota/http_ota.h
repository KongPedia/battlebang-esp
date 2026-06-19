#pragma once

#include <Arduino.h>

#include "ota_manifest.h"

namespace battlebang {
namespace boss_target {

struct OtaResult {
  bool ok = false;
  String message;
};

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error);
OtaResult runHttpOta(const OtaManifest& manifest);

}  // namespace boss_target
}  // namespace battlebang
