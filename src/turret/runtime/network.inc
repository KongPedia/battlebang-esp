// Wi-Fi, MQTT, and build configuration logging.
void startWiFiConnection() {
  if (isEmptyOrPlaceholder(BUILD_CONFIG.wifiSsid)) {
    Serial.println("[WIFI] SSID not configured. Override TURRET_WIFI_SSID at build time.");
    return;
  }

  Serial.print("[WIFI] connecting to ");
  Serial.println(BUILD_CONFIG.wifiSsid);

  Serial.println("[WIFI] stage: persistent(false) before init");
  Serial.flush();
  WiFi.persistent(false);

#if TURRET_WIFI_PRE_MODE_DELAY_MS > 0
  Serial.print("[WIFI] settling before radio init for ");
  Serial.print(TURRET_WIFI_PRE_MODE_DELAY_MS);
  Serial.println(" ms");
  Serial.flush();
  delay(TURRET_WIFI_PRE_MODE_DELAY_MS);
#endif

  Serial.println("[WIFI] stage: mode(WIFI_STA)");
  Serial.flush();

  WiFi.mode(WIFI_STA);

#if TURRET_WIFI_STAGE_DELAY_MS > 0
  delay(TURRET_WIFI_STAGE_DELAY_MS);
#endif

#if TURRET_WIFI_MODEM_SLEEP
  Serial.println("[WIFI] stage: setSleep(true)");
  Serial.flush();
  WiFi.setSleep(true);
#endif
  Serial.println("[WIFI] stage: setAutoReconnect(true)");
  Serial.flush();
  WiFi.setAutoReconnect(true);
#if TURRET_WIFI_REDUCED_TX_POWER
  Serial.println("[WIFI] stage: setTxPower(2dBm)");
  Serial.flush();
  WiFi.setTxPower(WIFI_POWER_2dBm);
  Serial.println("[WIFI] reduced TX power test mode enabled");
  Serial.flush();
#endif

  Serial.println("[WIFI] stage: begin()");
  Serial.flush();
  if (BUILD_CONFIG.wifiPassword == nullptr || BUILD_CONFIG.wifiPassword[0] == '\0' ||
      strncmp(BUILD_CONFIG.wifiPassword, "YOUR_", 5) == 0) {
    WiFi.begin(BUILD_CONFIG.wifiSsid);
  } else {
    WiFi.begin(BUILD_CONFIG.wifiSsid, BUILD_CONFIG.wifiPassword);
  }
}

void connectWiFiBlocking() {
  if (isEmptyOrPlaceholder(BUILD_CONFIG.wifiSsid)) return;

  startWiFiConnection();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_WAIT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] connected. IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] initial connect timed out. Will retry in loop.");
  }
}

void ensureWiFiConnected() {
  if (isEmptyOrPlaceholder(BUILD_CONFIG.wifiSsid)) return;
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (!wifiStartEnabled) {
    if (!wifiManualStartWaitLogged) {
      wifiManualStartWaitLogged = true;
      Serial.println("[WIFI] waiting for serial 'w' command before radio init");
    }
    return;
  }
  if (now < TURRET_WIFI_BOOT_DELAY_MS) {
    if (!wifiBootDelayLogged) {
      wifiBootDelayLogged = true;
      Serial.print("[WIFI] boot delay active before radio init: ");
      Serial.print(TURRET_WIFI_BOOT_DELAY_MS);
      Serial.println(" ms");
    }
    return;
  }
  if (now - lastWiFiRetryMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWiFiRetryMs = now;

  Serial.println("[WIFI] reconnect attempt");
  WiFi.disconnect();
  startWiFiConnection();
}

void mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
  char payloadBuf[MQTT_PAYLOAD_BUFFER_SIZE];
  if (length >= sizeof(payloadBuf)) {
    Serial.println("[MQTT] payload too large. dropped.");
    return;
  }

  memcpy(payloadBuf, payload, length);
  payloadBuf[length] = '\0';

  Serial.print("[MQTT] topic=");
  Serial.print(topic);
  Serial.print(" payload=");
  Serial.println(payloadBuf);

  StaticJsonDocument<MQTT_PAYLOAD_BUFFER_SIZE> doc;
  DeserializationError err = deserializeJson(doc, payloadBuf);
  if (err) {
    Serial.println("[MQTT] non-JSON payload. Falling back to serial command parser.");
    processSerialCommand(String(payloadBuf));
    return;
  }

  const char* payloadTurretId = doc[BUILD_CONFIG.mqttTurretIdField] | "";
  if (payloadTurretId[0] != '\0' && strcmp(payloadTurretId, BUILD_CONFIG.turretId) != 0) {
    Serial.print("[MQTT] turret_id mismatch. expected=");
    Serial.print(BUILD_CONFIG.turretId);
    Serial.print(" got=");
    Serial.println(payloadTurretId);
    return;
  }

  const char* command = doc[BUILD_CONFIG.mqttCommandField] | "";
  if (strcmp(command, "idle") == 0) {
    abortAndEnterIdleMode();
    return;
  }

  if (strcmp(command, "dead") == 0) {
    abortAndEnterDeadMode();
    return;
  }

  if (strcmp(command, "fire") == 0) {
    commandFire("MQTT");
    return;
  }

  if (strcmp(command, "target") == 0) {
    JsonObject target = doc[BUILD_CONFIG.mqttTargetField].as<JsonObject>();
    if (target.isNull()) {
      Serial.println("[MQTT] target object missing.");
      return;
    }

    if (!target.containsKey(BUILD_CONFIG.mqttTargetXField) ||
        !target.containsKey(BUILD_CONFIG.mqttTargetYField)) {
      Serial.println("[MQTT] target x/y missing.");
      return;
    }

    float x = mqttCoordToCm(target[BUILD_CONFIG.mqttTargetXField].as<float>());
    float y = mqttCoordToCm(target[BUILD_CONFIG.mqttTargetYField].as<float>());
    float z = default_z_target;
    if (target.containsKey(BUILD_CONFIG.mqttTargetZField)) {
      z = mqttCoordToCm(target[BUILD_CONFIG.mqttTargetZField].as<float>());
    }

    applyTargetCommand(x, y, z, "MQTT(target)");
    return;
  }

  Serial.print("[MQTT] unsupported command: ");
  Serial.println(command);
}

