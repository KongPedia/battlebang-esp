#include <Arduino.h>

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
