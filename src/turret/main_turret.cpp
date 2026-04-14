// main_turret.cpp - Arduino IDE standalone export for turret_5
// PlatformIO builds src/turret/main.cpp instead.

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

// =====================================================
// Arduino IDE single-file config for turret_5
// =====================================================
// IMPORTANT:
// - Do not commit real Wi-Fi password if this file is shared publicly.
// - For Arduino IDE, install libraries:
//   ESP32Servo, PubSubClient, ArduinoJson
// - Arduino IDE app version (e.g. 2.3.7) is not the ESP32 core version.
//   Check Boards Manager: "esp32 by Espressif Systems".

#ifndef TURRET_ID
#define TURRET_ID "turret_5"
#endif

#ifndef TURRET_X_CM
#define TURRET_X_CM -170.0f
#endif

#ifndef TURRET_Y_CM
#define TURRET_Y_CM 190.0f
#endif

#ifndef TURRET_Z_CM
#define TURRET_Z_CM 134.5f
#endif

#ifndef TURRET_DEFAULT_TARGET_Z_CM
#define TURRET_DEFAULT_TARGET_Z_CM 70.0f
#endif

// Fill these on the Arduino IDE user's machine before upload.
#ifndef TURRET_WIFI_SSID
#define TURRET_WIFI_SSID "CHANGE_ME_WIFI_SSID"
#endif

#ifndef TURRET_WIFI_PASSWORD
#define TURRET_WIFI_PASSWORD "CHANGE_ME_WIFI_PASSWORD"
#endif

#ifndef TURRET_MQTT_HOST
#define TURRET_MQTT_HOST "CHANGE_ME_MQTT_HOST"
#endif

#ifndef TURRET_MQTT_PORT
#define TURRET_MQTT_PORT 1883
#endif

#ifndef TURRET_MQTT_USERNAME
#define TURRET_MQTT_USERNAME ""
#endif

#ifndef TURRET_MQTT_PASSWORD
#define TURRET_MQTT_PASSWORD ""
#endif

#ifndef TURRET_MQTT_TOPIC_PREFIX
#define TURRET_MQTT_TOPIC_PREFIX "battlebang/turrets"
#endif

#ifndef TURRET_MQTT_CLIENT_PREFIX
#define TURRET_MQTT_CLIENT_PREFIX "battlebang-esp"
#endif

#ifndef TURRET_MQTT_COORDS_IN_METERS
#define TURRET_MQTT_COORDS_IN_METERS 1
#endif

#ifndef TURRET_AUTO_FIRE_ON_TARGET
#define TURRET_AUTO_FIRE_ON_TARGET 0
#endif

#ifndef TURRET_MQTT_FIELD_COMMAND
#define TURRET_MQTT_FIELD_COMMAND "command"
#endif

#ifndef TURRET_MQTT_FIELD_TURRET_ID
#define TURRET_MQTT_FIELD_TURRET_ID "turret_id"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET
#define TURRET_MQTT_FIELD_TARGET "target"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET_X
#define TURRET_MQTT_FIELD_TARGET_X "x"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET_Y
#define TURRET_MQTT_FIELD_TARGET_Y "y"
#endif

#ifndef TURRET_MQTT_FIELD_TARGET_Z
#define TURRET_MQTT_FIELD_TARGET_Z "z"
#endif

#ifndef TURRET_WIFI_REDUCED_TX_POWER
#define TURRET_WIFI_REDUCED_TX_POWER 0
#endif

#ifndef TURRET_LAZY_ATTACH_SERVOS
#define TURRET_LAZY_ATTACH_SERVOS 1
#endif

#ifndef TURRET_LAZY_ARM_ESC
#define TURRET_LAZY_ARM_ESC 1
#endif

#ifndef TURRET_ESC_STOP_SIGNAL_AT_BOOT
#define TURRET_ESC_STOP_SIGNAL_AT_BOOT 1
#endif

#ifndef TURRET_LAZY_RELAY_OUTPUTS
#define TURRET_LAZY_RELAY_OUTPUTS 1
#endif

#ifndef TURRET_WIFI_CONNECT_IN_LOOP
#define TURRET_WIFI_CONNECT_IN_LOOP 1
#endif

#ifndef TURRET_WIFI_BOOT_DELAY_MS
#define TURRET_WIFI_BOOT_DELAY_MS 0
#endif

#ifndef TURRET_WIFI_START_ON_SERIAL
#define TURRET_WIFI_START_ON_SERIAL 0
#endif

#ifndef TURRET_WIFI_PRE_MODE_DELAY_MS
#define TURRET_WIFI_PRE_MODE_DELAY_MS 0
#endif

#ifndef TURRET_WIFI_STAGE_DELAY_MS
#define TURRET_WIFI_STAGE_DELAY_MS 0
#endif

#ifndef TURRET_DISABLE_BT_AT_BOOT
#define TURRET_DISABLE_BT_AT_BOOT 1
#endif

#ifndef TURRET_WIFI_MODEM_SLEEP
#define TURRET_WIFI_MODEM_SLEEP 0
#endif

#ifndef TURRET_ESC_ARM_DELAY_MS
#define TURRET_ESC_ARM_DELAY_MS 3000
#endif