void ensureMqttConnected() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (isEmptyOrPlaceholder(BUILD_CONFIG.mqttHost)) {
    Serial.println("[MQTT] host not configured. Override TURRET_MQTT_HOST at build time.");
    return;
  }
  if (mqttClient.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttRetryMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttRetryMs = now;

  mqttClient.setServer(BUILD_CONFIG.mqttHost, BUILD_CONFIG.mqttPort);
  mqttClient.setCallback(mqttMessageCallback);
  mqttClient.setBufferSize(MQTT_PAYLOAD_BUFFER_SIZE);
  mqttClient.setKeepAlive(60);

  Serial.print("[MQTT] connecting to ");
  Serial.print(BUILD_CONFIG.mqttHost);
  Serial.print(':');
  Serial.println(BUILD_CONFIG.mqttPort);

  bool connected = false;
  if (BUILD_CONFIG.mqttUsername == nullptr || BUILD_CONFIG.mqttUsername[0] == '\0') {
    connected = mqttClient.connect(mqttClientId);
  } else {
    connected = mqttClient.connect(mqttClientId,
                                   BUILD_CONFIG.mqttUsername,
                                   BUILD_CONFIG.mqttPassword);
  }

  if (!connected) {
    Serial.print("[MQTT] connect failed. state=");
    Serial.println(mqttClient.state());
    return;
  }

  if (mqttClient.subscribe(mqttCommandTopic)) {
    Serial.print("[MQTT] subscribed to ");
    Serial.println(mqttCommandTopic);
  } else {
    Serial.print("[MQTT] subscribe failed for ");
    Serial.println(mqttCommandTopic);
  }
}

void logBuildConfig() {
  Serial.println("=== TURRET BUILD CONFIG ===");
  Serial.print("Turret ID          : ");
  Serial.println(BUILD_CONFIG.turretId);
  Serial.print("Turret XYZ [cm]    : ");
  Serial.print(x_turret, 2);
  Serial.print(", ");
  Serial.print(y_turret, 2);
  Serial.print(", ");
  Serial.println(z_turret, 2);
  Serial.print("Default target Z   : ");
  Serial.println(default_z_target, 2);
  Serial.print("Yaw/Pitch offset   : ");
  Serial.print(TURRET_YAW_OFFSET_DEG, 2);
  Serial.print(", ");
  Serial.println(TURRET_PITCH_OFFSET_DEG, 2);
  Serial.print("MQTT host/port     : ");
  Serial.print(BUILD_CONFIG.mqttHost);
  Serial.print(':');
  Serial.println(BUILD_CONFIG.mqttPort);
  Serial.print("MQTT topic         : ");
  Serial.println(mqttCommandTopic);
  Serial.print("MQTT coord units   : ");
  Serial.println(BUILD_CONFIG.mqttCoordinatesInMeters ? "meters -> cm" : "cm");
  Serial.println("Fire behavior      : event-driven only (target does not auto-fire)");
  Serial.print("Boot strategy      : ");
#if TURRET_WIFI_CONNECT_IN_LOOP
  Serial.print("WiFi-first/non-blocking");
#else
  Serial.print("blocking WiFi connect");
#endif
#if TURRET_LAZY_ATTACH_SERVOS
  Serial.print(", lazy servos");
#endif
#if TURRET_LAZY_ARM_ESC
  Serial.print(", lazy ESC arm");
#endif
#if TURRET_ESC_STOP_SIGNAL_AT_BOOT
  Serial.print(", ESC stop signal at boot");
#endif
#if TURRET_LAZY_RELAY_OUTPUTS
  Serial.print(", lazy relay outputs");
#endif
  Serial.println();
  Serial.print("CPU freq target    : ");
  Serial.print(TURRET_CPU_FREQ_MHZ);
  Serial.println(" MHz");
  Serial.print("WiFi boot delay    : ");
  Serial.print(TURRET_WIFI_BOOT_DELAY_MS);
  Serial.println(" ms");
  Serial.print("WiFi start trigger : ");
  Serial.println(TURRET_WIFI_START_ON_SERIAL ? "serial 'w' command" : "automatic");
  Serial.print("Serial log interval: ");
  Serial.print(TURRET_SERIAL_PRINT_INTERVAL_MS);
  Serial.println(" ms");
  Serial.println("===========================");
}
