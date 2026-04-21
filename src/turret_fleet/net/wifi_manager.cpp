#include "wifi_manager.h"

#include <WiFi.h>

namespace battlebang {
namespace turret_fleet {
namespace {
const unsigned long kRetryIntervalMs = 10000;

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "IDLE";
    case WL_NO_SSID_AVAIL:
      return "NO_SSID";
    case WL_SCAN_COMPLETED:
      return "SCAN_COMPLETED";
    case WL_CONNECTED:
      return "CONNECTED";
    case WL_CONNECT_FAILED:
      return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}
}

void WifiManager::begin(const RuntimeConfig& config) {
  warnedMissingConfig_ = false;
  lastAttemptMs_ = 0;
  lastStatus_ = -1;
  printedConnected_ = false;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  loop(config);
}

void WifiManager::loop(const RuntimeConfig& config) {
  const wl_status_t status = WiFi.status();
  if (status != lastStatus_) {
    Serial.print("[fleet][wifi] status=");
    Serial.print(wifiStatusName(status));
    Serial.print(" code=");
    Serial.println(static_cast<int>(status));
    lastStatus_ = status;
  }

  if (status == WL_CONNECTED) {
    if (!printedConnected_) {
      printStatus(config, "connected");
      printedConnected_ = true;
    }
    return;
  }

  printedConnected_ = false;

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

  Serial.print("[fleet][wifi] connecting to ");
  Serial.println(config.wifiSsid);
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

String WifiManager::gateway() const {
  return connected() ? WiFi.gatewayIP().toString() : String("");
}

String WifiManager::subnet() const {
  return connected() ? WiFi.subnetMask().toString() : String("");
}

String WifiManager::dns() const {
  return connected() ? WiFi.dnsIP().toString() : String("");
}

int32_t WifiManager::rssi() const {
  return connected() ? WiFi.RSSI() : 0;
}

String WifiManager::statusName() const {
  return wifiStatusName(WiFi.status());
}

String WifiManager::statusSummary(const RuntimeConfig& config) const {
  String out;
  out.reserve(256);
  out += "status=";
  out += statusName();
  out += " code=";
  out += String(static_cast<int>(WiFi.status()));
  out += " ssid=";
  out += (config.wifiSsid.length() > 0 ? config.wifiSsid : String("<missing>"));
  out += " ip=";
  out += (connected() ? ip() : String("<none>"));
  out += " gateway=";
  out += (connected() ? gateway() : String("<none>"));
  out += " subnet=";
  out += (connected() ? subnet() : String("<none>"));
  out += " dns=";
  out += (connected() ? dns() : String("<none>"));
  out += " rssi=";
  out += String(rssi());
  out += " bssid=";
  out += (connected() ? WiFi.BSSIDstr() : String("<none>"));
  return out;
}

void WifiManager::printStatus(const RuntimeConfig& config, const char* reason) const {
  Serial.print("[fleet][wifi] ");
  Serial.print(reason);
  Serial.print(' ');
  Serial.println(statusSummary(config));
}

}  // namespace turret_fleet
}  // namespace battlebang