#ifndef TURRET_CPU_FREQ_MHZ
#define TURRET_CPU_FREQ_MHZ 80
#endif

#ifndef TURRET_DISABLE_BROWNOUT_DETECTOR
#define TURRET_DISABLE_BROWNOUT_DETECTOR 0
#endif

#ifndef TURRET_SERIAL_PRINT_INTERVAL_MS
#define TURRET_SERIAL_PRINT_INTERVAL_MS 1000
#endif

struct TurretBuildConfig {
  const char* turretId;
  float xTurretCm;
  float yTurretCm;
  float zTurretCm;
  float defaultTargetZCm;
  const char* wifiSsid;
  const char* wifiPassword;
  const char* mqttHost;
  uint16_t mqttPort;
  const char* mqttUsername;
  const char* mqttPassword;
  const char* mqttTopicPrefix;
  const char* mqttClientPrefix;
  bool mqttCoordinatesInMeters;
  bool autoFireOnTarget;
  const char* mqttCommandField;
  const char* mqttTurretIdField;
  const char* mqttTargetField;
  const char* mqttTargetXField;
  const char* mqttTargetYField;
  const char* mqttTargetZField;
};

static const TurretBuildConfig BUILD_CONFIG = {
  TURRET_ID,
  TURRET_X_CM,
  TURRET_Y_CM,
  TURRET_Z_CM,
  TURRET_DEFAULT_TARGET_Z_CM,
  TURRET_WIFI_SSID,
  TURRET_WIFI_PASSWORD,
  TURRET_MQTT_HOST,
  static_cast<uint16_t>(TURRET_MQTT_PORT),
  TURRET_MQTT_USERNAME,
  TURRET_MQTT_PASSWORD,
  TURRET_MQTT_TOPIC_PREFIX,
  TURRET_MQTT_CLIENT_PREFIX,
  TURRET_MQTT_COORDS_IN_METERS != 0,
  TURRET_AUTO_FIRE_ON_TARGET != 0,
  TURRET_MQTT_FIELD_COMMAND,
  TURRET_MQTT_FIELD_TURRET_ID,
  TURRET_MQTT_FIELD_TARGET,
  TURRET_MQTT_FIELD_TARGET_X,
  TURRET_MQTT_FIELD_TARGET_Y,
  TURRET_MQTT_FIELD_TARGET_Z,
};

// ===== Turret position / defaults =====
float x_turret = BUILD_CONFIG.xTurretCm;
float y_turret = BUILD_CONFIG.yTurretCm;
float z_turret = BUILD_CONFIG.zTurretCm;
float default_z_target = BUILD_CONFIG.defaultTargetZCm;

// ===== Pins =====
const int YAW_POT_PIN   = 34;
const int YAW_SERVO_PIN = 18;
const int PITCH_POT_PIN   = 35;
const int PITCH_SERVO_PIN = 19;
const int RELAY_CH1_PIN = 21;
const int RELAY_CH2_PIN = 22;
const int RELAY_CH3_PIN = 23;
const int ESC_PIN       = 25;

// ===== Relay / Fire timing =====
const bool RELAY_ACTIVE_LOW = true;
const unsigned long RELAY_STEP_DELAY_MS = 250;
const unsigned long FIRE_COMMAND_HOLD_MS = 1000;

// ===== Serial / network timing =====
const unsigned long SERIAL_PRINT_INTERVAL_MS = TURRET_SERIAL_PRINT_INTERVAL_MS;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 3000;
const unsigned long MQTT_STATUS_INTERVAL_MS = 10000;
const unsigned long MQTT_RESUBSCRIBE_INTERVAL_MS = 5000;
const unsigned long WIFI_CONNECT_WAIT_MS = 12000;

// ===== ADC limits =====
#define YAW_LOW_CUT    300
#define YAW_HIGH_CUT   3700
#define PITCH_LOW_CUT  1700
#define PITCH_HIGH_CUT 2400

// ===== Servo / ESC =====
const int SERVO_STOP_US = 1500;
const int SERVO_MAX_US  = 500;
const int ESC_MIN_US  = 1000;
const int ESC_MAX_US  = 2000;
const int ESC_RUN_US  = 1900;
const int ESC_STOP_US = 1000;

// ===== PID gains =====
float Kp = 0.80f;
float Ki = 0.020f;
float Kd = 0.15f;

// ===== Control parameters =====
const float YAW_DEADBAND   = 15.0f;
const float PITCH_DEADBAND = 20.0f;
const float YAW_MIN_DRIVE   = 85.0f;
const float PITCH_MIN_DRIVE = 75.0f;
const float YAW_I_LIMIT   = 3000.0f;
const float PITCH_I_LIMIT = 3000.0f;
const int YAW_DIR_HYST = 30;

// ===== Yaw linearization =====
const float a_ccw = 0.000000054507f;
const float b_ccw = -0.000214602580f;
const float c_ccw = 1.265950091727f;
const float d_ccw = -96.842440399760f;
const float a_cw = 0.000000160563f;
const float b_cw = -0.001681412932f;
const float c_cw = 6.588812706009f;
const float d_cw = -5788.444633305053f;
const float m_line = 13.4666666667f;
const float b_line = 2050.0f;

