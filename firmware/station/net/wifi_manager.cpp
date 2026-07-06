#include "wifi_manager.h"

namespace battlebang {
namespace station {

WifiManager::WifiManager() : wifi_("[station][wifi]") {}

void WifiManager::begin(const RuntimeConfig& config) {
  wifi_.begin(credentials(config));
}

void WifiManager::stop() {
  wifi_.stop();
}

void WifiManager::loop(const RuntimeConfig& config) {
  wifi_.loop(credentials(config));
}

bool WifiManager::connected() const {
  return wifi_.connected();
}

String WifiManager::ip() const {
  return wifi_.ip();
}

int32_t WifiManager::rssi() const {
  return wifi_.rssi();
}

battlebang::esp::net::WifiCredentials WifiManager::credentials(const RuntimeConfig& config) {
  return battlebang::esp::net::WifiCredentials{config.wifiSsid, config.wifiPassword};
}

}  // namespace station
}  // namespace battlebang
