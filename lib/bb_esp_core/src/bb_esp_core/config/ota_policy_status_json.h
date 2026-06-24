#pragma once

#include <ArduinoJson.h>

#include <bb_esp_core/config/common_runtime_config.h>

namespace battlebang::esp::config {

inline void writeUnsupportedOtaStatus(JsonObject doc,
                                      const CommonRuntimeConfig& config,
                                      const char* reason) {
  doc["ota_supported"] = false;
  doc["ota_state"] = "unsupported";
  if (reason != nullptr && reason[0] != '\0') {
    doc["ota_reason"] = reason;
  }
  doc["ota_policy_command_center_controlled"] = config.otaCommandCenterControlled;
  doc["ota_policy_auto_check_enabled"] = config.otaAutoCheckEnabled;
  doc["ota_policy_channel"] = config.otaChannel;
  doc["ota_policy_desired_build"] = config.otaDesiredBuild;
  doc["ota_policy_apply_only_in_safe_state"] = config.otaApplyOnlyInSafeState;
}

}  // namespace battlebang::esp::config
