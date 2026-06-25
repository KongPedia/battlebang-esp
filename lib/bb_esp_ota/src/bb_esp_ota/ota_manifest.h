#pragma once

#include <Arduino.h>

#include <bb_esp_core/app/firmware_identity.h>

namespace battlebang::esp::ota {

struct OtaManifest {
  String type;
  String jobId;
  String channel;
  String app;
  String hardware;
  String version;
  uint32_t build = 0;
  String url;
  String sha256;
  uint32_t size = 0;
  bool force = false;
};

bool parseManifestJson(const char* json, OtaManifest& manifest, String& error);
bool shouldApplyManifest(const OtaManifest& manifest,
                            const battlebang::esp::app::FirmwareIdentity& identity,
                            String& reason);
String manifestSummary(const OtaManifest& manifest);

}  // namespace battlebang::esp::ota