// ===== Scale / tolerance =====
const float YAW_ADC_PER_DEG   = 2260.0f / 360.0f;
const float PITCH_ADC_PER_DEG = (3110.0f - 2050.0f) / 180.0f;
const float YAW_REACHED_TOL_DEG   = 2.0f;
const float PITCH_REACHED_TOL_DEG = 2.0f;

// ===== Ballistics =====
const float v0_cm_s = 3962.4f;
const float g_cm_s2 = 981.0f;
const float PITCH_CMD_MIN_DEG = ((PITCH_LOW_CUT  - 2050.0f) * 180.0f) / (3110.0f - 2050.0f);
const float PITCH_CMD_MAX_DEG = ((PITCH_HIGH_CUT - 2050.0f) * 180.0f) / (3110.0f - 2050.0f);

// ===== Idle behavior =====
const float IDLE_YAW_MIN = -70.0f;
const float IDLE_YAW_MAX =  70.0f;
const float IDLE_YAW_SWEEP_SPEED_DEG_PER_SEC = 25.0f;
const float IDLE_PITCH_MIN_DEG = -10.0f;
const float IDLE_PITCH_MAX_DEG = 0.0f;
const float IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC = 12.0f;

// ===== MQTT =====
const size_t MQTT_PAYLOAD_BUFFER_SIZE = 512;
char mqttCommandTopic[128] = {0};
char mqttClientId[64] = {0};
unsigned long lastWiFiRetryMs = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastMqttStatusMs = 0;
unsigned long lastMqttResubscribeMs = 0;

// ===== Devices =====
Servo yawServo;
Servo pitchServo;
Servo esc;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ===== Targets / state =====
float yawTargetDeg   = 0.0f;
float pitchTargetDeg = 0.0f;
float lastTargetX = 0.0f;
float lastTargetY = 0.0f;
float lastTargetZ = 0.0f;
int yawRawCurrent   = 0;
int pitchRawCurrent = 0;
float yawCurrentDeg   = 0.0f;
float pitchCurrentDeg = 0.0f;
float yawPrevErrorPseudo   = 0.0f;
float pitchPrevErrorPseudo = 0.0f;
float yawIntegralPseudo    = 0.0f;
float pitchIntegralPseudo  = 0.0f;
int yawDirRefRaw = -1;
bool lastYawIsCW = true;
float yawWarpADC = 0.0f;
bool lastPitchValid = true;
bool idleSweepForward = true;
bool idlePitchUp = true;
float deadYawHoldDeg = 0.0f;
unsigned long lastLoopMs = 0;
unsigned long lastSerialPrintMs = 0;
bool fireTriggeredForCurrentTarget = false;
bool pendingFireWhenAimReached = false;
bool manualFireQueued = false;
char serialBuf[64];
size_t serialLen = 0;
bool yawServoAttached = false;
bool pitchServoAttached = false;
bool escAttached = false;
bool escArmed = false;
bool relayOutputsAttached = false;
bool wifiStartEnabled = (TURRET_WIFI_START_ON_SERIAL == 0);
bool wifiManualStartWaitLogged = false;
bool wifiBootDelayLogged = false;

void updateCurrentAngles();

// ===== Mode / fire state =====
enum ControlMode {
  MODE_HOLD,
  MODE_IDLE,
  MODE_TARGET,
  MODE_DEAD
};

ControlMode currentMode = MODE_IDLE;
ControlMode postFireMode = MODE_HOLD;

enum FireState {
  FIRE_IDLE,
  FIRE_CH2_ON_WAIT,
  FIRE_CH1_ON_WAIT,
  FIRE_CH3_ON_WAIT,
  FIRE_BLDC_HOLD,
  FIRE_BLDC_OFF_WAIT,
  FIRE_CH3_OFF_WAIT,
  FIRE_CH1_OFF_WAIT
};

FireState fireState = FIRE_IDLE;
unsigned long fireStateTs = 0;
unsigned long fireKeepAliveUntilMs = 0;
bool fireRestartRequested = false;

float clampf(float v, float vmin, float vmax) {
  if (v < vmin) return vmin;
  if (v > vmax) return vmax;
  return v;
}

bool isEmptyOrPlaceholder(const char* value) {
  if (value == nullptr || value[0] == '\0') return true;
  return strncmp(value, "YOUR_", 5) == 0 || strncmp(value, "CHANGE_ME_", 10) == 0;
}

void buildIdentifiers() {
  snprintf(mqttCommandTopic, sizeof(mqttCommandTopic), "%s/%s/command",
           BUILD_CONFIG.mqttTopicPrefix,
           BUILD_CONFIG.turretId);
  snprintf(mqttClientId, sizeof(mqttClientId), "%s-%s",
           BUILD_CONFIG.mqttClientPrefix,
           BUILD_CONFIG.turretId);
}

void relayWrite(int pin, bool on) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, on ? LOW : HIGH);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

void relayAllOff() {
  relayWrite(RELAY_CH1_PIN, false);
  relayWrite(RELAY_CH2_PIN, false);
  relayWrite(RELAY_CH3_PIN, false);
}

void prepareRelayPinSafeOff(int pin) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }
  pinMode(pin, OUTPUT);
}

void parkRelayPinSafeOff(int pin) {
  if (RELAY_ACTIVE_LOW) {
    pinMode(pin, INPUT_PULLUP);
  } else {
    pinMode(pin, INPUT);
  }
}

