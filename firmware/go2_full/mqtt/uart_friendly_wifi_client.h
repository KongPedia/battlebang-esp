#pragma once

#include <WiFi.h>

namespace go2 {

// PubSubClient calls Client::connect(host, port) synchronously. Keep a failed
// broker connection from monopolizing the main loop that polls Jetson UART.
class UartFriendlyWiFiClient : public WiFiClient {
 public:
  static constexpr int32_t kConnectTimeoutMs = 100;

  int connect(IPAddress ip, uint16_t port) override {
    return WiFiClient::connect(ip, port, kConnectTimeoutMs);
  }

  int connect(const char* host, uint16_t port) override {
    return WiFiClient::connect(host, port, kConnectTimeoutMs);
  }
};

}  // namespace go2
