#pragma once

#include <Arduino.h>

namespace battlebang {
namespace boss_target {

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

bool parseOtaManifestJson(const char* json, OtaManifest& manifest, String& error);
bool shouldApplyOtaManifest(const OtaManifest& manifest, String& reason);
String otaManifestSummary(const OtaManifest& manifest);

}  // namespace boss_target
}  // namespace battlebang