void parkRelayPinsSafeOff() {
  parkRelayPinSafeOff(RELAY_CH1_PIN);
  parkRelayPinSafeOff(RELAY_CH2_PIN);
  parkRelayPinSafeOff(RELAY_CH3_PIN);
}

void prepareRelayPinsSafeOff() {
  if (relayOutputsAttached) return;
  prepareRelayPinSafeOff(RELAY_CH1_PIN);
  prepareRelayPinSafeOff(RELAY_CH2_PIN);
  prepareRelayPinSafeOff(RELAY_CH3_PIN);
  relayOutputsAttached = true;
  relayAllOff();
}

void ensureRelayOutputsAttached(const char* reason) {
  if (relayOutputsAttached) return;

  Serial.print("[POWER] attaching relay outputs for ");
  Serial.println(reason);
  prepareRelayPinsSafeOff();
}

void resetPidState() {
  yawPrevErrorPseudo = 0.0f;
  pitchPrevErrorPseudo = 0.0f;
  yawIntegralPseudo = 0.0f;
  pitchIntegralPseudo = 0.0f;
}

void forceOutputsSafeOff() {
  if (yawServoAttached) {
    yawServo.writeMicroseconds(SERVO_STOP_US);
  }
  if (pitchServoAttached) {
    pitchServo.writeMicroseconds(SERVO_STOP_US);
  }
  if (escAttached) {
    esc.writeMicroseconds(ESC_STOP_US);
    escArmed = false;
  }
  if (relayOutputsAttached) {
    relayAllOff();
  }
}

bool areMotionServosAttached() {
  return yawServoAttached && pitchServoAttached;
}

void ensureMotionServosAttached(const char* reason) {
  if (areMotionServosAttached()) return;

  updateCurrentAngles();
  resetPidState();
  yawTargetDeg = yawCurrentDeg;
  pitchTargetDeg = pitchCurrentDeg;

  Serial.print("[POWER] attaching motion servos for ");
  Serial.println(reason);

  if (!yawServoAttached) {
    yawServo.setPeriodHertz(50);
    yawServo.attach(YAW_SERVO_PIN, 500, 2500);
    yawServo.writeMicroseconds(SERVO_STOP_US);
    yawServoAttached = true;
  }

  if (!pitchServoAttached) {
    pitchServo.setPeriodHertz(50);
    pitchServo.attach(PITCH_SERVO_PIN, 500, 2500);
    pitchServo.writeMicroseconds(SERVO_STOP_US);
    pitchServoAttached = true;
  }
}

void ensureEscAttached(const char* reason) {
  if (escAttached) return;

  Serial.print("[POWER] attaching ESC for ");
  Serial.println(reason);
  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, ESC_MIN_US, ESC_MAX_US);
  esc.writeMicroseconds(ESC_STOP_US);
  escAttached = true;
  escArmed = false;
}

void ensureEscArmed(const char* reason) {
  ensureEscAttached(reason);
  if (escArmed) return;

  Serial.print("[POWER] arming ESC for ");
  Serial.println(reason);
  esc.writeMicroseconds(ESC_STOP_US);
  relayAllOff();
  delay(TURRET_ESC_ARM_DELAY_MS);
  escArmed = true;
  Serial.println("[POWER] ESC arm complete");
}

void ensureEscStopSignal(const char* reason) {
  ensureEscAttached(reason);
  esc.writeMicroseconds(ESC_STOP_US);
  Serial.print("[POWER] ESC STOP signal ready for ");
  Serial.println(reason);
}

void runEscNow(const char* reason) {
  ensureEscAttached(reason);
  esc.writeMicroseconds(ESC_RUN_US);
  escArmed = true;
  Serial.print("[FIRE] ESC RUN immediate for ");
  Serial.println(reason);
}

float radToDeg(float rad) {
  return rad * 180.0f / PI;
}

float computeYawDeg(float x_target, float y_target,
                    float x_turret_cm, float y_turret_cm) {
  float vfx = -x_turret_cm;
  float vfy = -y_turret_cm;
  float vtx = x_target - x_turret_cm;
  float vty = y_target - y_turret_cm;

  if (fabs(vfx) < 1e-6f && fabs(vfy) < 1e-6f) return 0.0f;
  if (fabs(vtx) < 1e-6f && fabs(vty) < 1e-6f) return 0.0f;

  float cross = vfx * vty - vfy * vtx;
  float dot   = vfx * vtx + vfy * vty;

  return radToDeg(-atan2(cross, dot));
}

bool computePitchDeg(float x_target, float y_target,
                     float x_turret_cm, float y_turret_cm,
                     float z_target_cm, float z_turret_cm,
                     float &pitchDeg) {
  float dx = x_target - x_turret_cm;
  float dy = y_target - y_turret_cm;
  float d  = sqrtf(dx * dx + dy * dy);

  if (d < 1e-6f) {
    pitchDeg = 0.0f;
    return false;
  }

  float dz = z_target_cm - z_turret_cm;
  float v2 = v0_cm_s * v0_cm_s;
  float disc = v2 * v2 - g_cm_s2 * (g_cm_s2 * d * d + 2.0f * dz * v2);

  if (disc < 0.0f) {
    pitchDeg = 0.0f;
    return false;
  }

  float root = sqrtf(disc);
  float tanTheta = (v2 - root) / (g_cm_s2 * d);
  float thetaRad = atanf(tanTheta);

  pitchDeg = radToDeg(thetaRad);
  return true;
}

