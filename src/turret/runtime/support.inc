// Utility, hardware, aiming, ADC, and PID helpers.
float clampf(float v, float vmin, float vmax) {
  if (v < vmin) return vmin;
  if (v > vmax) return vmax;
  return v;
}

float minf2(float a, float b) {
  return (a < b) ? a : b;
}

float maxf2(float a, float b) {
  return (a > b) ? a : b;
}

float clampRangef(float v, float a, float b) {
  return clampf(v, minf2(a, b), maxf2(a, b));
}

float clampYawCommand(float deg) {
  return clampRangef(deg, YAW_CMD_MIN_DEG, YAW_CMD_MAX_DEG);
}

float clampPitchCommand(float deg) {
  return clampRangef(deg, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);
}

float clampYawIdleTarget(float deg) {
  return clampYawCommand(clampRangef(deg, IDLE_YAW_MIN, IDLE_YAW_MAX));
}

float clampPitchIdleTarget(float deg) {
  return clampPitchCommand(clampRangef(deg, IDLE_PITCH_MIN_DEG, IDLE_PITCH_MAX_DEG));
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
  yawTargetDeg = clampYawCommand(yawCurrentDeg);
  pitchTargetDeg = clampPitchCommand(pitchCurrentDeg);

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
                bool invertMotor,
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

  if (invertMotor) {
    output = -output;
  }

  servo.writeMicroseconds(static_cast<int>(SERVO_STOP_US - output));
}

bool isAimReached() {
  float yawErr = fabs(yawTargetDeg - yawCurrentDeg);
  float pitchErr = fabs(pitchTargetDeg - pitchCurrentDeg);
  return (yawErr <= YAW_REACHED_TOL_DEG) && (pitchErr <= PITCH_REACHED_TOL_DEG);
}
