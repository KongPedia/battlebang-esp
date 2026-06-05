#pragma once

#include <Arduino.h>

#include "ota_manifest.h"

namespace battlebang {
namespace hit_target {

struct OtaResult {
  bool ok = false;
  String message;
};

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error);
OtaResult runHttpOta(const OtaManifest& manifest);

}  // namespace hit_target
}  // namespace battlebang
