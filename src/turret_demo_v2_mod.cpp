#include <Arduino.h>
#include <ESP32Servo.h>
#include <math.h>

// =====================================================
// 2-AXIS PID + YAW 3rd-order Linearization + Clipping
// + Target Coordinate (x,y) -> Yaw / Pitch Mapping
// + MODE_IDLE / MODE_TARGET / MODE_DEAD
// + Non-blocking fire sequence
// + Non-blocking serial command parser
//
// Serial Input:
//   x,y   -> target command
//   d     -> DEAD mode (immediate, even during fire)
//   r     -> return to IDLE (immediate, even during fire)
//
// Behavior:
//   - IDLE   : yaw sweep + pitch sweep
//   - TARGET : x,y 입력 -> yaw/pitch 계산 후 추종 -> 도달 시 발사 -> IDLE 복귀
//   - DEAD   : 현재 yaw 고정, pitch 최대 위로
// =====================================================

// ****************************************
// ===== 터렛 / 타겟 높이 =====    터렛마다 수동 입력
float x_turret = -300.0f;
float y_turret = 470.0f;
float z_turret = 134.5f;
float z_target = 70.0f;
// ****************************************


// ===== 핀 =====
const int YAW_POT_PIN   = 34;
const int YAW_SERVO_PIN = 18;

const int PITCH_POT_PIN   = 35;
const int PITCH_SERVO_PIN = 19;

// ===== Relay / ESC 핀 =====
const int RELAY_CH1_PIN = 21;
const int RELAY_CH2_PIN = 22;
const int RELAY_CH3_PIN = 23;
const int ESC_PIN       = 25;   // RELAY_CH3_PIN과 겹치지 않게

// =========================
// Relay logic
// true  -> Active LOW relay  (LOW = ON)
// false -> Active HIGH relay (HIGH = ON)
// =========================
const bool RELAY_ACTIVE_LOW = true;
const unsigned long RELAY_STEP_DELAY_MS = 250;
const unsigned long FIRE_HOLD_MS = 5000;

// ===== Serial 출력 주기 =====
const unsigned long SERIAL_PRINT_INTERVAL_MS = 100;

// ===== ADC 제한 =====
#define YAW_LOW_CUT    300
#define YAW_HIGH_CUT   3700

#define PITCH_LOW_CUT  1700
#define PITCH_HIGH_CUT 2400

// ===== 서보 =====
const int SERVO_STOP_US = 1500;
const int SERVO_MAX_US  = 500;

// ===== ESC =====
const int ESC_MIN_US  = 1000;
const int ESC_MAX_US  = 2000;
const int ESC_RUN_US  = 1800;
const int ESC_STOP_US = 1000;

// ===== PID 게인 =====
float Kp = 0.80f;
float Ki = 0.020f;
float Kd = 0.15f;

// ===== 축별 데드밴드 =====
const float YAW_DEADBAND   = 15.0f;
const float PITCH_DEADBAND = 20.0f;

// ===== 최소 구동량 =====
const float YAW_MIN_DRIVE   = 85.0f;
const float PITCH_MIN_DRIVE = 75.0f;

// ===== 적분 제한 =====
const float YAW_I_LIMIT   = 3000.0f;
const float PITCH_I_LIMIT = 3000.0f;

// ===== yaw 방향 판정 히스테리시스 =====
const int YAW_DIR_HYST = 30;

// ===== yaw 3차 선형화 계수 =====
// CCW
const float a_ccw = 0.000000054507f;
const float b_ccw = -0.000214602580f;
const float c_ccw = 1.265950091727f;
const float d_ccw = -96.842440399760f;

// CW
const float a_cw = 0.000000160563f;
const float b_cw = -0.001681412932f;
const float c_cw = 6.588812706009f;
const float d_cw = -5788.444633305053f;

// ===== yaw 기준 직선 =====
const float m_line = 13.4666666667f;
const float b_line = 2050.0f;

// ===== pseudo ADC/deg 스케일 =====
const float YAW_ADC_PER_DEG   = 2260.0f / 360.0f;
const float PITCH_ADC_PER_DEG = (3110.0f - 2050.0f) / 180.0f;

// ===== 목표 도달 판정 허용오차 =====
const float YAW_REACHED_TOL_DEG   = 2.0f;
const float PITCH_REACHED_TOL_DEG = 2.0f;


// ===== 탄도 파라미터 =====
const float v0_cm_s = 3962.4f;
const float g_cm_s2 = 981.0f;

// ===== pitch 명령 제한 =====
const float PITCH_CMD_MIN_DEG = ((PITCH_LOW_CUT  - 2050.0f) * 180.0f) / (3110.0f - 2050.0f);
const float PITCH_CMD_MAX_DEG = ((PITCH_HIGH_CUT - 2050.0f) * 180.0f) / (3110.0f - 2050.0f);