int readADC(int pin, int lowCut, int highCut) {
  long sum = 0;
  const int N = 8;

  for (int i = 0; i < N; i++) {
    sum += analogRead(pin);
    delayMicroseconds(500);
  }

  int val = sum / N;
  if (val < lowCut)  val = lowCut;
  if (val > highCut) val = highCut;
  return val;
}

float warpADC3rd(int raw, bool isCW) {
  float x = static_cast<float>(raw);
  float y = 0.0f;

  if (isCW) {
    y = a_cw * x * x * x + b_cw * x * x + c_cw * x + d_cw;
  } else {
    y = a_ccw * x * x * x + b_ccw * x * x + c_ccw * x + d_ccw;
  }

  if (y < 0.0f) y = 0.0f;
  if (y > 4095.0f) y = 4095.0f;
  return y;
}

float yawWarpADCtoDeg(float warp_adc) {
  return (warp_adc - b_line) / m_line;
}

float clipYawAngle(float angle_est) {
  if (angle_est > 140.0f) angle_est = 140.0f;
  if (angle_est < -140.0f) angle_est = -140.0f;
  return angle_est;
}

bool detectYawDirection(int raw) {
  if (yawDirRefRaw < 0) {
    yawDirRefRaw = raw;
    return lastYawIsCW;
  }

  int delta = raw - yawDirRefRaw;
  if (delta >= YAW_DIR_HYST) {
    lastYawIsCW = true;
    yawDirRefRaw = raw;
  } else if (delta <= -YAW_DIR_HYST) {
    lastYawIsCW = false;
    yawDirRefRaw = raw;
  }

  return lastYawIsCW;
}

float yawADCtoDegLinearized(int raw) {
  bool isCW = detectYawDirection(raw);
  yawWarpADC = warpADC3rd(raw, isCW);
  return clipYawAngle(yawWarpADCtoDeg(yawWarpADC));
}

float pitchADCtoDeg(int adc) {
  float deg = (static_cast<float>(adc - 2050) * 180.0f) / (3110.0f - 2050.0f);
  if (deg > 180.0f) deg = 180.0f;
  if (deg < -180.0f) deg = -180.0f;
  return deg;
}

void updateCurrentAngles() {
  yawRawCurrent   = readADC(YAW_POT_PIN, YAW_LOW_CUT, YAW_HIGH_CUT);
  pitchRawCurrent = readADC(PITCH_POT_PIN, PITCH_LOW_CUT, PITCH_HIGH_CUT);
  yawCurrentDeg   = yawADCtoDegLinearized(yawRawCurrent);
  pitchCurrentDeg = pitchADCtoDeg(pitchRawCurrent);
}

void runPIDAxis(Servo &servo,
                float currentDeg,
                float targetDeg,
                float adcPerDeg,
                float deadband,
                float minDrive,
                float iLimit,
                float &prevErrorPseudo,
                float &integralPseudo) {
  float errorDeg    = targetDeg - currentDeg;
  float errorPseudo = errorDeg * adcPerDeg;

  if (fabs(errorPseudo) < deadband) {
    servo.writeMicroseconds(SERVO_STOP_US);
    integralPseudo *= 0.90f;
    prevErrorPseudo = errorPseudo;
    return;
  }

  integralPseudo += errorPseudo;
  integralPseudo = clampf(integralPseudo, -iLimit, iLimit);

  float P = Kp * errorPseudo;
  float I = Ki * integralPseudo;
  float D = Kd * (errorPseudo - prevErrorPseudo);
  float output = clampf(P + I + D, -SERVO_MAX_US, SERVO_MAX_US);
  prevErrorPseudo = errorPseudo;

  if (fabs(output) < minDrive) {
    output = (output >= 0.0f) ? minDrive : -minDrive;
  }

  servo.writeMicroseconds(static_cast<int>(SERVO_STOP_US - output));
}

bool isAimReached() {
  float yawErr = fabs(yawTargetDeg - yawCurrentDeg);
  float pitchErr = fabs(pitchTargetDeg - pitchCurrentDeg);
  return (yawErr <= YAW_REACHED_TOL_DEG) && (pitchErr <= PITCH_REACHED_TOL_DEG);
}

const char* modeToStr(int mode) {
  switch (mode) {
    case MODE_HOLD:   return "HOLD";
    case MODE_IDLE:   return "IDLE";
    case MODE_TARGET: return "TARGET";
    case MODE_DEAD:   return "DEAD";
    default:          return "?";
  }
}

const char* fireStateToStr(int st) {
  switch (st) {
    case FIRE_IDLE:          return "IDLE";
    case FIRE_CH2_ON_WAIT:   return "CH2_ON_WAIT";
    case FIRE_CH1_ON_WAIT:   return "CH1_ON_WAIT";
    case FIRE_CH3_ON_WAIT:   return "CH3_ON_WAIT";
    case FIRE_BLDC_HOLD:     return "BLDC_HOLD";
    case FIRE_BLDC_OFF_WAIT: return "BLDC_OFF_WAIT";
    case FIRE_CH3_OFF_WAIT:  return "CH3_OFF_WAIT";
    case FIRE_CH1_OFF_WAIT:  return "CH1_OFF_WAIT";
    default:                 return "?";
  }
}

