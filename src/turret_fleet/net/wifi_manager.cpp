#include "wifi_manager.h"

#include <WiFi.h>

namespace battlebang {
namespace turret_fleet {
namespace {
const unsigned long kRetryIntervalMs = 10000;
}

void WifiManager::begin(const RuntimeConfig& config) {
  warnedMissingConfig_ = false;
  lastAttemptMs_ = 0;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  loop(config);
}

void WifiManager::stop() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  lastAttemptMs_ = 0;
  warnedMissingConfig_ = false;
  Serial.println("[fleet][wifi] stopped");
}

void WifiManager::loop(const RuntimeConfig& config) {
  if (connected()) return;

  if (config.wifiSsid.length() == 0) {
    if (!warnedMissingConfig_) {
      Serial.println("[fleet][wifi] missing wifi.ssid; provision config first");
      warnedMissingConfig_ = true;
    }
    return;
  }

  const unsigned long now = millis();
  if (lastAttemptMs_ != 0 && now - lastAttemptMs_ < kRetryIntervalMs) return;
  lastAttemptMs_ = now;

  Serial.println("[fleet][wifi] connecting to configured SSID");
  WiFi.disconnect(false, false);
  if (config.wifiPassword.length() == 0) {
    WiFi.begin(config.wifiSsid.c_str());
  } else {
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  }
}

bool WifiManager::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ip() const {
  return connected() ? WiFi.localIP().toString() : String("");
}

int32_t WifiManager::rssi() const {
  return connected() ? WiFi.RSSI() : 0;
}

}  // namespace turret_fleet
}  // namespace battlebang