// ===== 서보 객체 =====
Servo yawServo;
Servo pitchServo;
Servo esc;

// ===== 목표 각도 =====
float yawTargetDeg   = 0.0f;
float pitchTargetDeg = 0.0f;

// ===== 현재 raw ADC =====
int yawRawCurrent   = 0;
int pitchRawCurrent = 0;

// ===== 현재 각도 =====
float yawCurrentDeg   = 0.0f;
float pitchCurrentDeg = 0.0f;

// ===== PID 상태 =====
float yawPrevErrorPseudo   = 0.0f;
float pitchPrevErrorPseudo = 0.0f;
float yawIntegralPseudo    = 0.0f;
float pitchIntegralPseudo  = 0.0f;

// ===== yaw 방향 판정 상태 =====
int yawDirRefRaw = -1;
bool lastYawIsCW = true;

// ===== 디버그 =====
float yawWarpADC   = 0.0f;
float lastTargetX  = 0.0f;
float lastTargetY  = 0.0f;
bool  lastPitchValid = true;

// ===== 모드 상태 =====
enum ControlMode {
  MODE_IDLE,
  MODE_TARGET,
  MODE_DEAD
};

ControlMode currentMode = MODE_IDLE;

// ===== idle sweep 설정 =====
const float IDLE_YAW_MIN = -70.0f;
const float IDLE_YAW_MAX =  70.0f;
const float IDLE_YAW_SWEEP_SPEED_DEG_PER_SEC = 25.0f;

const float IDLE_PITCH_MIN_DEG = -30.0f;
const float IDLE_PITCH_MAX_DEG = 30.0f;
const float IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC = 30.0f;

bool idleSweepForward = true;   // yaw: -70 -> +70
bool idlePitchUp      = true;   // pitch: min -> max

// ===== dead mode 설정 =====
float deadYawHoldDeg = 0.0f;

// ===== loop timing =====
unsigned long lastLoopMs = 0;

// ===== Serial 출력 상태 =====
unsigned long lastSerialPrintMs = 0;

// ===== TARGET 발사 상태 =====
bool fireTriggeredForCurrentTarget = false;

// ===== 발사 상태머신 =====
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

// ===== Serial command buffer =====
char serialBuf[64];
size_t serialLen = 0;

float clampf(float v, float vmin, float vmax) {
  if (v < vmin) return vmin;
  if (v > vmax) return vmax;
  return v;
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
                    float x_turret, float y_turret) {
  float vfx = -x_turret;
  float vfy = -y_turret;

  float vtx = x_target - x_turret;
  float vty = y_target - y_turret;

  if (fabs(vfx) < 1e-6f && fabs(vfy) < 1e-6f) return 0.0f;
  if (fabs(vtx) < 1e-6f && fabs(vty) < 1e-6f) return 0.0f;

  float cross = vfx * vty - vfy * vtx;
  float dot   = vfx * vtx + vfy * vty;

  float yaw_rad = -atan2(cross, dot);
  return radToDeg(yaw_rad);
}

bool computePitchDeg(float x_target, float y_target,
                     float x_turret, float y_turret,
                     float z_target, float z_turret,
                     float &pitchDeg) {
  float dx = x_target - x_turret;
  float dy = y_target - y_turret;
  float d  = sqrtf(dx * dx + dy * dy);

  if (d < 1e-6f) {
    pitchDeg = 0.0f;
    return false;
  }

  float dz = z_target - z_turret;
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
  float x = (float)raw;
  float y = 0.0f;

  if (isCW) {
    y = a_cw * x * x * x
      + b_cw * x * x
      + c_cw * x
      + d_cw;
  } else {
    y = a_ccw * x * x * x
      + b_ccw * x * x
      + c_ccw * x
      + d_ccw;
  }

  if (y < 0.0f)    y = 0.0f;
  if (y > 4095.0f) y = 4095.0f;

  return y;
}

float yawWarpADCtoDeg(float warp_adc) {
  return (warp_adc - b_line) / m_line;
}

float clipYawAngle(float angle_est) {
  if (angle_est > 140.0f)  angle_est = 140.0f;
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

  float angle_est  = yawWarpADCtoDeg(yawWarpADC);
  float angle_clip = clipYawAngle(angle_est);

  return angle_clip;
}

float pitchADCtoDeg(int adc) {
  float deg = ((float)(adc - 2050) * 180.0f) / (3110.0f - 2050.0f);

  if (deg > 180.0f)  deg = 180.0f;
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

  float output = P + I + D;
  prevErrorPseudo = errorPseudo;

  output = clampf(output, -SERVO_MAX_US, SERVO_MAX_US);

  if (fabs(output) < minDrive) {
    output = (output >= 0.0f) ? minDrive : -minDrive;
  }

  int servo_us = (int)(SERVO_STOP_US - output);
  servo.writeMicroseconds(servo_us);
}

