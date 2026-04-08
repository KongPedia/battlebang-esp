#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "build_config.h"

static const TurretBuildConfig BUILD_CONFIG = makeTurretBuildConfig();

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
const unsigned long FIRE_HOLD_MS = 5000;

// ===== Serial / network timing =====
const unsigned long SERIAL_PRINT_INTERVAL_MS = 250;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5000;
const unsigned long MQTT_RETRY_INTERVAL_MS = 3000;
const unsigned long MQTT_STATUS_INTERVAL_MS = 10000;
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
const int ESC_RUN_US  = 1800;
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
const float IDLE_PITCH_MIN_DEG = -30.0f;
const float IDLE_PITCH_MAX_DEG = 30.0f;
const float IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC = 30.0f;

// ===== MQTT =====
const size_t MQTT_PAYLOAD_BUFFER_SIZE = 512;
char mqttCommandTopic[128] = {0};
char mqttClientId[64] = {0};
unsigned long lastWiFiRetryMs = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastMqttStatusMs = 0;

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

// ===== Mode / fire state =====
enum ControlMode {
  MODE_IDLE,
  MODE_TARGET,
  MODE_DEAD
};

ControlMode currentMode = MODE_IDLE;

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

float clampf(float v, float vmin, float vmax) {
  if (v < vmin) return vmin;
  if (v > vmax) return vmax;
  return v;
}

bool isEmptyOrPlaceholder(const char* value) {
  if (value == nullptr || value[0] == '\0') return true;
  return strncmp(value, "YOUR_", 5) == 0;
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

void forceOutputsSafeOff() {
  esc.writeMicroseconds(ESC_STOP_US);
  relayAllOff();
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

const char* modeToStr(ControlMode mode) {
  switch (mode) {
    case MODE_IDLE:   return "IDLE";
    case MODE_TARGET: return "TARGET";
    case MODE_DEAD:   return "DEAD";
    default:          return "?";
  }
}

const char* fireStateToStr(FireState st) {
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
  currentMode = MODE_IDLE;
  yawTargetDeg = clampf(yawCurrentDeg, IDLE_YAW_MIN, IDLE_YAW_MAX);
  pitchTargetDeg = clampf(pitchCurrentDeg, IDLE_PITCH_MIN_DEG, IDLE_PITCH_MAX_DEG);
  setIdleSweepDirectionFromCurrentTarget();
  setIdlePitchDirectionFromCurrentTarget();
  clearPendingFireFlags();
  Serial.println("=== MODE CHANGE: -> IDLE ===");
}

void enterDeadMode() {
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
  Serial.println("[FIRE] Start sequence");
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
        esc.writeMicroseconds(ESC_RUN_US);
        fireState = FIRE_BLDC_HOLD;
        fireStateTs = now;
      }
      break;

    case FIRE_BLDC_HOLD:
      if (now - fireStateTs >= FIRE_HOLD_MS) {
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
        Serial.println("[FIRE] Sequence done");
        enterIdleMode();
      }
      break;
  }
}

void applyTargetCommand(float xTargetCm, float yTargetCm, float zTargetCm, const char* source) {
  if (fireState != FIRE_IDLE) {
    Serial.println("[WARN] target ignored during firing. Use dead/idle to interrupt.");
    return;
  }

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
  pendingFireWhenAimReached = BUILD_CONFIG.autoFireOnTarget;

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

  Serial.print("Auto fire on target: ");
  Serial.println(BUILD_CONFIG.autoFireOnTarget ? "YES" : "NO");
  Serial.println("===================================");
}

void commandFire(const char* source) {
  if (fireState != FIRE_IDLE) {
    Serial.print("[WARN] fire ignored during active sequence from ");
    Serial.println(source);
    return;
  }

  if (currentMode == MODE_DEAD) {
    Serial.print("[WARN] fire ignored in DEAD mode from ");
    Serial.println(source);
    return;
  }

  if (currentMode == MODE_TARGET && !isAimReached()) {
    manualFireQueued = true;
    pendingFireWhenAimReached = false;
    fireTriggeredForCurrentTarget = false;
    Serial.print("[FIRE] queued until aim reached from ");
    Serial.println(source);
    return;
  }

  fireTriggeredForCurrentTarget = true;
  manualFireQueued = false;
  pendingFireWhenAimReached = false;
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
}