void clearPendingFireFlags() {
  pendingFireWhenAimReached = false;
  manualFireQueued = false;
  fireTriggeredForCurrentTarget = false;
}

void enterHoldMode() {
  currentMode = MODE_HOLD;
  yawTargetDeg = yawCurrentDeg;
  pitchTargetDeg = pitchCurrentDeg;
  resetPidState();
  clearPendingFireFlags();
  Serial.println("=== MODE CHANGE: -> HOLD ===");
}

void setIdleSweepDirectionFromCurrentTarget() {
  if (yawTargetDeg >= IDLE_YAW_MAX) {
    idleSweepForward = false;
  } else if (yawTargetDeg <= IDLE_YAW_MIN) {
    idleSweepForward = true;
  }
}

void setIdlePitchDirectionFromCurrentTarget() {
  if (pitchTargetDeg >= IDLE_PITCH_MAX_DEG) {
    idlePitchUp = false;
  } else if (pitchTargetDeg <= IDLE_PITCH_MIN_DEG) {
    idlePitchUp = true;
  }
}

void enterIdleMode() {
#if TURRET_LAZY_ATTACH_SERVOS
  ensureMotionServosAttached("idle");
#endif
  currentMode = MODE_IDLE;
  yawTargetDeg = clampf(yawCurrentDeg, IDLE_YAW_MIN, IDLE_YAW_MAX);
  pitchTargetDeg = clampf(pitchCurrentDeg, IDLE_PITCH_MIN_DEG, IDLE_PITCH_MAX_DEG);
  setIdleSweepDirectionFromCurrentTarget();
  setIdlePitchDirectionFromCurrentTarget();
  clearPendingFireFlags();
  Serial.println("=== MODE CHANGE: -> IDLE ===");
}

void enterDeadMode() {
#if TURRET_LAZY_ATTACH_SERVOS
  ensureMotionServosAttached("dead");
#endif
  currentMode = MODE_DEAD;
  deadYawHoldDeg = yawCurrentDeg;
  yawTargetDeg = deadYawHoldDeg;
  pitchTargetDeg = PITCH_CMD_MAX_DEG;
  clearPendingFireFlags();
  Serial.println("=== MODE CHANGE: -> DEAD ===");
}

void abortFireSequenceAndSafeOff() {
  fireState = FIRE_IDLE;
  fireStateTs = millis();
  fireKeepAliveUntilMs = 0;
  fireRestartRequested = false;
  clearPendingFireFlags();
  forceOutputsSafeOff();
}

void abortAndEnterIdleMode() {
  abortFireSequenceAndSafeOff();
  enterIdleMode();
}

void abortAndEnterDeadMode() {
  abortFireSequenceAndSafeOff();
  enterDeadMode();
}

void updateIdleSweep() {
  unsigned long now = millis();
  float dt = (lastLoopMs == 0) ? 0.02f : (now - lastLoopMs) / 1000.0f;
  if (dt <= 0.0f) dt = 0.02f;
  if (dt > 0.2f)  dt = 0.2f;

  float yawDelta = IDLE_YAW_SWEEP_SPEED_DEG_PER_SEC * dt;
  if (idleSweepForward) {
    yawTargetDeg += yawDelta;
    if (yawTargetDeg >= IDLE_YAW_MAX) {
      yawTargetDeg = IDLE_YAW_MAX;
      idleSweepForward = false;
    }
  } else {
    yawTargetDeg -= yawDelta;
    if (yawTargetDeg <= IDLE_YAW_MIN) {
      yawTargetDeg = IDLE_YAW_MIN;
      idleSweepForward = true;
    }
  }

  float pitchDelta = IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC * dt;
  if (idlePitchUp) {
    pitchTargetDeg += pitchDelta;
    if (pitchTargetDeg >= IDLE_PITCH_MAX_DEG) {
      pitchTargetDeg = IDLE_PITCH_MAX_DEG;
      idlePitchUp = false;
    }
  } else {
    pitchTargetDeg -= pitchDelta;
    if (pitchTargetDeg <= IDLE_PITCH_MIN_DEG) {
      pitchTargetDeg = IDLE_PITCH_MIN_DEG;
      idlePitchUp = true;
    }
  }

  pitchTargetDeg = clampf(pitchTargetDeg, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);
}

void updateDeadModeTarget() {
  yawTargetDeg = deadYawHoldDeg;
  pitchTargetDeg = PITCH_CMD_MAX_DEG;
}

void startFireSequence() {
  if (fireState != FIRE_IDLE) return;
  ensureEscStopSignal("fire");
#if TURRET_LAZY_RELAY_OUTPUTS
  ensureRelayOutputsAttached("fire");
#endif
  postFireMode = (currentMode == MODE_IDLE) ? MODE_HOLD : currentMode;
  Serial.println("[FIRE] Start sequence");
  runEscNow("fire-command");
  fireKeepAliveUntilMs = millis() + FIRE_COMMAND_HOLD_MS;
  relayWrite(RELAY_CH2_PIN, true);
  fireState = FIRE_CH2_ON_WAIT;
  fireStateTs = millis();
}

