#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp32-hal-bt.h>
#include <math.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <string.h>

#include "build_config.h"

static const TurretBuildConfig BUILD_CONFIG = makeTurretBuildConfig();

// Keep the firmware in one translation unit for Arduino/PlatformIO stability,
// but split large sections into ordered implementation fragments for readability.
#include "runtime/state.inc"
#include "runtime/support.inc"
#include "runtime/control.inc"
#include "runtime/network.inc"

void setup() {
  Serial.begin(115200);
  delay(50);

#if TURRET_DISABLE_BROWNOUT_DETECTOR
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.println("[POWER] brownout detector disabled for diagnostics");
#endif

  if (TURRET_CPU_FREQ_MHZ > 0) {
    Serial.print("[POWER] setting CPU frequency to ");
    Serial.print(TURRET_CPU_FREQ_MHZ);
    Serial.println(" MHz");
    setCpuFrequencyMhz(TURRET_CPU_FREQ_MHZ);
  }

  buildIdentifiers();
  lastTargetZ = default_z_target;
  analogReadResolution(12);

#if TURRET_LAZY_RELAY_OUTPUTS
  parkRelayPinsSafeOff();
  Serial.println("[POWER] relay outputs parked as input-pullup until fire command");
#else
  prepareRelayPinsSafeOff();
#endif

#if TURRET_DISABLE_BT_AT_BOOT
  Serial.println("[POWER] stopping Bluetooth controller");
  btStop();
#endif

#if !TURRET_LAZY_ATTACH_SERVOS
  ensureMotionServosAttached("boot");
#else
  Serial.println("[POWER] motion servos deferred until first command");
#endif

#if TURRET_ESC_STOP_SIGNAL_AT_BOOT
  ensureEscStopSignal("boot-ready");
#elif !TURRET_LAZY_ARM_ESC
  ensureEscArmed("boot");
#else
  Serial.println("[POWER] ESC arm deferred until fire command");
#endif

#if TURRET_LAZY_ATTACH_SERVOS
  yawCurrentDeg = 0.0f;
  pitchCurrentDeg = 0.0f;
  yawRawCurrent = 0;
  pitchRawCurrent = 0;
#else
  updateCurrentAngles();
#endif
  yawTargetDeg = yawCurrentDeg;
  pitchTargetDeg = pitchCurrentDeg;
  idleSweepForward = true;
  idlePitchUp = true;
  serialLen = 0;
  lastLoopMs = millis();
  lastSerialPrintMs = millis();
  lastWiFiRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
  lastMqttRetryMs = 0;
  lastMqttStatusMs = 0;
  resetPidState();
  clearPendingFireFlags();
  enterHoldMode();

  Serial.println("=== TURRET FIRMWARE READY ===");
  Serial.println("Serial input:");
  Serial.println("  x,y");
  Serial.println("  x,y,z");
  Serial.println("  f / fire");
  Serial.println("  d / dead");
  Serial.println("  r / idle");
  Serial.println("  w / wifi");
  Serial.println("MQTT commands: idle / target / fire / dead");
  logBuildConfig();

#if TURRET_WIFI_CONNECT_IN_LOOP
  Serial.println("[WIFI] starting immediately (non-blocking)");
  startWiFiConnection();
  lastWiFiRetryMs = millis();
#else
  connectWiFiBlocking();
  ensureMqttConnected();
#endif
}

void loop() {
  unsigned long now = millis();

  handleSerialNonBlocking();
  ensureWiFiConnected();
  ensureMqttConnected();
  mqttClient.loop();

  if (areMotionServosAttached()) {
    updateCurrentAngles();
  }

  if (fireState == FIRE_IDLE) {
    if (currentMode == MODE_IDLE) {
      updateIdleSweep();
    } else if (currentMode == MODE_DEAD) {
      updateDeadModeTarget();
    }
  }

  if (areMotionServosAttached()) {
    runPIDAxis(yawServo,
               yawCurrentDeg, yawTargetDeg,
               YAW_ADC_PER_DEG,
               YAW_DEADBAND,
               YAW_MIN_DRIVE,
               YAW_I_LIMIT,
               YAW_INVERT_MOTOR,
               yawPrevErrorPseudo,
               yawIntegralPseudo);

    runPIDAxis(pitchServo,
               pitchCurrentDeg, pitchTargetDeg,
               PITCH_ADC_PER_DEG,
               PITCH_DEADBAND,
               PITCH_MIN_DRIVE,
               PITCH_I_LIMIT,
               PITCH_INVERT_MOTOR,
               pitchPrevErrorPseudo,
               pitchIntegralPseudo);
  }

  updateFireSequence();

  if (now - lastMqttStatusMs >= MQTT_STATUS_INTERVAL_MS) {
    lastMqttStatusMs = now;
    Serial.print("[NET] WiFi=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "UP" : "DOWN");
    Serial.print(" MQTT=");
    Serial.println(mqttClient.connected() ? "UP" : "DOWN");
  }

  if (now - lastSerialPrintMs >= SERIAL_PRINT_INTERVAL_MS) {
    lastSerialPrintMs = now;

    Serial.print("Mode=");
    Serial.print(modeToStr(currentMode));

    Serial.print(" | TargetXYZ=(");
    Serial.print(lastTargetX, 1);
    Serial.print(", ");
    Serial.print(lastTargetY, 1);
    Serial.print(", ");
    Serial.print(lastTargetZ, 1);
    Serial.print(")");

    Serial.print(" | YAW raw=");
    Serial.print(yawRawCurrent);
    Serial.print(" warp=");
    Serial.print(yawWarpADC, 1);
    Serial.print(" cur=");
    Serial.print(yawCurrentDeg, 2);
    Serial.print(" tgt=");
    Serial.print(yawTargetDeg, 2);

    Serial.print(" | PITCH raw=");
    Serial.print(pitchRawCurrent);
    Serial.print(" cur=");
    Serial.print(pitchCurrentDeg, 2);
    Serial.print(" tgt=");
    Serial.print(pitchTargetDeg, 2);

    Serial.print(" | Pvalid=");
    Serial.print(lastPitchValid ? "Y" : "N");

    Serial.print(" | DIR=");
    Serial.print(lastYawIsCW ? "CW" : "CCW");

    Serial.print(" | AIM=");
    Serial.print(isAimReached() ? "REACHED" : "MOVING");

    Serial.print(" | FIRE_PENDING=");
    Serial.print(manualFireQueued ? "Y" : "N");

    Serial.print(" | FIRE_STATE=");
    Serial.print(fireStateToStr(fireState));

    Serial.print(" | WiFi=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "UP" : "DOWN");

    Serial.print(" | MQTT=");
    Serial.println(mqttClient.connected() ? "UP" : "DOWN");
  }

  lastLoopMs = now;
}