bool isAimReached() {
  float yawErr = fabs(yawTargetDeg - yawCurrentDeg);
  float pitchErr = fabs(pitchTargetDeg - pitchCurrentDeg);

  return (yawErr <= YAW_REACHED_TOL_DEG) &&
         (pitchErr <= PITCH_REACHED_TOL_DEG);
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

  fireTriggeredForCurrentTarget = false;
  Serial.println("=== MODE CHANGE: -> IDLE ===");
}

void enterDeadMode() {
  currentMode = MODE_DEAD;
  deadYawHoldDeg = yawCurrentDeg;
  yawTargetDeg = deadYawHoldDeg;
  pitchTargetDeg = PITCH_CMD_MAX_DEG;
  fireTriggeredForCurrentTarget = false;
  Serial.println("=== MODE CHANGE: -> DEAD ===");
}

void abortFireSequenceAndSafeOff() {
  fireState = FIRE_IDLE;
  fireStateTs = millis();
  fireTriggeredForCurrentTarget = false;
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

void applyTargetCommand(float xTarget, float yTarget) {
  if (fireState != FIRE_IDLE) {
    Serial.println("[WARN] target ignored during firing. Use d or r to interrupt.");
    return;
  }

  float yawDeg = computeYawDeg(xTarget, yTarget, x_turret, y_turret);

  float pitchDeg = 0.0f;
  bool pitchValid = computePitchDeg(xTarget, yTarget,
                                    x_turret, y_turret,
                                    z_target, z_turret,
                                    pitchDeg);

  if (yawDeg > 140.0f)  yawDeg = 140.0f;
  if (yawDeg < -140.0f) yawDeg = -140.0f;

  pitchDeg = clampf(pitchDeg, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);

  yawTargetDeg   = yawDeg;
  pitchTargetDeg = pitchDeg;

  lastTargetX = xTarget;
  lastTargetY = yTarget;
  lastPitchValid = pitchValid;

  currentMode = MODE_TARGET;
  fireTriggeredForCurrentTarget = false;

  Serial.println("========== TARGET UPDATE ==========");
  Serial.println("Mode              : TARGET");
  Serial.print("Target X [cm]      : ");
  Serial.println(xTarget, 3);
  Serial.print("Target Y [cm]      : ");
  Serial.println(yTarget, 3);

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

  Serial.println("===================================");
}

void processSerialCommand(const String &inputRaw) {
  String input = inputRaw;
  input.trim();

  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("d")) {
    abortAndEnterDeadMode();
    return;
  }

  if (input.equalsIgnoreCase("r")) {
    abortAndEnterIdleMode();
    return;
  }

  int commaIndex = input.indexOf(',');
  if (commaIndex > 0) {
    float xTarget = input.substring(0, commaIndex).toFloat();
    float yTarget = input.substring(commaIndex + 1).toFloat();
    applyTargetCommand(xTarget, yTarget);
    return;
  }

  Serial.println("Invalid input. Use:");
  Serial.println("  x,y   (example: 100,50)");
  Serial.println("  d     (dead mode)");
  Serial.println("  r     (return idle)");
}

void handleSerialNonBlocking() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if ((c == 'd' || c == 'D' || c == 'r' || c == 'R') && serialLen == 0) {
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

void setup() {
  Serial.begin(115200);
  delay(500);

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

  lastLoopMs = millis();
  lastSerialPrintMs = millis();
  serialLen = 0;

  Serial.println("=== 2-AXIS PID READY ===");
  Serial.println("Input:");
  Serial.println("  x,y  (example: 100,50)");
  Serial.println("  d    -> DEAD mode (immediate)");
  Serial.println("  r    -> return to IDLE (immediate)");
  Serial.println("Mode IDLE   : yaw sweep + pitch sweep");
  Serial.println("Mode TARGET : track target, fire, return idle");
  Serial.println("Mode DEAD   : hold current yaw, pitch up");
}

void loop() {
  unsigned long now = millis();

  updateCurrentAngles();
  handleSerialNonBlocking();

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

  if (currentMode == MODE_TARGET &&
      fireState == FIRE_IDLE &&
      !fireTriggeredForCurrentTarget &&
      isAimReached()) {
    fireTriggeredForCurrentTarget = true;
    startFireSequence();
  }

  updateFireSequence();

  if (now - lastSerialPrintMs >= SERIAL_PRINT_INTERVAL_MS) {
    lastSerialPrintMs = now;

    Serial.print("Mode=");
    Serial.print(modeToStr(currentMode));

    Serial.print(" | TargetXY=(");
    Serial.print(lastTargetX, 1);
    Serial.print(", ");
    Serial.print(lastTargetY, 1);
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

    Serial.print(" | FIRE_STATE=");
    Serial.println(fireStateToStr(fireState));
  }

  lastLoopMs = now;
}
