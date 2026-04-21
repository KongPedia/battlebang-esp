#include <Arduino.h>
#include <WiFiClient.h>

#include "app/firmware_info.h"
#include "app/identity.h"
#include "config/runtime_config.h"
#include "control/turret_control.h"
#include "mqtt/mqtt_bus.h"
#include "net/wifi_manager.h"
#include "ota/http_ota.h"
#include "ota/ota_manifest.h"

using namespace battlebang::turret_fleet;

RuntimeConfig config;
RuntimeConfigStore configStore;
WifiManager wifi;
TurretControl control;
MqttBus mqtt;

namespace {
char serialBuf[2048];
size_t serialLen = 0;

void printHelp() {
  Serial.println("[fleet] serial commands:");
  Serial.println("  help");
  Serial.println("  show-config");
  Serial.println("  clear-config");
  Serial.println("  config {json}");
  Serial.println("  net-status");
  Serial.println("  wifi-status");
  Serial.println("  mqtt-status");
  Serial.println("  tcp-probe [host] [port]");
  Serial.println("  check-ota [manifest-url]");
  Serial.print("  default manifest: ");
  Serial.println(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
}

void applyAndPersistConfig(const char* json) {
  String error;
  RuntimeConfig next = config;
  if (!applyRuntimeConfigJson(json, next, error)) {
    Serial.print("[fleet][serial] config rejected: ");
    Serial.println(error);
    return;
  }

  config = next;
  const bool saved = configStore.save(config);
  Serial.print("[fleet][serial] config applied saved=");
  Serial.println(saved ? "yes" : "no");
  control.applyConfig(config);
  wifi.begin(config);
  mqtt.reconfigure();
}

void checkOtaManifestUrl(const String& url) {
  if (!wifi.connected()) {
    Serial.println("[fleet][serial] Wi-Fi is not connected; cannot fetch OTA manifest");
    return;
  }

  String payload;
  String error;
  if (!fetchHttpText(url, 4096, payload, error)) {
    Serial.print("[fleet][serial] manifest fetch failed: ");
    Serial.println(error);
    return;
  }

  OtaManifest manifest;
  if (!parseOtaManifestJson(payload.c_str(), manifest, error)) {
    Serial.print("[fleet][serial] manifest rejected: ");
    Serial.println(error);
    return;
  }

  String reason;
  if (!shouldApplyOtaManifest(manifest, reason)) {
    Serial.print("[fleet][serial] OTA skipped: ");
    Serial.println(reason);
    return;
  }

  Serial.print("[fleet][serial] OTA accepted: ");
  Serial.println(otaManifestSummary(manifest));
  OtaResult result = runHttpOta(manifest);
  Serial.print("[fleet][serial] OTA result ok=");
  Serial.print(result.ok ? "yes" : "no");
  Serial.print(" message=");
  Serial.println(result.message);
  if (result.ok) {
    delay(500);
    ESP.restart();
  }
}

void runTcpProbe(String args) {
  if (!wifi.connected()) {
    Serial.println("[fleet][tcp] Wi-Fi is not connected; cannot probe TCP");
    wifi.printStatus(config, "tcp-probe");
    return;
  }

  args.trim();
  String host = config.mqttHost;
  uint16_t port = config.mqttPort;

  if (args.length() > 0) {
    const int firstSpace = args.indexOf(' ');
    if (firstSpace < 0) {
      host = args;
    } else {
      host = args.substring(0, firstSpace);
      String portStr = args.substring(firstSpace + 1);
      portStr.trim();
      const long parsedPort = portStr.toInt();
      if (parsedPort > 0 && parsedPort <= 65535) {
        port = static_cast<uint16_t>(parsedPort);
      }
    }
  }

  if (host.length() == 0) {
    Serial.println("[fleet][tcp] missing host; pass tcp-probe HOST PORT or configure mqtt.host first");
    return;
  }

  WiFiClient client;
  client.setTimeout(5);
  const unsigned long startedMs = millis();
  Serial.print("[fleet][tcp] probing ");
  Serial.print(host);
  Serial.print(':');
  Serial.print(port);
  Serial.print(" from_ip=");
  Serial.print(wifi.ip());
  Serial.print(" gateway=");
  Serial.println(wifi.gateway());

  const bool ok = client.connect(host.c_str(), port);
  const unsigned long elapsedMs = millis() - startedMs;
  Serial.print("[fleet][tcp] result=");
  Serial.print(ok ? "ok" : "fail");
  Serial.print(" elapsed_ms=");
  Serial.println(elapsedMs);
  if (ok) client.stop();
}

void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line == "help") {
    printHelp();
    return;
  }
  if (line == "show-config") {
    Serial.println(runtimeConfigToJson(config, false));
    return;
  }
  if (line == "clear-config") {
    const bool cleared = configStore.clear();
    config = makeDefaultRuntimeConfig(config.deviceId);
    control.applyConfig(config);
    wifi.begin(config);
    mqtt.reconfigure();
    Serial.print("[fleet][serial] config cleared=");
    Serial.println(cleared ? "yes" : "no");
    return;
  }
  if (line.startsWith("config ")) {
    applyAndPersistConfig(line.substring(7).c_str());
    return;
  }
  if (line == "net-status") {
    wifi.printStatus(config, "serial");
    mqtt.printStatus("serial");
    return;
  }
  if (line == "wifi-status") {
    wifi.printStatus(config, "serial");
    return;
  }
  if (line == "mqtt-status") {
    mqtt.printStatus("serial");
    return;
  }
  if (line == "tcp-probe") {
    runTcpProbe("");
    return;
  }
  if (line.startsWith("tcp-probe ")) {
    runTcpProbe(line.substring(10));
    return;
  }
  if (line == "check-ota" || line == "check-latest") {
    checkOtaManifestUrl(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
    return;
  }
  if (line.startsWith("check-ota ")) {
    String url = line.substring(10);
    url.trim();
    if (url.length() == 0) url = BB_TURRET_FLEET_LATEST_MANIFEST_URL;
    checkOtaManifestUrl(url);
    return;
  }

  Serial.print("[fleet][serial] unsupported command: ");
  Serial.println(line);
  printHelp();
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialBuf[serialLen] = '\0';
        handleSerialLine(String(serialBuf));
        serialLen = 0;
      }
      continue;
    }

    if (serialLen < sizeof(serialBuf) - 1) {
      serialBuf[serialLen++] = c;
    } else {
      serialLen = 0;
      Serial.println("[fleet][serial] buffer overflow; command dropped");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  config = makeDefaultRuntimeConfig(buildDeviceId());
  configStore.load(config);

  Serial.println("=== BATTLEBANG TURRET FLEET FIRMWARE ===");
  Serial.print("app=");
  Serial.print(BB_TURRET_FLEET_APP_NAME);
  Serial.print(" hardware=");
  Serial.print(BB_TURRET_FLEET_HARDWARE);
  Serial.print(" version=");
  Serial.print(BB_TURRET_FLEET_VERSION);
  Serial.print(" build=");
  Serial.print(BB_TURRET_FLEET_BUILD);
  Serial.print(" device_id=");
  Serial.println(config.deviceId);
  Serial.print("release_repo=");
  Serial.println(BB_TURRET_FLEET_RELEASE_REPO);
  Serial.print("latest_manifest=");
  Serial.println(BB_TURRET_FLEET_LATEST_MANIFEST_URL);
  Serial.println(runtimeConfigToJson(config, false));
  printHelp();

  control.begin(config);
  wifi.begin(config);
  mqtt.begin(config, configStore, wifi, control);
}

void loop() {
  pollSerial();
  wifi.loop(config);
  mqtt.loop();
  control.loop();
}
