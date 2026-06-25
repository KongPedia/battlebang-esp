#pragma once

#include <Arduino.h>

#include <bb_esp_ota/ota_manifest.h>

namespace battlebang::esp::ota {

struct OtaResult {
  bool ok = false;
  String message;
};

bool fetchHttpText(const String& url, size_t maxBytes, String& body, String& error);
OtaResult runHttpOta(const OtaManifest& manifest);

}  // namespace battlebang::esp::ota