void handleSerialNonBlocking() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());

    if ((c == 'd' || c == 'D' || c == 'r' || c == 'R' || c == 'f' || c == 'F') && serialLen == 0) {
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

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

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
  Serial.print("MQTT host/port     : ");
  Serial.print(BUILD_CONFIG.mqttHost);
  Serial.print(':');
  Serial.println(BUILD_CONFIG.mqttPort);
  Serial.print("MQTT topic         : ");
  Serial.println(mqttCommandTopic);
  Serial.print("MQTT coord units   : ");
  Serial.println(BUILD_CONFIG.mqttCoordinatesInMeters ? "meters -> cm" : "cm");
  Serial.print("Auto fire on target: ");
  Serial.println(BUILD_CONFIG.autoFireOnTarget ? "YES" : "NO");
  Serial.println("===========================");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  buildIdentifiers();
  lastTargetZ = default_z_target;
  analogReadResolution(12);

  yawServo.setPeriodHertz(50);
  yawServo.attach(YAW_SERVO_PIN, 500, 2500);
  pitchServo.setPeriodHertz(50);
  pitchServo.attach(PITCH_SERVO_PIN, 500, 2500);
  yawServo.writeMicroseconds(SERVO_STOP_US);
  pitchServo.writeMicroseconds(SERVO_STOP_US);

  pinMode(RELAY_CH1_PIN, OUTPUT);
  pinMode(RELAY_CH2_PIN, OUTPUT);
  pinMode(RELAY_CH3_PIN, OUTPUT);

  esc.setPeriodHertz(50);
  esc.attach(ESC_PIN, ESC_MIN_US, ESC_MAX_US);

  Serial.println("ESC arming...");
  esc.writeMicroseconds(ESC_STOP_US);
  relayAllOff();
  delay(3000);

  currentMode = MODE_IDLE;
  yawTargetDeg = IDLE_YAW_MIN;
  pitchTargetDeg = clampf(IDLE_PITCH_MIN_DEG, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);
  idleSweepForward = true;
  idlePitchUp = true;
  serialLen = 0;
  lastLoopMs = millis();
  lastSerialPrintMs = millis();
  lastWiFiRetryMs = 0;
  lastMqttRetryMs = 0;
  lastMqttStatusMs = 0;
  clearPendingFireFlags();

  Serial.println("=== TURRET FIRMWARE READY ===");
  Serial.println("Serial input:");
  Serial.println("  x,y");
  Serial.println("  x,y,z");
  Serial.println("  f / fire");
  Serial.println("  d / dead");
  Serial.println("  r / idle");
  Serial.println("MQTT commands: idle / target / fire / dead");
  logBuildConfig();

  connectWiFiBlocking();
  ensureMqttConnected();
}

void loop() {
  unsigned long now = millis();

  updateCurrentAngles();
  handleSerialNonBlocking();
  ensureWiFiConnected();
  ensureMqttConnected();
  mqttClient.loop();

  if (fireState == FIRE_IDLE) {
    if (currentMode == MODE_IDLE) {
      updateIdleSweep();
    } else if (currentMode == MODE_DEAD) {
      updateDeadModeTarget();
    }
  }

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

  bool fireRequested = pendingFireWhenAimReached || manualFireQueued;
  if (currentMode == MODE_TARGET &&
      fireState == FIRE_IDLE &&
      !fireTriggeredForCurrentTarget &&
      fireRequested &&
      isAimReached()) {
    fireTriggeredForCurrentTarget = true;
    pendingFireWhenAimReached = false;
    manualFireQueued = false;
    startFireSequence();
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
    Serial.print((pendingFireWhenAimReached || manualFireQueued) ? "Y" : "N");

    Serial.print(" | FIRE_STATE=");
    Serial.print(fireStateToStr(fireState));

    Serial.print(" | WiFi=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "UP" : "DOWN");

    Serial.print(" | MQTT=");
    Serial.println(mqttClient.connected() ? "UP" : "DOWN");
  }

  lastLoopMs = now;
}
