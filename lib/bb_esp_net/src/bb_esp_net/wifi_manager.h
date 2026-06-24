#pragma once

#include <Arduino.h>

namespace battlebang::esp::net {

struct WifiCredentials {
  String ssid;
  String password;
};

class WifiManager {
 public:
  explicit WifiManager(const char* logPrefix = "[wifi]", uint32_t retryIntervalMs = 10000);

  void begin(const WifiCredentials& credentials);
  void stop();
  void loop(const WifiCredentials& credentials);
  bool connected() const;
  String ip() const;
  int32_t rssi() const;

 private:
  const char* logPrefix_ = "[wifi]";
  uint32_t retryIntervalMs_ = 10000;
  uint32_t lastAttemptMs_ = 0;
  bool warnedMissingConfig_ = false;

  void println(const char* message) const;
};

}  // namespace battlebang::esp::net