void updateFireSequence() {
  unsigned long now = millis();

  switch (fireState) {
    case FIRE_IDLE:
      break;

    case FIRE_CH2_ON_WAIT:
      if (now - fireStateTs >= RELAY_STEP_DELAY_MS) {
        relayWrite(RELAY_CH1_PIN, true);
        fireState = FIRE_CH1_ON_WAIT;
        fireStateTs = now;
      }
      break;

    case FIRE_CH1_ON_WAIT:
      if (now - fireStateTs >= RELAY_STEP_DELAY_MS) {
        relayWrite(RELAY_CH3_PIN, true);
        fireState = FIRE_CH3_ON_WAIT;
        fireStateTs = now;
      }
      break;

    case FIRE_CH3_ON_WAIT:
      if (now - fireStateTs >= RELAY_STEP_DELAY_MS) {
        fireState = FIRE_BLDC_HOLD;
        fireStateTs = now;
      }
      break;

    case FIRE_BLDC_HOLD:
      if (fireKeepAliveUntilMs == 0) {
        fireKeepAliveUntilMs = now + FIRE_COMMAND_HOLD_MS;
      }

      if (now >= fireKeepAliveUntilMs) {
        esc.writeMicroseconds(ESC_STOP_US);
        fireState = FIRE_BLDC_OFF_WAIT;
        fireStateTs = now;
      }
      break;

    case FIRE_BLDC_OFF_WAIT:
      if (now - fireStateTs >= RELAY_STEP_DELAY_MS) {
        relayWrite(RELAY_CH3_PIN, false);
        fireState = FIRE_CH3_OFF_WAIT;
        fireStateTs = now;
      }
      break;

    case FIRE_CH3_OFF_WAIT:
      if (now - fireStateTs >= RELAY_STEP_DELAY_MS) {
        relayWrite(RELAY_CH1_PIN, false);
        fireState = FIRE_CH1_OFF_WAIT;
        fireStateTs = now;
      }
      break;

    case FIRE_CH1_OFF_WAIT:
      if (now - fireStateTs >= RELAY_STEP_DELAY_MS) {
        relayWrite(RELAY_CH2_PIN, false);
        fireState = FIRE_IDLE;
        fireStateTs = now;
        forceOutputsSafeOff();
        fireKeepAliveUntilMs = 0;
        clearPendingFireFlags();
        Serial.println("[FIRE] Sequence done");

        if (fireRestartRequested) {
          fireRestartRequested = false;
          fireKeepAliveUntilMs = now + FIRE_COMMAND_HOLD_MS;
          Serial.println("[FIRE] Restarting due to queued keepalive");
          startFireSequence();
        } else {
          currentMode = postFireMode;
        }
      }
      break;
  }
}

void applyTargetCommand(float xTargetCm, float yTargetCm, float zTargetCm, const char* source) {
  if (fireState != FIRE_IDLE) {
    Serial.println("[WARN] target ignored during firing. Use dead/idle to interrupt.");
    return;
  }

#if TURRET_LAZY_ATTACH_SERVOS
  ensureMotionServosAttached("target");
#endif

  float yawDeg = computeYawDeg(xTargetCm, yTargetCm, x_turret, y_turret);
  float pitchDeg = 0.0f;
  bool pitchValid = computePitchDeg(xTargetCm, yTargetCm,
                                    x_turret, y_turret,
                                    zTargetCm, z_turret,
                                    pitchDeg);

  yawDeg = clampf(yawDeg, -140.0f, 140.0f);
  pitchDeg = clampf(pitchDeg, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);

  yawTargetDeg   = yawDeg;
  pitchTargetDeg = pitchDeg;
  lastTargetX = xTargetCm;
  lastTargetY = yTargetCm;
  lastTargetZ = zTargetCm;
  lastPitchValid = pitchValid;

  currentMode = MODE_TARGET;
  fireTriggeredForCurrentTarget = false;
  manualFireQueued = false;
  pendingFireWhenAimReached = false;

  Serial.println("========== TARGET UPDATE ==========");
  Serial.print("Source             : ");
  Serial.println(source);
  Serial.println("Mode               : TARGET");
  Serial.print("Target X [cm]      : ");
  Serial.println(xTargetCm, 3);
  Serial.print("Target Y [cm]      : ");
  Serial.println(yTargetCm, 3);
  Serial.print("Target Z [cm]      : ");
  Serial.println(zTargetCm, 3);
  Serial.print("Computed Yaw [deg] : ");
  Serial.println(yawTargetDeg, 3);

  if (pitchValid) {
    Serial.print("Computed Pitch[deg]: ");
    Serial.println(pitchTargetDeg, 3);
  } else {
    Serial.println("Computed Pitch[deg]: INVALID (unreachable or too close)");
    Serial.print("Applied  Pitch[deg]: ");
    Serial.println(pitchTargetDeg, 3);
  }

  Serial.println("Auto fire on target: DISABLED (event-driven fire only)");
  Serial.println("===================================");
}

