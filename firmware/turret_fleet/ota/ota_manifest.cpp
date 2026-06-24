#include "ota_manifest.h"

#include <ArduinoJson.h>

#include "../app/firmware_info.h"

namespace battlebang {
namespace turret_fleet {

bool parseOtaManifestJson(const char* json, OtaManifest& manifest, String& error) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid json: ") + err.c_str();
    return false;
  }

  manifest.type = doc["type"] | "firmware";
  manifest.jobId = doc["job_id"] | "";
  manifest.channel = doc["channel"] | "stable";
  manifest.app = doc["app"] | "";
  manifest.hardware = doc["hardware"] | "";
  manifest.version = doc["version"] | "";
  manifest.build = doc["build"] | 0;
  manifest.url = doc["url"] | "";
  manifest.sha256 = doc["sha256"] | "";
  manifest.size = doc["size"] | 0;
  manifest.force = doc["force"] | false;

  if (manifest.type != "firmware") {
    error = "manifest type must be firmware";
    return false;
  }
  if (manifest.app.length() == 0 || manifest.hardware.length() == 0 ||
      manifest.version.length() == 0 || manifest.build == 0 || manifest.url.length() == 0) {
    error = "manifest missing app/hardware/version/build/url";
    return false;
  }
  if (manifest.sha256.length() != 64) {
    error = "manifest sha256 must be 64 hex characters";
    return false;
  }
  error = "";
  return true;
}

bool shouldApplyOtaManifest(const OtaManifest& manifest, String& reason) {
  if (manifest.app != BB_TURRET_FLEET_APP_NAME) {
    reason = "app mismatch";
    return false;
  }
  if (manifest.hardware != BB_TURRET_FLEET_HARDWARE) {
    reason = "hardware mismatch";
    return false;
  }
  if (!manifest.force && manifest.build <= BB_TURRET_FLEET_BUILD) {
    reason = "build is not newer";
    return false;
  }
  reason = "update accepted";
  return true;
}

String otaManifestSummary(const OtaManifest& manifest) {
  return String("job=") + manifest.jobId + " app=" + manifest.app +
         " hardware=" + manifest.hardware + " version=" + manifest.version +
         " build=" + String(manifest.build) + " url=" + manifest.url;
}

}  // namespace turret_fleet
}  // namespace battlebang
