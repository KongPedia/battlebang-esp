// Mode transitions, fire sequence, target, and serial command handling.
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

float getIdleYawMinTargetDeg();
float getIdleYawMaxTargetDeg();

void setIdleSweepDirectionFromCurrentTarget() {
  if (yawTargetDeg >= getIdleYawMaxTargetDeg()) {
    idleSweepForward = false;
  } else if (yawTargetDeg <= getIdleYawMinTargetDeg()) {
    idleSweepForward = true;
  }
}

float getIdleYawCenterDeg() {
  return clampf(IDLE_YAW_CENTER_DEG, -140.0f, 140.0f);
}

float getIdleYawSweepDeg() {
  return fabs(IDLE_YAW_SWEEP_DEG);
}

float getIdleYawMinTargetDeg() {
  return clampf(getIdleYawCenterDeg() - getIdleYawSweepDeg(), -140.0f, 140.0f);
}

float getIdleYawMaxTargetDeg() {
  return clampf(getIdleYawCenterDeg() + getIdleYawSweepDeg(), -140.0f, 140.0f);
}

float getIdlePitchMinTargetDeg();
float getIdlePitchMaxTargetDeg();

void setIdlePitchDirectionFromCurrentTarget() {
  float pitchMinDeg = getIdlePitchMinTargetDeg();
  float pitchMaxDeg = getIdlePitchMaxTargetDeg();
  if (pitchTargetDeg >= pitchMaxDeg) {
    idlePitchUp = false;
  } else if (pitchTargetDeg <= pitchMinDeg) {
    idlePitchUp = true;
  }
}

float getIdleConfiguredPitchCenterDeg() {
  return clampf(IDLE_CONFIGURED_PITCH_CENTER_DEG, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);
}

float getIdleConfiguredPitchSweepDeg() {
  return fabs(IDLE_CONFIGURED_PITCH_SWEEP_DEG);
}

float getIdlePitchMinTargetDeg() {
  if (IDLE_CONFIGURED_PITCH_ENABLED) {
    return clampf(getIdleConfiguredPitchCenterDeg() - getIdleConfiguredPitchSweepDeg(),
                  PITCH_CMD_MIN_DEG,
                  PITCH_CMD_MAX_DEG);
  }
  return clampf(IDLE_PITCH_MIN_DEG, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);
}

float getIdlePitchMaxTargetDeg() {
  if (IDLE_CONFIGURED_PITCH_ENABLED) {
    return clampf(getIdleConfiguredPitchCenterDeg() + getIdleConfiguredPitchSweepDeg(),
                  PITCH_CMD_MIN_DEG,
                  PITCH_CMD_MAX_DEG);
  }
  return clampf(IDLE_PITCH_MAX_DEG, PITCH_CMD_MIN_DEG, PITCH_CMD_MAX_DEG);
}

bool isIdlePitchSweepEnabled() {
  return (getIdlePitchMaxTargetDeg() - getIdlePitchMinTargetDeg()) > 0.001f;
}

void enterIdleMode() {
#if TURRET_LAZY_ATTACH_SERVOS
  ensureMotionServosAttached("idle");
#endif
  currentMode = MODE_IDLE;
  yawTargetDeg = clampf(yawCurrentDeg, getIdleYawMinTargetDeg(), getIdleYawMaxTargetDeg());
  if (IDLE_CONFIGURED_PITCH_ENABLED) {
    pitchTargetDeg = getIdleConfiguredPitchCenterDeg();
    idlePitchUp = true;
  } else {
    pitchTargetDeg = clampf(pitchCurrentDeg, getIdlePitchMinTargetDeg(), getIdlePitchMaxTargetDeg());
  }
  setIdlePitchDirectionFromCurrentTarget();
  setIdleSweepDirectionFromCurrentTarget();
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
  float yawMinDeg = getIdleYawMinTargetDeg();
  float yawMaxDeg = getIdleYawMaxTargetDeg();
  if (idleSweepForward) {
    yawTargetDeg += yawDelta;
    if (yawTargetDeg >= yawMaxDeg) {
      yawTargetDeg = yawMaxDeg;
      idleSweepForward = false;
    }
  } else {
    yawTargetDeg -= yawDelta;
    if (yawTargetDeg <= yawMinDeg) {
      yawTargetDeg = yawMinDeg;
      idleSweepForward = true;
    }
  }

  if (!isIdlePitchSweepEnabled()) {
    pitchTargetDeg = IDLE_CONFIGURED_PITCH_ENABLED
                       ? getIdleConfiguredPitchCenterDeg()
                       : getIdlePitchMinTargetDeg();
  } else {
    float pitchDelta = IDLE_PITCH_SWEEP_SPEED_DEG_PER_SEC * dt;
    float pitchMinDeg = getIdlePitchMinTargetDeg();
    float pitchMaxDeg = getIdlePitchMaxTargetDeg();
    if (idlePitchUp) {
      pitchTargetDeg += pitchDelta;
      if (pitchTargetDeg >= pitchMaxDeg) {
        pitchTargetDeg = pitchMaxDeg;
        idlePitchUp = false;
      }
    } else {
      pitchTargetDeg -= pitchDelta;
      if (pitchTargetDeg <= pitchMinDeg) {
        pitchTargetDeg = pitchMinDeg;
        idlePitchUp = true;
      }
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
  postFireMode = currentMode;
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

  yawDeg += TURRET_YAW_OFFSET_DEG;
  pitchDeg += TURRET_PITCH_OFFSET_DEG;

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
  Serial.print("Yaw offset [deg]   : ");
  Serial.println(TURRET_YAW_OFFSET_DEG, 3);
  Serial.print("Pitch offset[deg]  : ");
  Serial.println(TURRET_PITCH_OFFSET_DEG, 3);

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