void commandFire(const char* source) {
  if (currentMode == MODE_DEAD) {
    Serial.print("[WARN] fire ignored in DEAD mode from ");
    Serial.println(source);
    return;
  }

  unsigned long now = millis();

  if (fireState != FIRE_IDLE) {
    fireKeepAliveUntilMs = now + FIRE_COMMAND_HOLD_MS;

    if (fireState == FIRE_BLDC_OFF_WAIT ||
        fireState == FIRE_CH3_OFF_WAIT ||
        fireState == FIRE_CH1_OFF_WAIT) {
      fireRestartRequested = true;
      Serial.print("[FIRE] restart queued from ");
      Serial.println(source);
    } else {
      Serial.print("[FIRE] keepalive refreshed from ");
      Serial.println(source);
    }
    return;
  }

  fireTriggeredForCurrentTarget = true;
  manualFireQueued = false;
  pendingFireWhenAimReached = false;
  fireRestartRequested = false;
  fireKeepAliveUntilMs = now + FIRE_COMMAND_HOLD_MS;
  Serial.print("[FIRE] immediate trigger from ");
  Serial.println(source);
  startFireSequence();
}

float mqttCoordToCm(float value) {
  return BUILD_CONFIG.mqttCoordinatesInMeters ? value * 100.0f : value;
}

bool parseCoordinateCommand(const String &input, float &x, float &y, float &z, bool &hasZ) {
  int firstComma = input.indexOf(',');
  if (firstComma <= 0) return false;

  int secondComma = input.indexOf(',', firstComma + 1);
  if (secondComma > firstComma) {
    x = input.substring(0, firstComma).toFloat();
    y = input.substring(firstComma + 1, secondComma).toFloat();
    z = input.substring(secondComma + 1).toFloat();
    hasZ = true;
    return true;
  }

  x = input.substring(0, firstComma).toFloat();
  y = input.substring(firstComma + 1).toFloat();
  z = default_z_target;
  hasZ = false;
  return true;
}

void processSerialCommand(const String &inputRaw) {
  String input = inputRaw;
  input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("d") || input.equalsIgnoreCase("dead")) {
    abortAndEnterDeadMode();
    return;
  }

  if (input.equalsIgnoreCase("r") || input.equalsIgnoreCase("idle")) {
    abortAndEnterIdleMode();
    return;
  }

  if (input.equalsIgnoreCase("f") || input.equalsIgnoreCase("fire")) {
    commandFire("SERIAL");
    return;
  }

  if (input.equalsIgnoreCase("w") || input.equalsIgnoreCase("wifi") ||
      input.equalsIgnoreCase("wifi_on")) {
    wifiStartEnabled = true;
    wifiManualStartWaitLogged = false;
    wifiBootDelayLogged = false;
    lastWiFiRetryMs = millis() - WIFI_RETRY_INTERVAL_MS;
    Serial.println("[WIFI] serial start requested");
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = default_z_target;
  bool hasZ = false;
  if (parseCoordinateCommand(input, x, y, z, hasZ)) {
    applyTargetCommand(x, y, z, hasZ ? "SERIAL(x,y,z)" : "SERIAL(x,y)");
    return;
  }

  Serial.println("Invalid input. Use:");
  Serial.println("  x,y");
  Serial.println("  x,y,z");
  Serial.println("  f / fire");
  Serial.println("  d / dead");
  Serial.println("  r / idle");
  Serial.println("  w / wifi");
}

void handleSerialNonBlocking() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());

    if ((c == 'd' || c == 'D' || c == 'r' || c == 'R' ||
         c == 'f' || c == 'F' || c == 'w' || c == 'W') && serialLen == 0) {
      String cmd = "";
      cmd += c;
      processSerialCommand(cmd);
      continue;
    }

    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialBuf[serialLen] = '\0';
        processSerialCommand(String(serialBuf));
        serialLen = 0;
      }
      continue;
    }

    if (serialLen < sizeof(serialBuf) - 1) {
      serialBuf[serialLen++] = c;
    } else {
      serialLen = 0;
      Serial.println("[WARN] serial buffer overflow. input dropped.");
    }
  }
}

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

bool subscribeMqttCommandTopic(const char* reason) {
  if (!mqttClient.connected()) return false;

  bool ok = mqttClient.subscribe(mqttCommandTopic);
  Serial.print("[MQTT] ");
  Serial.print(ok ? "subscribed" : "subscribe failed");
  Serial.print(" (");
  Serial.print(reason);
  Serial.print(") to ");
  Serial.println(mqttCommandTopic);
  lastMqttResubscribeMs = millis();
  return ok;
}

void ensureMqttConnected() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (isEmptyOrPlaceholder(BUILD_CONFIG.mqttHost)) {
    Serial.println("[MQTT] host not configured. Override TURRET_MQTT_HOST at build time.");
    return;
  }

  unsigned long now = millis();
  if (mqttClient.connected()) {
    if (now - lastMqttResubscribeMs >= MQTT_RESUBSCRIBE_INTERVAL_MS) {
      subscribeMqttCommandTopic("refresh");
    }
    return;
  }

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

  subscribeMqttCommandTopic("connect");
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
  enterIdleMode();

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
               yawPrevErrorPseudo,
               yawIntegralPseudo);

    runPIDAxis(pitchServo,
               pitchCurrentDeg, pitchTargetDeg,
               PITCH_ADC_PER_DEG,
               PITCH_DEADBAND,
               PITCH_MIN_DRIVE,
               PITCH_I_LIMIT,
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
