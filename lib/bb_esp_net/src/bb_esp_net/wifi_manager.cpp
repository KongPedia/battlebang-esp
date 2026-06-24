#include <bb_esp_net/wifi_manager.h>

#include <WiFi.h>

namespace battlebang::esp::net {

WifiManager::WifiManager(const char* logPrefix, uint32_t retryIntervalMs)
    : logPrefix_(logPrefix == nullptr ? "[wifi]" : logPrefix), retryIntervalMs_(retryIntervalMs) {}

void WifiManager::begin(const WifiCredentials& credentials) {
  warnedMissingConfig_ = false;
  lastAttemptMs_ = 0;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  loop(credentials);
}

void WifiManager::stop() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  lastAttemptMs_ = 0;
  warnedMissingConfig_ = false;
  println(" stopped");
}

void WifiManager::loop(const WifiCredentials& credentials) {
  if (connected()) return;

  if (credentials.ssid.length() == 0) {
    if (!warnedMissingConfig_) {
      println(" missing wifi.ssid; provision config first");
      warnedMissingConfig_ = true;
    }
    return;
  }

  const uint32_t now = millis();
  if (lastAttemptMs_ != 0 && now - lastAttemptMs_ < retryIntervalMs_) return;
  lastAttemptMs_ = now;

  println(" connecting to configured SSID");
  WiFi.disconnect(false, false);
  if (credentials.password.length() == 0) {
    WiFi.begin(credentials.ssid.c_str());
  } else {
    WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
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

void WifiManager::println(const char* message) const {
  Serial.print(logPrefix_);
  Serial.println(message);
}

}  // namespace battlebang::esp::net
