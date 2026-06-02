#include "turret_control.h"

#include <math.h>
#include <Preferences.h>
#include <string.h>

namespace battlebang {
namespace turret_fleet {
namespace {

const float kPi = 3.14159265358979323846f;
const float kV0CmS = 3962.4f;
const float kGCmS2 = 981.0f;

const int kYawPotPin = 34;
const int kYawServoPin = 18;
const int kPitchPotPin = 35;
const int kPitchServoPin = 19;
const int kRelayCh1Pin = 21;
const int kRelayCh2Pin = 22;
const int kRelayCh3Pin = 23;
const int kEscPin = 25;
const bool kRelayActiveLow = true;
const int kServoMaxDeltaUs = 260;
const int kEscMinUs = 1000;
const int kEscMaxUs = 2000;

const int kYawLowCut = 300;
const int kYawHighCut = 3700;
// ADC rail guards are only for clearly invalid feedback readings. They must
// not be treated as the normal operating envelope: the current bench turret can
// legitimately report yaw near the wrap edge and pitch above the old legacy
// 2400 clamp while still needing a small inward debug jog.
const int kYawFeedbackRailLowCut = 25;
const int kYawFeedbackRailHighCut = 4070;
const int kPitchLowCut = 1000;
const int kPitchHighCut = 3110;
const float kYawMinDeg = -140.0f;
const float kYawMaxDeg = 140.0f;
const float kPitchMinDeg = -90.0f;
const float kPitchMaxDeg = 90.0f;
// Production tracking must stay out of the observed 360-yaw feedback deadzone.
// The local origin is treated as calibrated, then target/idle/dead/home commands
// are clamped to a 150-degree total software envelope by default.
const bool kUnsafeManualCalibrationMode = false;
const float kDefaultCommandEnvelopeRatio = 0.65f;
const float kYawSoftMinDeg = -75.0f;
const float kYawSoftMaxDeg = 75.0f;
const float kPitchSoftMinDeg = -75.0f;
const float kPitchSoftMaxDeg = 75.0f;
const float kKp = 0.45f;
const float kKi = 0.0f;
const float kKd = 0.03f;
const float kYawDeadbandPseudo = 10.0f;
const float kPitchDeadbandPseudo = 10.0f;
const float kAimReachedToleranceDeg = 3.0f;
const unsigned long kAimStableBeforeCompleteMs = 300;
const float kAxisLockReleaseDeg = 5.0f;
const float kAxisSwitchHysteresisDeg = 3.0f;
const float kPitchUpPriorityBelowDeg = -35.0f;
const float kPatternCommandMarginDeg = 3.0f;
const float kYawMinDriveUs = 120.0f;
const float kPitchMinDriveUs = 120.0f;
const bool kYawInvertMotor = false;
const bool kPitchInvertMotor = false;
// Bench observation on 2026-05-29: yaw +PWM decreases raw/deg and -PWM increases raw/deg.
// With the monotonic raw->degree mapping, positive yaw error must therefore command -PWM.
const float kYawILimit = 3000.0f;
const float kPitchILimit = 3000.0f;
const float kYawAdcPerDeg = 2260.0f / 360.0f;
const float kPitchAdcPerDeg = (3110.0f - 2050.0f) / 180.0f;
const float kYawLineM = 13.4666666667f;
const float kYawLineB = 2050.0f;
const float kTargetYawLeadDeg = 30.0f;
const float kTargetPitchLeadDeg = 30.0f;
const int kYawSoftLowCut = 1040;
const int kYawSoftHighCut = 3060;
const int kPitchSoftLowCut = 1608;
const int kPitchSoftHighCut = 2492;
const bool kYawContinuousFeedback = true;
const char* kSafetyPrefsNamespace = "bb_fleet";
const char* kFireRecoveryMarkerKey = "fire_active";
const char* kRecoveryLockoutMarkerKey = "recover_req";
const bool kKeepMotionServosAttachedAtStop = true;
const int kSoftLimitRescueDeltaUs = 120;
const int kBootProbeDeltaUs = 20;
const int kYawSoftRecoverDeltaUs = 90;
const int kPitchSoftRecoverDeltaUs = 160;
const unsigned long kBootProbeDriveMs = 220;
const unsigned long kBootProbeStopMs = 180;
const unsigned long kYawSoftRecoverDriveMs = 80;
const unsigned long kPitchSoftRecoverDriveMs = 260;
const unsigned long kSoftRecoverStopMs = 160;
const int kSoftRecoverMaxSteps = 12;
const int kSoftRecoverMinProgressRaw = 4;
const int kYawStableSpanRaw = 450;
const int kPitchStableSpanRaw = 180;
// turret_4's yaw stage needs more breakaway torque than the original bench
// cap allowed. Runtime config still controls the normal value, but tracking
// must not silently clamp a configured 1900us-class yaw drive back to 1800us.
const int kTrackingYawMaxDeltaUs = 450;
const int kTrackingPitchMaxDeltaUs = 160;
const unsigned long kJogAttachSettleMs = 40;
const unsigned long kJogStopMs = 20;

float radToDeg(float rad) {
  return rad * 180.0f / kPi;
}

float clampf(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

int clampi(int value, int lo, int hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

float commandEnvelopeEdge(float outerDeg, float homeDeg, float ratio) {
  return homeDeg + (outerDeg - homeDeg) * ratio;
}

float leadToward(float current, float goal, float maxLeadDeg) {
  const float delta = goal - current;
  if (fabs(delta) <= maxLeadDeg) return goal;
  return current + (delta > 0.0f ? maxLeadDeg : -maxLeadDeg);
}

float computeYawDeg(float xTargetCm, float yTargetCm, float xTurretCm, float yTurretCm) {
  // Preserve the current src/turret solver convention: yaw zero faces the frame origin.
  const float vfx = -xTurretCm;
  const float vfy = -yTurretCm;
  const float vtx = xTargetCm - xTurretCm;
  const float vty = yTargetCm - yTurretCm;

  if (fabs(vfx) < 1e-6f && fabs(vfy) < 1e-6f) return 0.0f;
  if (fabs(vtx) < 1e-6f && fabs(vty) < 1e-6f) return 0.0f;

  const float cross = vfx * vty - vfy * vtx;
  const float dot = vfx * vtx + vfy * vty;
  return radToDeg(-atan2f(cross, dot));
}

bool computePitchDeg(float xTargetCm, float yTargetCm,
                     float xTurretCm, float yTurretCm,
                     float zTargetCm, float zTurretCm,
                     float& pitchDeg) {
  const float dx = xTargetCm - xTurretCm;
  const float dy = yTargetCm - yTurretCm;
  const float d = sqrtf(dx * dx + dy * dy);
  if (d < 1e-6f) {
    pitchDeg = 0.0f;
    return false;
  }

  const float dz = zTargetCm - zTurretCm;
  const float v2 = kV0CmS * kV0CmS;
  const float disc = v2 * v2 - kGCmS2 * (kGCmS2 * d * d + 2.0f * dz * v2);
  if (disc < 0.0f) {
    pitchDeg = 0.0f;
    return false;
  }

  const float root = sqrtf(disc);
  const float tanTheta = (v2 - root) / (kGCmS2 * d);
  pitchDeg = radToDeg(atanf(tanTheta));
  return true;
}

unsigned long normalizeFireHoldMs(JsonDocument& doc, const RuntimeConfig& config) {
  unsigned long durationMs = 0;
  if (doc["engagement_duration_ms"].is<unsigned long>()) {
    durationMs = doc["engagement_duration_ms"].as<unsigned long>();
  } else if (doc["duration_ms"].is<unsigned long>()) {
    durationMs = doc["duration_ms"].as<unsigned long>();
  } else if (doc["fire_duration_ms"].is<unsigned long>()) {
    durationMs = doc["fire_duration_ms"].as<unsigned long>();
  }
  if (durationMs == 0) return config.fireDefaultHoldMs;
  if (durationMs < config.fireMinHoldMs) return config.fireMinHoldMs;
  if (durationMs > config.fireMaxHoldMs) return config.fireMaxHoldMs;
  return durationMs;
}

unsigned long fireHardOffDelayMs(const RuntimeConfig& config, unsigned long holdMs) {
  // Relay sequencing is deliberately staged to avoid brownout. Even so, every
  // fire command gets an absolute wall-clock cap from the first relay-on edge,
  // so a stuck pattern leg or missed state transition cannot leave relays/ESC
  // energized indefinitely.
  return holdMs + (config.fireRelayStepDelayMs * 6UL) + 250UL;
}

void writeFireRecoveryMarker(bool active) {
  Preferences prefs;
  if (!prefs.begin(kSafetyPrefsNamespace, false)) {
    Serial.println("[fleet][safety] fire recovery marker NVS open failed");
    return;
  }
  prefs.putBool(kFireRecoveryMarkerKey, active);
  prefs.end();
}

void writeRecoveryLockoutMarker(bool active) {
  Preferences prefs;
  if (!prefs.begin(kSafetyPrefsNamespace, false)) {
    Serial.println("[fleet][safety] recovery lockout marker NVS open failed");
    return;
  }
  prefs.putBool(kRecoveryLockoutMarkerKey, active);
  prefs.end();
}

}  // namespace

void TurretControl::begin(const RuntimeConfig& config) {
  analogReadResolution(12);
  randomSeed(static_cast<unsigned long>(micros()) ^
             (static_cast<unsigned long>(analogRead(kYawPotPin)) << 16) ^
             static_cast<unsigned long>(analogRead(kPitchPotPin)));
  parkRelayPinsSafeOff();
  applyConfig(config);
  // Keep the legacy src/turret ESC contract: GPIO25 gets a 50Hz low-throttle
  // STOP signal at boot so the BLDC ESC can initialize/arm safely before an
  // explicit fire command. Relays remain parked safe-off until fire.
  ensureEscStopSignal("boot-ready");
  updateCurrentAngles();
  yawTargetDeg_ = yawCurrentDeg_;
  pitchTargetDeg_ = pitchCurrentDeg_;
  yawGoalDeg_ = yawCurrentDeg_;
  pitchGoalDeg_ = pitchCurrentDeg_;
  targetSlewActive_ = false;
  mode_ = config.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  fireState_ = "SAFE_OFF";
  patternState_ = "IDLE";
  lastLoopMs_ = millis();
  Serial.print("[fleet][control] mode=");
  Serial.println(mode_);
  Serial.println("[fleet][control] boot safe: runs initial local home without MQTT dependency; no idle sweep, pattern, or fire");
  Serial.print("[fleet][motion] boot ADC yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" yaw_deg=");
  Serial.print(yawCurrentDeg_, 2);
  Serial.print(" pitch_raw=");
  Serial.print(pitchRawCurrent_);
  Serial.print(" pitch_deg=");
  Serial.println(pitchCurrentDeg_, 2);
  Serial.println("[fleet][fire] relay outputs parked safe-off; ESC STOP signal active on boot-ready");
}

void TurretControl::prepareForNewMotionCommand(const char* source) {
  stopMotionOutputs();
  updateCurrentAngles();
  yawTargetDeg_ = yawCurrentDeg_;
  pitchTargetDeg_ = pitchCurrentDeg_;
  yawGoalDeg_ = yawCurrentDeg_;
  pitchGoalDeg_ = pitchCurrentDeg_;
  targetSlewActive_ = false;
  yawTrackingSuppressed_ = false;
  selectedMotionAxis_ = 'N';
  lockedMotionAxis_ = 'N';
  resetPidState();
  resetAxisGuard('A');
  Serial.print("[fleet][motion] preempted active motion for ");
  Serial.println(source);
}


void TurretControl::enterBootInitialTarget(bool motionAllowed) {
  if (!config_.configured) {
    mode_ = "UNCONFIGURED";
    return;
  }

  Serial.print("[fleet][control] boot initial local home: yaw=home_yaw,pitch=home_pitch motion=");
  Serial.println(motionAllowed ? "allowed" : "inhibited_after_brownout");
  bool bootProbeSafe = true;
  if (motionAllowed) {
    bootProbeSafe = runBootAxisProbe();
  }
  prepareForNewMotionCommand("boot_initial_home");
  solvedYawDeg_ = config_.homeYawDeg;
  solvedPitchDeg_ = config_.homePitchDeg;
  pitchReachable_ = true;
  clampedYawDeg_ = clampYawCommand(solvedYawDeg_);
  clampedPitchDeg_ = clampPitchCommand(solvedPitchDeg_);
  haveTarget_ = false;
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  aimReachedSinceMs_ = 0;
  patternState_ = "IDLE";
  resetPidState();

  const bool bootHomeInwardRecoveryAllowed =
      motionAllowed && !bootProbeSafe && yawInwardRecoveryAllowed();
  const bool trackInitialHome =
      motionAllowed &&
      (bootProbeSafe || bootHomeInwardRecoveryAllowed) &&
      ensureMotionSafetyForTracking("BOOT_HOME(home_0_0)");
  if (trackInitialHome) {
    setTrackedTarget(clampedYawDeg_, clampedPitchDeg_);
    mode_ = "HOME";
    lastError_ = "";
  } else {
    stopMotionOutputs();
    yawTrackingSuppressed_ = false;
    yawTargetDeg_ = yawCurrentDeg_;
    pitchTargetDeg_ = pitchCurrentDeg_;
    yawGoalDeg_ = yawCurrentDeg_;
    pitchGoalDeg_ = pitchCurrentDeg_;
    targetSlewActive_ = false;
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  }
  lastCommandId_ = "boot-home-0-0";
  Serial.println("========== FLEET BOOT HOME ==========");
  Serial.print("Home Yaw [deg]     : ");
  Serial.println(clampedYawDeg_, 3);
  Serial.print("Home Pitch[deg]    : ");
  Serial.println(clampedPitchDeg_, 3);
  Serial.print("Motion tracking    : ");
  Serial.println(trackInitialHome ? "ENABLED" : "INHIBITED");
  Serial.println("=====================================");
  if (!motionAllowed) {
    lastError_ = "boot home motion inhibited after brownout/fire reset";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  } else if (trackInitialHome && bootHomeInwardRecoveryAllowed) {
    Serial.println("[fleet][motion] boot home yaw inward recovery enabled");
  } else if (!bootProbeSafe && yawTrackingSuppressed_) {
    lastError_ = "boot home yaw inhibited: yaw outside calibrated 150deg safe envelope";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  } else if (!bootProbeSafe) {
    lastError_ = "boot home motion inhibited: axis outside calibrated 150deg safe envelope";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  } else if (kUnsafeManualCalibrationMode) {
    lastError_ = "boot home preview only: unsafe/manual calibration mode";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  }
}

void TurretControl::applyConfig(const RuntimeConfig& config) {
  const bool wasUnconfigured = (mode_ == "UNCONFIGURED" || mode_ == "BOOT");
  config_ = config;
  if (wasUnconfigured && config_.configured) {
    mode_ = "WAIT_COMMAND";
  }

  Serial.print("[fleet][control] config applied turret_id=");
  Serial.print(config.turretId);
  Serial.print(" frame_id=");
  Serial.print(config.frameId);
  Serial.print(" mqtt_target_unit=");
  Serial.print(config.mqttTargetUnit);
  Serial.print(" xyz_cm=(");
  Serial.print(config.xCm, 2);
  Serial.print(", ");
  Serial.print(config.yCm, 2);
  Serial.print(", ");
  Serial.print(config.zCm, 2);
  Serial.print(") fire_esc_run_us=");
  Serial.print(config.fireEscRunUs);
  Serial.print(" fire_default_hold_ms=");
  Serial.print(config.fireDefaultHoldMs);
  Serial.print(" yaw_stop_us=");
  Serial.print(config.yawStopUs);
  Serial.print(" pitch_stop_us=");
  Serial.print(config.pitchStopUs);
  Serial.print(" home=(");
  Serial.print(config.homeYawDeg, 2);
  Serial.print(", ");
  Serial.print(config.homePitchDeg, 2);
  Serial.print(") yaw_limits=(");
  Serial.print(config.yawMinDeg, 2);
  Serial.print(", ");
  Serial.print(config.yawMaxDeg, 2);
  Serial.print(") pitch_limits=(");
  Serial.print(config.pitchMinDeg, 2);
  Serial.print(", ");
  Serial.print(config.pitchMaxDeg, 2);
  Serial.print(")");
  Serial.print(" servo_max_delta_us=");
  Serial.print(config.servoMaxDeltaUs);
  Serial.print(" yaw_max_delta_us=");
  Serial.print(config.yawMaxDeltaUs);
  Serial.print(" pitch_max_delta_us=");
  Serial.print(config.pitchMaxDeltaUs);
  Serial.print(" axis_switch_cooldown_ms=");
  Serial.println(config.axisSwitchCooldownMs);
}

void TurretControl::setBrownoutLockout(bool active) {
  brownoutLockoutActive_ = active;
  writeRecoveryLockoutMarker(active);
  if (!active) return;

  forceFireOutputsSafeOff();
  stopMotionOutputs();
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  fireRestartRequested_ = false;
  fireKeepAliveUntilMs_ = 0;
  targetSlewActive_ = false;
  yawTrackingSuppressed_ = false;
  lockedMotionAxis_ = 'N';
  selectedMotionAxis_ = 'N';
  clearPatternState("brownout_lockout");
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  fireState_ = "SAFE_OFF";
  lastError_ = "brownout/fire-reset lockout active: motion/fire blocked until recover succeeds";
  Serial.println("[fleet][safety] brownout/fire-reset lockout active; fire forced off, motion commands blocked");
}

bool TurretControl::brownoutLockoutActive() const {
  return brownoutLockoutActive_;
}

bool TurretControl::recoverBrownoutLockoutIfSafe(const char* source) {
  return clearBrownoutLockoutIfSafe(source);
}

void TurretControl::ensureYawAttached(const char* reason) {
  if (!yawServoAttached_) {
    yawServo_.setPeriodHertz(50);
    yawServo_.attach(kYawServoPin, 1000, 2000);
    yawServo_.writeMicroseconds(config_.yawStopUs);
    yawLastCommandUs_ = config_.yawStopUs;
    yawServoAttached_ = true;
    yawAttachedAtMs_ = millis();
    Serial.print("[fleet][motion] yaw servo attached for ");
    Serial.println(reason);
  }
  motionEnabled_ = yawServoAttached_ || pitchServoAttached_;
}

void TurretControl::ensurePitchAttached(const char* reason) {
  if (!pitchServoAttached_) {
    pitchServo_.setPeriodHertz(50);
    pitchServo_.attach(kPitchServoPin, 1000, 2000);
    pitchServo_.writeMicroseconds(config_.pitchStopUs);
    pitchLastCommandUs_ = config_.pitchStopUs;
    pitchServoAttached_ = true;
    pitchAttachedAtMs_ = millis();
    Serial.print("[fleet][motion] pitch servo attached for ");
    Serial.println(reason);
  }
  motionEnabled_ = yawServoAttached_ || pitchServoAttached_;
}

void TurretControl::detachYawOutput(const char* reason) {
  if (!yawServoAttached_) return;
  const bool wasDriving = yawLastCommandUs_ != config_.yawStopUs || selectedMotionAxis_ == 'Y';
  yawServo_.writeMicroseconds(config_.yawStopUs);
  yawLastCommandUs_ = config_.yawStopUs;
  if (kKeepMotionServosAttachedAtStop) {
    motionEnabled_ = yawServoAttached_ || pitchServoAttached_;
    if (selectedMotionAxis_ == 'Y') selectedMotionAxis_ = 'N';
    if (wasDriving) {
      Serial.print("[fleet][motion] yaw servo parked attached for ");
      Serial.println(reason);
    }
    return;
  }
  yawServo_.detach();
  yawServoAttached_ = false;
  yawAttachedAtMs_ = 0;
  motionEnabled_ = yawServoAttached_ || pitchServoAttached_;
  lastAxisSwitchMs_ = millis();
  if (selectedMotionAxis_ == 'Y') selectedMotionAxis_ = 'N';
  Serial.print("[fleet][motion] yaw servo detached for ");
  Serial.println(reason);
}

void TurretControl::detachPitchOutput(const char* reason) {
  if (!pitchServoAttached_) return;
  const bool wasDriving = pitchLastCommandUs_ != config_.pitchStopUs || selectedMotionAxis_ == 'P';
  pitchServo_.writeMicroseconds(config_.pitchStopUs);
  pitchLastCommandUs_ = config_.pitchStopUs;
  if (kKeepMotionServosAttachedAtStop) {
    motionEnabled_ = yawServoAttached_ || pitchServoAttached_;
    if (selectedMotionAxis_ == 'P') selectedMotionAxis_ = 'N';
    if (wasDriving) {
      Serial.print("[fleet][motion] pitch servo parked attached for ");
      Serial.println(reason);
    }
    return;
  }
  pitchServo_.detach();
  pitchServoAttached_ = false;
  pitchAttachedAtMs_ = 0;
  motionEnabled_ = yawServoAttached_ || pitchServoAttached_;
  lastAxisSwitchMs_ = millis();
  if (selectedMotionAxis_ == 'P') selectedMotionAxis_ = 'N';
  Serial.print("[fleet][motion] pitch servo detached for ");
  Serial.println(reason);
}

void TurretControl::stopMotionOutputs() {
  detachYawOutput("stop_motion_outputs");
  detachPitchOutput("stop_motion_outputs");
}

bool TurretControl::motionInsideSoftWindow() const {
  if (kUnsafeManualCalibrationMode) return true;
  return yawRawInsideSoftWindow(yawRawCurrent_) &&
         pitchRawInsideSoftWindow(pitchRawCurrent_);
}

bool TurretControl::pitchInsideSoftWindow() const {
  if (kUnsafeManualCalibrationMode) return true;
  return pitchRawInsideSoftWindow(pitchRawCurrent_);
}

bool TurretControl::motionReadingsStableInSoftWindow(const char* source) {
  int yawMin = 4095;
  int yawMax = 0;
  int pitchMin = 4095;
  int pitchMax = 0;
  bool allInside = true;
  for (int i = 0; i < 6; ++i) {
    updateCurrentAngles();
    if (!motionInsideSoftWindow()) allInside = false;
    if (yawRawCurrent_ < yawMin) yawMin = yawRawCurrent_;
    if (yawRawCurrent_ > yawMax) yawMax = yawRawCurrent_;
    if (pitchRawCurrent_ < pitchMin) pitchMin = pitchRawCurrent_;
    if (pitchRawCurrent_ > pitchMax) pitchMax = pitchRawCurrent_;
    delay(40);
  }

  const int yawSpan = yawMax - yawMin;
  const int pitchSpan = pitchMax - pitchMin;
  const bool stable = allInside && yawSpan <= kYawStableSpanRaw && pitchSpan <= kPitchStableSpanRaw;
  Serial.print("[fleet][cal] feedback stability source=");
  Serial.print(source);
  Serial.print(" yaw_continuous=");
  Serial.print(kYawContinuousFeedback ? "Y" : "N");
  Serial.print(" yaw_raw_range=");
  Serial.print(yawMin);
  Serial.print("..");
  Serial.print(yawMax);
  Serial.print(" pitch_raw_range=");
  Serial.print(pitchMin);
  Serial.print("..");
  Serial.print(pitchMax);
  Serial.print(" result=");
  Serial.println(stable ? "STABLE" : "UNSTABLE");
  if (!stable) {
    lastError_ = String("motion inhibited: unstable feedback before tracking yaw_range=") +
                 String(yawMin) + ".." + String(yawMax) +
                 " pitch_range=" + String(pitchMin) + ".." + String(pitchMax);
  }
  return stable;
}

bool TurretControl::pitchReadingsStableInSoftWindow(const char* source) {
  int pitchMin = 4095;
  int pitchMax = 0;
  bool allInside = true;
  for (int i = 0; i < 6; ++i) {
    updateCurrentAngles();
    if (!pitchInsideSoftWindow()) allInside = false;
    if (pitchRawCurrent_ < pitchMin) pitchMin = pitchRawCurrent_;
    if (pitchRawCurrent_ > pitchMax) pitchMax = pitchRawCurrent_;
    delay(40);
  }

  const int pitchSpan = pitchMax - pitchMin;
  const bool stable = allInside && pitchSpan <= kPitchStableSpanRaw;
  Serial.print("[fleet][cal] pitch feedback stability source=");
  Serial.print(source);
  Serial.print(" pitch_raw_range=");
  Serial.print(pitchMin);
  Serial.print("..");
  Serial.print(pitchMax);
  Serial.print(" result=");
  Serial.println(stable ? "STABLE" : "UNSTABLE");
  if (!stable) {
    lastError_ = String("pitch-only motion inhibited: unstable pitch feedback before tracking pitch_range=") +
                 String(pitchMin) + ".." + String(pitchMax);
  }
  return stable;
}

bool TurretControl::yawInwardRecoveryAllowed() const {
  if (kUnsafeManualCalibrationMode) return true;
  const int yawSoftLow = yawSoftLowRaw();
  const int yawSoftHigh = yawSoftHighRaw();
  const bool yawLowOutside = yawRawCurrent_ < yawSoftLow;
  const bool yawHighOutside = yawRawCurrent_ > yawSoftHigh;
  if (!yawLowOutside && !yawHighOutside) return false;
  if (yawRawCurrent_ <= kYawFeedbackRailLowCut || yawRawCurrent_ >= kYawFeedbackRailHighCut) {
    return false;
  }

  // If inertia or sensor lag reports yaw just outside the configured outer
  // soft window, allow only commands that drive back toward the inner command
  // envelope. The motion loop's soft-limit guard still blocks outward drive.
  return (yawLowOutside && clampedYawDeg_ > yawCurrentDeg_) ||
         (yawHighOutside && clampedYawDeg_ < yawCurrentDeg_);
}

bool TurretControl::ensurePitchSafetyForTracking(const char* source) {
  updateCurrentAngles();
  if (kUnsafeManualCalibrationMode) {
    motionSafetyInhibited_ = false;
    Serial.print("[fleet][motion] unsafe/manual calibration: pitch tracking allowed from ");
    Serial.println(source);
    return true;
  }
  if (pitchInsideSoftWindow()) {
    if (pitchReadingsStableInSoftWindow(source)) {
      motionSafetyInhibited_ = false;
      return true;
    }
  }

  Serial.print("[fleet][motion] pitch-only soft-window recovery requested by ");
  Serial.print(source);
  Serial.print(" pitch_raw=");
  Serial.println(pitchRawCurrent_);
  if (recoverAxisTowardSoftWindow("pitch") && pitchReadingsStableInSoftWindow(source)) {
    motionSafetyInhibited_ = false;
    return true;
  }

  motionSafetyInhibited_ = true;
  detachPitchOutput("pitch_only_safety_failed");
  pitchIntegralPseudo_ = 0.0f;
  pitchPrevErrorPseudo_ = 0.0f;
    lastError_ = String("pitch-only motion inhibited outside calibrated 150deg safe envelope from ") + source +
               " pitch_raw=" + String(pitchRawCurrent_);
  Serial.print("[fleet][motion] ");
  Serial.println(lastError_);
  return false;
}

bool TurretControl::ensureMotionSafetyForTracking(const char* source) {
  if (brownoutLockoutActive_) {
    motionSafetyInhibited_ = true;
    stopMotionOutputs();
    resetPidState();
    lockedMotionAxis_ = 'N';
    selectedMotionAxis_ = 'N';
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
    lastError_ = String("motion rejected after brownout lockout from ") + source +
                 "; send recover after manually confirming pose/power";
    Serial.print("[fleet][safety] ");
    Serial.println(lastError_);
    return false;
  }
  updateCurrentAngles();
  if (kUnsafeManualCalibrationMode) {
    motionSafetyInhibited_ = false;
    Serial.print("[fleet][motion] unsafe/manual calibration: motion tracking allowed from ");
    Serial.println(source);
    return true;
  }
  if (motionInsideSoftWindow()) {
    if (motionReadingsStableInSoftWindow(source)) {
      motionSafetyInhibited_ = false;
      return true;
    }
  }

  if (yawInwardRecoveryAllowed()) {
    bool pitchSafe = pitchInsideSoftWindow();
    if (!pitchSafe) {
      Serial.print("[fleet][motion] yaw inward recovery requested pitch soft-window recovery by ");
      Serial.println(source);
      pitchSafe = recoverAxisTowardSoftWindow("pitch");
    }
    if (pitchSafe && pitchReadingsStableInSoftWindow(source)) {
      motionSafetyInhibited_ = false;
      Serial.print("[fleet][motion] yaw inward recovery tracking allowed from ");
      Serial.print(source);
      Serial.print(" yaw_raw=");
      Serial.print(yawRawCurrent_);
      Serial.print(" soft=");
      Serial.print(yawSoftLowRaw());
      Serial.print("..");
      Serial.print(yawSoftHighRaw());
      Serial.print(" target_deg=");
      Serial.println(clampedYawDeg_, 3);
      return true;
    }
  }

  Serial.print("[fleet][motion] soft-window recovery requested by ");
  Serial.print(source);
  Serial.print(" yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" pitch_raw=");
  Serial.println(pitchRawCurrent_);
  if (recoverMotionSoftWindow(source) && motionReadingsStableInSoftWindow(source)) {
    motionSafetyInhibited_ = false;
    return true;
  }

  motionSafetyInhibited_ = true;
  stopMotionOutputs();
  resetPidState();
  lockedMotionAxis_ = 'N';
  selectedMotionAxis_ = 'N';
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  targetSlewActive_ = false;
  lastError_ = String("motion inhibited outside calibrated 150deg safe envelope from ") + source +
               " yaw_raw=" + String(yawRawCurrent_) +
               " pitch_raw=" + String(pitchRawCurrent_);
  Serial.print("[fleet][motion] ");
  Serial.println(lastError_);
  return false;
}

bool TurretControl::clearBrownoutLockoutIfSafe(const char* source) {
  forceFireOutputsSafeOff();
  stopMotionOutputs();
  updateCurrentAngles();
  if (!brownoutLockoutActive_) {
    writeRecoveryLockoutMarker(false);
    lastError_ = "";
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
    Serial.print("[fleet][safety] recover ignored; brownout lockout already clear from ");
    Serial.println(source);
    return true;
  }

  if (!motionInsideSoftWindow()) {
    Serial.print("[fleet][safety] brownout recover soft-window recovery requested by ");
    Serial.print(source);
    Serial.print(" yaw_raw=");
    Serial.print(yawRawCurrent_);
    Serial.print(" pitch_raw=");
    Serial.println(pitchRawCurrent_);
    recoverMotionSoftWindow(source);
    stopMotionOutputs();
    updateCurrentAngles();
  }

  if (!motionInsideSoftWindow() || !motionReadingsStableInSoftWindow(source)) {
    motionSafetyInhibited_ = true;
    lastError_ = String("recover rejected: feedback outside stable soft window yaw_raw=") +
                 String(yawRawCurrent_) + " pitch_raw=" + String(pitchRawCurrent_);
    Serial.print("[fleet][safety] ");
    Serial.println(lastError_);
    return false;
  }

  brownoutLockoutActive_ = false;
  writeRecoveryLockoutMarker(false);
  motionSafetyInhibited_ = false;
  targetSlewActive_ = false;
  yawTrackingSuppressed_ = false;
  lockedMotionAxis_ = 'N';
  selectedMotionAxis_ = 'N';
  yawTargetDeg_ = yawCurrentDeg_;
  pitchTargetDeg_ = pitchCurrentDeg_;
  yawGoalDeg_ = yawCurrentDeg_;
  pitchGoalDeg_ = pitchCurrentDeg_;
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  lastError_ = "";
  Serial.print("[fleet][safety] brownout lockout cleared by ");
  Serial.print(source);
  Serial.print(" yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" pitch_raw=");
  Serial.println(pitchRawCurrent_);
  return true;
}

bool TurretControl::commandBlockedByBrownoutLockout(const char* command, const char* source) {
  if (!brownoutLockoutActive_) return false;
  if (strcmp(command, "hold") == 0 || strcmp(command, "wait") == 0) return false;
  if (strcmp(command, "recover") == 0 || strcmp(command, "clear_brownout_lockout") == 0) return false;

  forceFireOutputsSafeOff();
  stopMotionOutputs();
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  patternState_ = "IDLE";
  targetSlewActive_ = false;
  lastError_ = String("command rejected after brownout lockout: ") + command +
               " from " + source + "; allowed: hold, recover";
  Serial.print("[fleet][safety] ");
  Serial.println(lastError_);
  return true;
}

bool TurretControl::recoverMotionSoftWindow(const char* source) {
  if (!config_.configured) return false;

  updateCurrentAngles();
  bool yawSafe = yawRawInsideSoftWindow(yawRawCurrent_);
  bool pitchSafe = pitchRawInsideSoftWindow(pitchRawCurrent_);
  if (yawSafe && pitchSafe) return true;

  Serial.print("[fleet][cal] soft-window recovery start source=");
  Serial.print(source);
  Serial.print(" yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" pitch_raw=");
  Serial.println(pitchRawCurrent_);

  if (!yawSafe) {
    yawSafe = recoverAxisTowardSoftWindow("yaw");
    delay(config_.axisSwitchCooldownMs);
  }

  updateCurrentAngles();
  pitchSafe = pitchRawInsideSoftWindow(pitchRawCurrent_);
  if (!pitchSafe) {
    pitchSafe = recoverAxisTowardSoftWindow("pitch");
    delay(config_.axisSwitchCooldownMs);
  }

  stopMotionOutputs();
  updateCurrentAngles();
  yawSafe = yawRawInsideSoftWindow(yawRawCurrent_);
  pitchSafe = pitchRawInsideSoftWindow(pitchRawCurrent_);
  Serial.print("[fleet][cal] soft-window recovery done yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" pitch_raw=");
  Serial.print(pitchRawCurrent_);
  Serial.print(" result=");
  Serial.println((yawSafe && pitchSafe) ? "SAFE" : "INHIBIT");
  return yawSafe && pitchSafe;
}

bool TurretControl::recoverAxisTowardSoftWindow(const char* axis) {
  const bool isYaw = strcmp(axis, "yaw") == 0;
  updateCurrentAngles();
  int raw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  const int softLow = isYaw ? yawSoftLowRaw() : pitchSoftLowRaw();
  const int softHigh = isYaw ? yawSoftHighRaw() : pitchSoftHighRaw();
  const int centerRaw = isYaw ? yawRawForDeg(config_.homeYawDeg) : pitchRawForDeg(config_.homePitchDeg);
  const int stopUs = isYaw ? config_.yawStopUs : config_.pitchStopUs;
  int recoverDeltaUs = 0;
  if (isYaw) {
    if (kYawContinuousFeedback) {
      Serial.println("[fleet][cal] yaw recovery skipped: continuous feedback must be inside calibrated 150deg safe envelope before tracking");
      return false;
    }
    recoverDeltaUs = config_.yawMaxDeltaUs < kYawSoftRecoverDeltaUs ? config_.yawMaxDeltaUs : kYawSoftRecoverDeltaUs;
    if (recoverDeltaUs < config_.yawMinDriveUs) recoverDeltaUs = config_.yawMinDriveUs;
  } else {
    recoverDeltaUs = config_.pitchMaxDeltaUs < kPitchSoftRecoverDeltaUs ? config_.pitchMaxDeltaUs : kPitchSoftRecoverDeltaUs;
    if (recoverDeltaUs < config_.pitchMinDriveUs) recoverDeltaUs = config_.pitchMinDriveUs;
  }
  recoverDeltaUs = clampi(recoverDeltaUs, 20, 400);
  const unsigned long recoverDriveMs = isYaw ? kYawSoftRecoverDriveMs : kPitchSoftRecoverDriveMs;
  const int plusUs = clampi(stopUs + recoverDeltaUs, 1000, 2000);
  const int minusUs = clampi(stopUs - recoverDeltaUs, 1000, 2000);

  if (raw >= softLow && raw <= softHigh) return true;

  if (isYaw && !kYawContinuousFeedback && (raw <= kYawLowCut || raw >= kYawHighCut)) {
    Serial.println("[fleet][cal] yaw recovery skipped at hard-edge feedback; refusing servo attach to avoid brownout/overshoot");
    return false;
  }

  Serial.print("[fleet][cal] ");
  Serial.print(axis);
  Serial.print(" recovery begin raw=");
  Serial.print(raw);
  Serial.print(" soft=");
  Serial.print(softLow);
  Serial.print("..");
  Serial.print(softHigh);
  Serial.print(" center=");
  Serial.print(centerRaw);
  Serial.print(" delta_us=");
  Serial.println(recoverDeltaUs);

  if (isYaw) {
    ensureYawAttached("soft_window_recovery");
  } else {
    ensurePitchAttached("soft_window_recovery");
  }
  delay(config_.servoAttachSettleMs);

  auto runPulse = [&](int commandUs, const char* label) -> int {
    updateCurrentAngles();
    const int beforeRaw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
    const int beforeDistance = abs(beforeRaw - centerRaw);
    if (isYaw) {
      yawServo_.writeMicroseconds(commandUs);
      yawLastCommandUs_ = commandUs;
    } else {
      pitchServo_.writeMicroseconds(commandUs);
      pitchLastCommandUs_ = commandUs;
    }
    delay(recoverDriveMs);
    if (isYaw) {
      yawServo_.writeMicroseconds(config_.yawStopUs);
      yawLastCommandUs_ = config_.yawStopUs;
    } else {
      pitchServo_.writeMicroseconds(config_.pitchStopUs);
      pitchLastCommandUs_ = config_.pitchStopUs;
    }
    delay(kSoftRecoverStopMs);
    updateCurrentAngles();
    const int afterRaw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
    const int afterDistance = abs(afterRaw - centerRaw);
    const int progress = beforeDistance - afterDistance;
    Serial.print("[fleet][cal] ");
    Serial.print(axis);
    Serial.print(" recovery ");
    Serial.print(label);
    Serial.print(" command_us=");
    Serial.print(commandUs);
    Serial.print(" raw_before=");
    Serial.print(beforeRaw);
    Serial.print(" raw_after=");
    Serial.print(afterRaw);
    Serial.print(" progress=");
    Serial.println(progress);
    return progress;
  };

  int selectedUs = stopUs;
  int progress = runPulse(plusUs, "probe_plus");
  raw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  if (raw >= softLow && raw <= softHigh) {
    selectedUs = plusUs;
  } else if (progress >= kSoftRecoverMinProgressRaw) {
    selectedUs = plusUs;
  } else {
    progress = runPulse(minusUs, "probe_minus");
    raw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
    if (raw >= softLow && raw <= softHigh) {
      selectedUs = minusUs;
    } else if (progress >= kSoftRecoverMinProgressRaw) {
      selectedUs = minusUs;
    }
  }

  if (selectedUs == stopUs) {
    if (isYaw) {
      detachYawOutput("soft_window_recovery_no_progress");
    } else {
      detachPitchOutput("soft_window_recovery_no_progress");
    }
    Serial.print("[fleet][cal] ");
    Serial.print(axis);
    Serial.println(" recovery failed: neither PWM direction moved ADC toward center");
    return false;
  }

  int stagnantSteps = 0;
  for (int step = 0; step < kSoftRecoverMaxSteps; ++step) {
    updateCurrentAngles();
    raw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
    if (raw >= softLow && raw <= softHigh) break;

    progress = runPulse(selectedUs, "drive");
    updateCurrentAngles();
    raw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
    if (raw >= softLow && raw <= softHigh) break;

    if (progress < -kSoftRecoverMinProgressRaw) {
      Serial.print("[fleet][cal] ");
      Serial.print(axis);
      Serial.println(" recovery aborted: ADC moved away from center");
      break;
    }
    if (progress < kSoftRecoverMinProgressRaw) {
      stagnantSteps++;
      if (stagnantSteps >= 2) {
        Serial.print("[fleet][cal] ");
        Serial.print(axis);
        Serial.println(" recovery aborted: ADC stopped making progress");
        break;
      }
    } else {
      stagnantSteps = 0;
    }
  }

  if (isYaw) {
    detachYawOutput("soft_window_recovery_done");
  } else {
    detachPitchOutput("soft_window_recovery_done");
  }
  updateCurrentAngles();
  raw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  const bool safe = raw >= softLow && raw <= softHigh;
  Serial.print("[fleet][cal] ");
  Serial.print(axis);
  Serial.print(" recovery ");
  Serial.print(safe ? "safe" : "incomplete");
  Serial.print(" raw=");
  Serial.println(raw);
  return safe;
}

bool TurretControl::probeAxisDirection(const char* axis, bool positivePulse) {
  updateCurrentAngles();
  const bool isYaw = strcmp(axis, "yaw") == 0;
  const int beforeRaw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  const int softLow = isYaw ? yawSoftLowRaw() : pitchSoftLowRaw();
  const int softHigh = isYaw ? yawSoftHighRaw() : pitchSoftHighRaw();
  const int stopUs = isYaw ? config_.yawStopUs : config_.pitchStopUs;
  const int axisMaxDeltaUs = isYaw ? config_.yawMaxDeltaUs : config_.pitchMaxDeltaUs;
  const int probeDeltaUs = clampi(axisMaxDeltaUs < kBootProbeDeltaUs ? axisMaxDeltaUs : kBootProbeDeltaUs, 20, kBootProbeDeltaUs);
  const int pulseUs = clampi(stopUs + (positivePulse ? probeDeltaUs : -probeDeltaUs), 1000, 2000);

  if (isYaw && !kYawContinuousFeedback && (beforeRaw <= kYawLowCut || beforeRaw >= kYawHighCut)) {
    Serial.println("[fleet][cal] skip yaw probe at hard-edge feedback; manual/power recovery required");
    return false;
  }

  // Do not intentionally drive farther into the last 10% of a limited pot
  // range. Yaw uses continuous 360-degree feedback on the turret_fleet unit,
  // so raw values near 0/4095 are wrap points rather than physical hard stops.
  if ((!isYaw || !kYawContinuousFeedback) && beforeRaw <= softLow && positivePulse) {
    Serial.print("[fleet][cal] skip ");
    Serial.print(axis);
    Serial.println(" positive probe near low soft limit");
    return false;
  }
  if ((!isYaw || !kYawContinuousFeedback) && beforeRaw >= softHigh && !positivePulse) {
    Serial.print("[fleet][cal] skip ");
    Serial.print(axis);
    Serial.println(" negative probe near high soft limit");
    return false;
  }

  if (isYaw) {
    ensureYawAttached("boot_axis_probe");
    yawServo_.writeMicroseconds(pulseUs);
    yawLastCommandUs_ = pulseUs;
  } else {
    ensurePitchAttached("boot_axis_probe");
    pitchServo_.writeMicroseconds(pulseUs);
    pitchLastCommandUs_ = pulseUs;
  }
  delay(kBootProbeDriveMs);
  if (isYaw) {
    yawServo_.writeMicroseconds(config_.yawStopUs);
    yawLastCommandUs_ = config_.yawStopUs;
  } else {
    pitchServo_.writeMicroseconds(config_.pitchStopUs);
    pitchLastCommandUs_ = config_.pitchStopUs;
  }
  delay(kBootProbeStopMs);
  updateCurrentAngles();
  const int afterRaw = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  const int deltaRaw = afterRaw - beforeRaw;
  Serial.print("[fleet][cal] ");
  Serial.print(axis);
  Serial.print(" probe pulse_us=");
  Serial.print(pulseUs);
  Serial.print(" raw_before=");
  Serial.print(beforeRaw);
  Serial.print(" raw_after=");
  Serial.print(afterRaw);
  Serial.print(" delta=");
  Serial.println(deltaRaw);
  if (isYaw) {
    detachYawOutput("boot_axis_probe");
  } else {
    detachPitchOutput("boot_axis_probe");
  }
  return abs(deltaRaw) >= 5;
}

bool TurretControl::runBootAxisProbe() {
  updateCurrentAngles();
  Serial.print("[fleet][cal] boot probe start yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" yaw_deg=");
  Serial.print(yawCurrentDeg_, 2);
  Serial.print(" pitch_raw=");
  Serial.print(pitchRawCurrent_);
  Serial.print(" pitch_deg=");
  Serial.println(pitchCurrentDeg_, 2);

  const int yawBefore = yawRawCurrent_;
  const int yawSoftLow = yawSoftLowRaw();
  const int yawSoftHigh = yawSoftHighRaw();
  const int yawHomeRaw = yawRawForDeg(config_.homeYawDeg);
  const bool yawOutsideSafe = yawBefore < yawSoftLow || yawBefore > yawSoftHigh;
  const bool yawNeedsProbe = yawOutsideSafe;
  const bool yawProbePlus = yawBefore > yawHomeRaw || yawBefore >= yawSoftHigh;
  if (!yawNeedsProbe) {
    Serial.println("[fleet][cal] yaw boot probe skipped: already inside calibrated 150deg safe envelope");
  } else if (kYawContinuousFeedback) {
    Serial.println("[fleet][cal] yaw boot probe blocked: feedback outside calibrated 150deg safe envelope; use bounded jog/home before closed-loop zeroing");
    yawTrackingSuppressed_ = true;
    return false;
  } else if (probeAxisDirection("yaw", yawProbePlus)) {
    updateCurrentAngles();
    const int yawDelta = yawRawCurrent_ - yawBefore;
    if (yawDelta != 0) {
      // For positive yaw error, PID must choose the pulse direction that
      // increases raw/deg. If +PWM increases raw, invert=true; otherwise false.
      yawInvertMotor_ = yawDelta > 0 ? yawProbePlus : !yawProbePlus;
    }
  }

  delay(config_.axisSwitchCooldownMs);

  const int pitchBefore = pitchRawCurrent_;
  const int pitchSoftLow = pitchSoftLowRaw();
  const int pitchSoftHigh = pitchSoftHighRaw();
  const int pitchHomeRaw = pitchRawForDeg(config_.homePitchDeg);
  const bool pitchNeedsProbe = pitchBefore < pitchSoftLow || pitchBefore > pitchSoftHigh;
  const bool pitchProbePlus = pitchBefore > pitchHomeRaw || pitchBefore >= pitchSoftHigh;
  if (!pitchNeedsProbe) {
    Serial.println("[fleet][cal] pitch boot probe skipped: already inside calibrated 150deg safe envelope");
  } else if (probeAxisDirection("pitch", pitchProbePlus)) {
    updateCurrentAngles();
    const int pitchDelta = pitchRawCurrent_ - pitchBefore;
    if (pitchDelta != 0) {
      // For positive pitch error, PID must choose the pulse direction that
      // increases raw/deg. If +PWM increases raw, invert=true; otherwise false.
      pitchInvertMotor_ = pitchDelta > 0 ? pitchProbePlus : !pitchProbePlus;
    }
  }

  delay(config_.axisSwitchCooldownMs);
  stopMotionOutputs();
  updateCurrentAngles();
  Serial.print("[fleet][cal] boot probe done yaw_invert=");
  Serial.print(yawInvertMotor_ ? "true" : "false");
  Serial.print(" pitch_invert=");
  Serial.print(pitchInvertMotor_ ? "true" : "false");
  Serial.print(" yaw_raw=");
  Serial.print(yawRawCurrent_);
  Serial.print(" yaw_deg=");
  Serial.print(yawCurrentDeg_, 2);
  Serial.print(" pitch_raw=");
  Serial.print(pitchRawCurrent_);
  Serial.print(" pitch_deg=");
  Serial.println(pitchCurrentDeg_, 2);

  bool safeWindow = motionInsideSoftWindow();
  if (!safeWindow) {
    Serial.println("[fleet][cal] boot probe outside calibrated 150deg safe envelope; attempting bounded pitch feedback recovery");
    safeWindow = recoverMotionSoftWindow("boot_axis_probe");
  }
  if (!safeWindow) {
    motionSafetyInhibited_ = true;
    Serial.println("[fleet][cal] boot probe result outside calibrated 150deg safe envelope; initial home drive is inhibited");
  } else {
    motionSafetyInhibited_ = false;
  }
  return safeWindow;
}

void TurretControl::parkRelayPinsSafeOff() {
  if (kRelayActiveLow) {
    pinMode(kRelayCh1Pin, INPUT_PULLUP);
    pinMode(kRelayCh2Pin, INPUT_PULLUP);
    pinMode(kRelayCh3Pin, INPUT_PULLUP);
  } else {
    pinMode(kRelayCh1Pin, INPUT);
    pinMode(kRelayCh2Pin, INPUT);
    pinMode(kRelayCh3Pin, INPUT);
  }
  relayOutputsAttached_ = false;
  relayCh1On_ = false;
  relayCh2On_ = false;
  relayCh3On_ = false;
}

void TurretControl::relayWrite(int pin, bool on) {
  if (kRelayActiveLow) {
    digitalWrite(pin, on ? LOW : HIGH);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }

  if (pin == kRelayCh1Pin) {
    relayCh1On_ = on;
  } else if (pin == kRelayCh2Pin) {
    relayCh2On_ = on;
  } else if (pin == kRelayCh3Pin) {
    relayCh3On_ = on;
  }
}

void TurretControl::relayAllOff() {
  relayWrite(kRelayCh1Pin, false);
  relayWrite(kRelayCh2Pin, false);
  relayWrite(kRelayCh3Pin, false);
}

void TurretControl::ensureRelayOutputsAttached(const char* reason) {
  if (relayOutputsAttached_) return;

  Serial.print("[fleet][fire] attaching relay outputs for ");
  Serial.println(reason);

  digitalWrite(kRelayCh1Pin, kRelayActiveLow ? HIGH : LOW);
  digitalWrite(kRelayCh2Pin, kRelayActiveLow ? HIGH : LOW);
  digitalWrite(kRelayCh3Pin, kRelayActiveLow ? HIGH : LOW);
  pinMode(kRelayCh1Pin, OUTPUT);
  pinMode(kRelayCh2Pin, OUTPUT);
  pinMode(kRelayCh3Pin, OUTPUT);
  relayOutputsAttached_ = true;
  relayAllOff();
}

void TurretControl::ensureEscAttached(const char* reason) {
  if (escAttached_) return;

  Serial.print("[fleet][fire] attaching ESC for ");
  Serial.println(reason);
  esc_.setPeriodHertz(50);
  esc_.attach(kEscPin, kEscMinUs, kEscMaxUs);
  esc_.writeMicroseconds(config_.fireEscStopUs);
  escLastCommandUs_ = config_.fireEscStopUs;
  escAttached_ = true;
}

void TurretControl::ensureEscStopSignal(const char* reason) {
  ensureEscAttached(reason);
  esc_.writeMicroseconds(config_.fireEscStopUs);
  escLastCommandUs_ = config_.fireEscStopUs;
  Serial.print("[fleet][fire] ESC STOP signal ready for ");
  Serial.println(reason);
}

void TurretControl::runEscNow(const char* reason) {
  ensureEscAttached(reason);
  esc_.writeMicroseconds(config_.fireEscRunUs);
  escLastCommandUs_ = config_.fireEscRunUs;
  Serial.print("[fleet][fire] ESC RUN immediate for ");
  Serial.println(reason);
}

void TurretControl::forceFireOutputsSafeOff() {
  if (escAttached_) {
    esc_.writeMicroseconds(config_.fireEscStopUs);
    escLastCommandUs_ = config_.fireEscStopUs;
  }
  if (relayOutputsAttached_) {
    relayAllOff();
  }
  fireSequenceState_ = FIRE_SEQUENCE_IDLE;
  fireStateTs_ = 0;
  fireKeepAliveUntilMs_ = 0;
  fireHardOffAtMs_ = 0;
  fireStartedMs_ = 0;
  fireRequestedHoldMs_ = 0;
  fireRestartRequested_ = false;
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  fireState_ = "SAFE_OFF";
  writeFireRecoveryMarker(false);
}

void TurretControl::resetPidState() {
  yawPrevErrorPseudo_ = 0.0f;
  pitchPrevErrorPseudo_ = 0.0f;
  yawIntegralPseudo_ = 0.0f;
  pitchIntegralPseudo_ = 0.0f;
}

void TurretControl::updateCurrentAngles() {
  long yawSum = 0;
  long pitchSum = 0;
  for (int i = 0; i < 8; ++i) {
    yawSum += analogRead(kYawPotPin);
    pitchSum += analogRead(kPitchPotPin);
    delayMicroseconds(300);
  }
  yawRawCurrent_ = static_cast<int>(yawSum / 8);
  pitchRawCurrent_ = static_cast<int>(pitchSum / 8);
  const int yawFeedbackRaw = clampi(yawRawCurrent_, 0, 4095);
  const int pitchFeedbackRaw = clampi(pitchRawCurrent_, kPitchLowCut, kPitchHighCut);

  // Use a single monotonic yaw mapping for feedback control. The legacy
  // direction-dependent cubic mapping jumped between CW/CCW curves near the
  // same raw ADC value, so the controller saw false yaw reversals and tripped
  // the runaway guard even when the raw sensor was moving in the right
  // direction.
  yawWarpAdc_ = static_cast<float>(yawFeedbackRaw);
  const float yawSensorDeg = (yawWarpAdc_ - kYawLineB) / kYawLineM;
  yawCurrentDeg_ = clampf(yawSensorDeg + config_.yawAxisOffsetDeg, kYawMinDeg, kYawMaxDeg);

  const float pitchSensorDeg = (static_cast<float>(pitchFeedbackRaw - 2050) * 180.0f) / (3110.0f - 2050.0f);
  pitchCurrentDeg_ = clampf(pitchSensorDeg + config_.pitchAxisOffsetDeg, -180.0f, 180.0f);
}

void TurretControl::setTrackedTarget(float yawDeg, float pitchDeg) {
  yawTrackingSuppressed_ = false;
  yawGoalDeg_ = clampYawCommand(yawDeg);
  pitchGoalDeg_ = clampPitchCommand(pitchDeg);
  // Seed the intermediate setpoint immediately. Pattern MOVE steps are
  // started after the loop's normal motion-slew update, so leaving the target
  // equal to the current feedback for the first cycle can make the pattern
  // look "in progress" while the motor sees no error and never starts.
  yawTargetDeg_ = leadToward(yawCurrentDeg_, yawGoalDeg_, kTargetYawLeadDeg);
  pitchTargetDeg_ = leadToward(pitchCurrentDeg_, pitchGoalDeg_, kTargetPitchLeadDeg);
  targetSlewActive_ = fabs(yawTargetDeg_ - yawGoalDeg_) > 0.01f ||
                      fabs(pitchTargetDeg_ - pitchGoalDeg_) > 0.01f;
  lockedMotionAxis_ = 'N';
}

void TurretControl::setPitchOnlyTrackedTarget(float pitchDeg) {
  yawTrackingSuppressed_ = true;
  yawGoalDeg_ = yawCurrentDeg_;
  yawTargetDeg_ = yawCurrentDeg_;
  pitchGoalDeg_ = clampPitchCommand(pitchDeg);
  pitchTargetDeg_ = leadToward(pitchCurrentDeg_, pitchGoalDeg_, kTargetPitchLeadDeg);
  targetSlewActive_ = fabs(pitchTargetDeg_ - pitchGoalDeg_) > 0.01f;
  lockedMotionAxis_ = 'N';
  yawIntegralPseudo_ = 0.0f;
  yawPrevErrorPseudo_ = 0.0f;
  resetAxisGuard('Y');
}

void TurretControl::updateTrackedTargetSlew(float dtS) {
  (void)dtS;
  // Use the live sensor position as the base for the next closed-loop
  // waypoint. A previous implementation slewed the old setpoint toward the
  // goal; when the mechanism moved faster than that virtual setpoint, the
  // setpoint could be left on the wrong side of the final goal and drive the
  // pitch back upward after it had already crossed below the goal.
  yawTargetDeg_ = leadToward(yawCurrentDeg_, yawGoalDeg_, kTargetYawLeadDeg);
  pitchTargetDeg_ = leadToward(pitchCurrentDeg_, pitchGoalDeg_, kTargetPitchLeadDeg);
  targetSlewActive_ = fabs(yawTargetDeg_ - yawGoalDeg_) > 0.01f ||
                      fabs(pitchTargetDeg_ - pitchGoalDeg_) > 0.01f;
}

void TurretControl::updateIdleSweep(float dtS) {
  if (dtS <= 0.0f) dtS = 0.02f;
  if (dtS > 0.2f) dtS = 0.2f;

  const float yawDelta = config_.idleYawSpeedDegS * dtS;
  if (idleSweepForward_) {
    yawTargetDeg_ += yawDelta;
    if (yawTargetDeg_ >= config_.idleYawMaxDeg) {
      yawTargetDeg_ = config_.idleYawMaxDeg;
      idleSweepForward_ = false;
    }
  } else {
    yawTargetDeg_ -= yawDelta;
    if (yawTargetDeg_ <= config_.idleYawMinDeg) {
      yawTargetDeg_ = config_.idleYawMinDeg;
      idleSweepForward_ = true;
    }
  }

  const float pitchDelta = config_.idlePitchSpeedDegS * dtS;
  if (idlePitchUp_) {
    pitchTargetDeg_ += pitchDelta;
    if (pitchTargetDeg_ >= config_.idlePitchMaxDeg) {
      pitchTargetDeg_ = config_.idlePitchMaxDeg;
      idlePitchUp_ = false;
    }
  } else {
    pitchTargetDeg_ -= pitchDelta;
    if (pitchTargetDeg_ <= config_.idlePitchMinDeg) {
      pitchTargetDeg_ = config_.idlePitchMinDeg;
      idlePitchUp_ = true;
    }
  }
  yawTargetDeg_ = clampYawCommand(yawTargetDeg_);
  pitchTargetDeg_ = clampPitchCommand(pitchTargetDeg_);
}

void TurretControl::updateDeadTarget(float dtS) {
  yawGoalDeg_ = clampYawCommand(deadYawHoldDeg_);
  pitchGoalDeg_ = clampPitchCommand(config_.deadPitchDeg);
  updateTrackedTargetSlew(dtS);
}

void TurretControl::resetAxisGuard(char axis) {
  if (axis == 'Y' || axis == 'A') {
    yawGuardStartErrorDeg_ = 0.0f;
    yawGuardStartMs_ = 0;
    yawGuardSign_ = 0;
  }
  if (axis == 'P' || axis == 'A') {
    pitchGuardStartErrorDeg_ = 0.0f;
    pitchGuardStartMs_ = 0;
    pitchGuardSign_ = 0;
  }
}

bool TurretControl::axisConvergenceAllowed(const char* axis,
                                           float goalErrorDeg,
                                           float& guardStartErrorDeg,
                                           unsigned long& guardStartMs,
                                           int& guardSign) {
  const float absError = fabs(goalErrorDeg);
  if (kUnsafeManualCalibrationMode) {
    guardStartErrorDeg = absError;
    guardStartMs = millis();
    guardSign = 0;
    return true;
  }
  if (absError <= 2.0f || config_.axisDivergenceGuardMs == 0) {
    guardStartErrorDeg = absError;
    guardStartMs = millis();
    guardSign = 0;
    return true;
  }

  const int sign = goalErrorDeg >= 0.0f ? 1 : -1;
  const unsigned long now = millis();
  if (guardStartMs == 0 || guardSign != sign) {
    guardStartErrorDeg = absError;
    guardStartMs = now;
    guardSign = sign;
    return true;
  }

  if (now - guardStartMs >= config_.axisDivergenceGuardMs &&
      absError > guardStartErrorDeg + config_.axisDivergenceMarginDeg) {
    lastError_ = String(axis) + " divergence guard: goal error increased from " +
                 String(guardStartErrorDeg, 2) + "deg to " + String(absError, 2) +
                 "deg; check motor direction/sensor zero/calibration";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
    guardStartErrorDeg = absError;
    guardStartMs = now;
    guardSign = sign;
    return false;
  }

  if (absError < guardStartErrorDeg) {
    guardStartErrorDeg = absError;
    guardStartMs = now;
  }
  return true;
}

void TurretControl::runPidAxis(Servo& servo,
                               float currentDeg,
                               float targetDeg,
                               float adcPerDeg,
                               float deadband,
                               float maxDeltaUs,
                               float minDriveUs,
                               float plusMaxDeltaUs,
                               float plusMinDriveUs,
                               float minusMaxDeltaUs,
                               float minusMinDriveUs,
                               bool invertMotor,
                               int stopUs,
                               float& prevErrorPseudo,
                               float& integralPseudo,
                               int& lastCommandUs) {
  const float errorDeg = targetDeg - currentDeg;
  const float errorPseudo = errorDeg * adcPerDeg;

  if (fabs(errorPseudo) < deadband) {
    servo.writeMicroseconds(stopUs);
    lastCommandUs = stopUs;
    integralPseudo *= 0.90f;
    prevErrorPseudo = errorPseudo;
    return;
  }

  integralPseudo += errorPseudo;
  integralPseudo = clampf(integralPseudo, -((adcPerDeg == kYawAdcPerDeg) ? kYawILimit : kPitchILimit),
                          ((adcPerDeg == kYawAdcPerDeg) ? kYawILimit : kPitchILimit));
  float output = kKp * errorPseudo + kKi * integralPseudo + kKd * (errorPseudo - prevErrorPseudo);
  prevErrorPseudo = errorPseudo;

  if (invertMotor) output = -output;

  // output < 0 maps to stopUs + delta because commandUs = stopUs - output.
  // Direction overrides are PWM-relative so each turret can compensate for
  // asymmetric yaw load without guessing clockwise/counter-clockwise labels.
  const bool plusPwmDirection = output < 0.0f;
  float directionalMaxDeltaUs = plusPwmDirection ? plusMaxDeltaUs : minusMaxDeltaUs;
  float directionalMinDriveUs = plusPwmDirection ? plusMinDriveUs : minusMinDriveUs;
  if (directionalMaxDeltaUs <= 0.0f) directionalMaxDeltaUs = maxDeltaUs;
  if (directionalMinDriveUs <= 0.0f) directionalMinDriveUs = minDriveUs;
  if (directionalMinDriveUs > directionalMaxDeltaUs) directionalMinDriveUs = directionalMaxDeltaUs;

  output = clampf(output, -directionalMaxDeltaUs, directionalMaxDeltaUs);
  if (fabs(output) < directionalMinDriveUs) {
    output = plusPwmDirection ? -directionalMinDriveUs : directionalMinDriveUs;
  }

  const int commandUs = clampi(static_cast<int>(stopUs - output), 1000, 2000);
  servo.writeMicroseconds(commandUs);
  lastCommandUs = commandUs;
}

bool TurretControl::isFireSequenceActive() const {
  return fireSequenceState_ != FIRE_SEQUENCE_IDLE;
}

bool TurretControl::aimReached() const {
  const bool finalTargetMode = mode_ == "HOME" || mode_ == "TARGET" || mode_ == "PATTERN";
  const float yawFinal = finalTargetMode ? yawGoalDeg_ : yawTargetDeg_;
  const float pitchFinal = finalTargetMode ? pitchGoalDeg_ : pitchTargetDeg_;
  return (!finalTargetMode || !targetSlewActive_) &&
         fabs(yawFinal - yawCurrentDeg_) <= kAimReachedToleranceDeg &&
         fabs(pitchFinal - pitchCurrentDeg_) <= kAimReachedToleranceDeg;
}

bool TurretControl::patternSweepYawReached() const {
  // lane_sweep is a yaw-readable attack: fire should cut as soon as the yaw
  // sweep reaches the lane edge, even if pitch is still settling inside the
  // safe envelope. Point-shot patterns keep using aimReached() so they wait
  // for both axes before firing.
  return fabs(yawGoalDeg_ - yawCurrentDeg_) <= kAimReachedToleranceDeg;
}

const char* TurretControl::fireSequenceName() const {
  switch (fireSequenceState_) {
    case FIRE_SEQUENCE_IDLE:
      return "IDLE";
    case FIRE_SEQUENCE_CH2_ON_WAIT:
      return "CH2_ON_WAIT";
    case FIRE_SEQUENCE_CH1_ON_WAIT:
      return "CH1_ON_WAIT";
    case FIRE_SEQUENCE_CH3_ON_WAIT:
      return "CH3_ON_WAIT";
    case FIRE_SEQUENCE_RUNNING:
      return "RUNNING";
    case FIRE_SEQUENCE_ESC_OFF_WAIT:
      return "ESC_OFF_WAIT";
    case FIRE_SEQUENCE_CH3_OFF_WAIT:
      return "CH3_OFF_WAIT";
    case FIRE_SEQUENCE_CH1_OFF_WAIT:
      return "CH1_OFF_WAIT";
    case FIRE_SEQUENCE_CH2_OFF_WAIT:
      return "CH2_OFF_WAIT";
  }
  return "UNKNOWN";
}

void TurretControl::startFireSequence(unsigned long holdMs, const char* source, bool keepMotionTracking) {
  ensureEscStopSignal("fire");
  ensureRelayOutputsAttached("fire");
  if (!keepMotionTracking) {
    stopMotionOutputs();
  }

  const unsigned long now = millis();
  fireStartedMs_ = now;
  fireRequestedHoldMs_ = holdMs;
  fireKeepAliveUntilMs_ = 0;
  fireHardOffAtMs_ = now + fireHardOffDelayMs(config_, holdMs);
  fireRestartRequested_ = false;
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;

  writeFireRecoveryMarker(true);
  runEscNow("fire-command");
  relayWrite(kRelayCh2Pin, true);
  fireSequenceState_ = FIRE_SEQUENCE_CH2_ON_WAIT;
  fireStateTs_ = now;
  fireState_ = "FIRING";
  mode_ = keepMotionTracking ? String("PATTERN") : String("FIRING");

  Serial.print("[fleet][fire] sequence started source=");
  Serial.print(source);
  Serial.print(" hold_ms=");
  Serial.print(holdMs);
  Serial.print(" esc_run_us=");
  Serial.print(config_.fireEscRunUs);
  Serial.println(" relay_order=CH2->CH1->CH3");
}

void TurretControl::ensurePatternSweepFire(unsigned long holdMs) {
  if (!isFireSequenceActive()) {
    startFireSequence(holdMs, "PATTERN(sweep_fire)", true);
    return;
  }

  fireRequestedHoldMs_ = holdMs;
  bool restartRequested = false;
  if (fireSequenceState_ == FIRE_SEQUENCE_RUNNING && fireKeepAliveUntilMs_ != 0) {
    const unsigned long now = millis();
    fireKeepAliveUntilMs_ = now + holdMs;
    fireHardOffAtMs_ = now + fireHardOffDelayMs(config_, holdMs);
  } else if (fireSequenceState_ == FIRE_SEQUENCE_ESC_OFF_WAIT ||
             fireSequenceState_ == FIRE_SEQUENCE_CH3_OFF_WAIT ||
             fireSequenceState_ == FIRE_SEQUENCE_CH1_OFF_WAIT ||
             fireSequenceState_ == FIRE_SEQUENCE_CH2_OFF_WAIT) {
    fireRestartRequested_ = true;
    restartRequested = true;
  }
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  postFireMode_ = "PATTERN";
  mode_ = "PATTERN";
  fireState_ = "FIRING";
  if (restartRequested) {
    Serial.print("[fleet][pattern] sweep fire restart requested hold_ms=");
    Serial.println(holdMs);
  }
}

void TurretControl::stopPatternSweepFireAtEndpoint(const char* source) {
  if (!isFireSequenceActive() && fireState_ != "FIRING") return;

  Serial.print("[fleet][pattern] sweep fire cut at ");
  Serial.println(source);
  forceFireOutputsSafeOff();
  postFireMode_ = "PATTERN";
  mode_ = "PATTERN";
}

void TurretControl::updateFireSequence() {
  if (!isFireSequenceActive()) return;

  const unsigned long now = millis();

  if (fireHardOffAtMs_ != 0 && now >= fireHardOffAtMs_) {
    Serial.print("[fleet][fire] hard off after wall-clock cap elapsed_ms=");
    Serial.println(fireStartedMs_ == 0 ? 0 : now - fireStartedMs_);
    forceFireOutputsSafeOff();
    return;
  }

  if (fireSequenceState_ == FIRE_SEQUENCE_RUNNING &&
      fireKeepAliveUntilMs_ != 0 && now >= fireKeepAliveUntilMs_) {
    if (escAttached_ && escLastCommandUs_ != config_.fireEscStopUs) {
      esc_.writeMicroseconds(config_.fireEscStopUs);
      escLastCommandUs_ = config_.fireEscStopUs;
      Serial.print("[fleet][fire] ESC STOP after hold_ms=");
      Serial.print(fireRequestedHoldMs_);
      Serial.print(" elapsed_ms=");
      Serial.println(now - fireStartedMs_);
    }
    fireSequenceState_ = FIRE_SEQUENCE_ESC_OFF_WAIT;
    fireStateTs_ = now;
  }

  switch (fireSequenceState_) {
    case FIRE_SEQUENCE_IDLE:
      return;

    case FIRE_SEQUENCE_CH2_ON_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        relayWrite(kRelayCh1Pin, true);
        fireSequenceState_ = FIRE_SEQUENCE_CH1_ON_WAIT;
        fireStateTs_ = now;
        Serial.println("[fleet][fire] relay CH1 ON");
      }
      break;

    case FIRE_SEQUENCE_CH1_ON_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        relayWrite(kRelayCh3Pin, true);
        fireSequenceState_ = FIRE_SEQUENCE_CH3_ON_WAIT;
        fireStateTs_ = now;
        Serial.println("[fleet][fire] relay CH3 ON");
      }
      break;

    case FIRE_SEQUENCE_CH3_ON_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        fireSequenceState_ = FIRE_SEQUENCE_RUNNING;
        fireStateTs_ = now;
        fireStartedMs_ = now;
        fireKeepAliveUntilMs_ = now + fireRequestedHoldMs_;
        Serial.println("[fleet][fire] relay sequence complete; ESC hold active");
      }
      break;

    case FIRE_SEQUENCE_RUNNING:
      break;

    case FIRE_SEQUENCE_ESC_OFF_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        if (relayCh3On_) {
          relayWrite(kRelayCh3Pin, false);
          Serial.println("[fleet][fire] relay CH3 OFF");
        }
        fireSequenceState_ = FIRE_SEQUENCE_CH3_OFF_WAIT;
        fireStateTs_ = now;
      }
      break;

    case FIRE_SEQUENCE_CH3_OFF_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        if (relayCh1On_) {
          relayWrite(kRelayCh1Pin, false);
          Serial.println("[fleet][fire] relay CH1 OFF");
        }
        fireSequenceState_ = FIRE_SEQUENCE_CH1_OFF_WAIT;
        fireStateTs_ = now;
      }
      break;

    case FIRE_SEQUENCE_CH1_OFF_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        if (relayCh2On_) {
          relayWrite(kRelayCh2Pin, false);
          Serial.println("[fleet][fire] relay CH2 OFF");
        }
        fireSequenceState_ = FIRE_SEQUENCE_CH2_OFF_WAIT;
        fireStateTs_ = now;
      }
      break;

    case FIRE_SEQUENCE_CH2_OFF_WAIT:
      if (now - fireStateTs_ >= config_.fireRelayStepDelayMs) {
        relayAllOff();
        fireSequenceState_ = FIRE_SEQUENCE_IDLE;
        fireStateTs_ = now;
        fireKeepAliveUntilMs_ = 0;
        fireHardOffAtMs_ = 0;
        fireStartedMs_ = 0;
        fireRequestedHoldMs_ = 0;
        fireState_ = "SAFE_OFF";
        writeFireRecoveryMarker(false);
        Serial.println("[fleet][fire] sequence complete -> SAFE_OFF");
        if (fireRestartRequested_) {
          fireRestartRequested_ = false;
          const bool restartForPattern = postFireMode_ == "PATTERN";
          startFireSequence(config_.fireDefaultHoldMs, "queued_keepalive", restartForPattern);
        } else if (mode_ == "FIRING") {
          mode_ = postFireMode_.length() > 0 ? postFireMode_ : String("WAIT_COMMAND");
        }
      }
      break;
  }
}

void TurretControl::loop() {
  const unsigned long now = millis();
  const float dtS = (lastLoopMs_ == 0) ? 0.02f : (now - lastLoopMs_) / 1000.0f;

  updateFireSequence();

  const bool motionMode = mode_ == "HOME" || mode_ == "IDLE" || mode_ == "TARGET" || mode_ == "DEAD" || mode_ == "PATTERN";
  if (motionEnabled_ || motionMode) {
    updateCurrentAngles();
    if (mode_ == "WAIT_COMMAND" || mode_ == "UNCONFIGURED") {
      // WAIT_COMMAND/HOLD is not tracking a commanded coordinate. Keep the
      // reported target glued to the live sensor position so status does not
      // show a stale idle/target error after outputs have been stopped.
      yawTargetDeg_ = yawCurrentDeg_;
      pitchTargetDeg_ = pitchCurrentDeg_;
      yawGoalDeg_ = yawCurrentDeg_;
      pitchGoalDeg_ = pitchCurrentDeg_;
      targetSlewActive_ = false;
      if (!brownoutLockoutActive_) {
        motionSafetyInhibited_ = !motionInsideSoftWindow();
      }
    } else if (mode_ == "HOME" || mode_ == "TARGET" || mode_ == "PATTERN") {
      updateTrackedTargetSlew(dtS);
    } else if (mode_ == "IDLE") {
      updateIdleSweep(dtS);
    } else if (mode_ == "DEAD") {
      updateDeadTarget(dtS);
    }

      if (yawTrackingSuppressed_) {
        yawGoalDeg_ = yawCurrentDeg_;
        yawTargetDeg_ = yawCurrentDeg_;
        detachYawOutput("yaw_tracking_suppressed");
        yawIntegralPseudo_ = 0.0f;
        yawPrevErrorPseudo_ = 0.0f;
        resetAxisGuard('Y');
      }

    if (motionMode) {
      bool driveYaw = !yawTrackingSuppressed_ && fabs(yawTargetDeg_ - yawCurrentDeg_) * kYawAdcPerDeg >= kYawDeadbandPseudo;
      bool drivePitch = fabs(pitchTargetDeg_ - pitchCurrentDeg_) * kPitchAdcPerDeg >= kPitchDeadbandPseudo;
      const bool yawFeedbackRailRisk = yawRawCurrent_ <= kYawFeedbackRailLowCut ||
                                       yawRawCurrent_ >= kYawFeedbackRailHighCut;
      if (!kUnsafeManualCalibrationMode && driveYaw && yawFeedbackRailRisk) {
        driveYaw = false;
        detachYawOutput("yaw_feedback_rail_guard");
        yawIntegralPseudo_ = 0.0f;
        yawPrevErrorPseudo_ = 0.0f;
        resetAxisGuard('Y');
        if (lastError_.indexOf("yaw feedback rail guard") < 0) {
          lastError_ = String("yaw feedback rail guard: raw=") + String(yawRawCurrent_);
          Serial.print("[fleet][motion] ");
          Serial.println(lastError_);
        }
      }
      const int yawSoftLow = yawSoftLowRaw();
      const int yawSoftHigh = yawSoftHighRaw();
      const int pitchSoftLow = pitchSoftLowRaw();
      const int pitchSoftHigh = pitchSoftHighRaw();
      const bool yawSoftLowOutward = yawRawCurrent_ <= yawSoftLow &&
                                     yawTargetDeg_ <= yawCurrentDeg_;
      const bool yawSoftHighOutward = yawRawCurrent_ >= yawSoftHigh &&
                                      yawTargetDeg_ >= yawCurrentDeg_;
      if (!kUnsafeManualCalibrationMode && driveYaw && (yawSoftLowOutward || yawSoftHighOutward)) {
        driveYaw = false;
        detachYawOutput("yaw_soft_limit_guard");
        yawIntegralPseudo_ = 0.0f;
        yawPrevErrorPseudo_ = 0.0f;
        if (lastError_.indexOf("yaw soft limit guard") < 0) {
          lastError_ = yawSoftLowOutward ? "yaw soft limit guard: low edge, outward drive blocked" :
                                           "yaw soft limit guard: high edge, outward drive blocked";
          Serial.print("[fleet][motion] ");
          Serial.println(lastError_);
        }
      }
      const bool pitchUpperGuard = pitchCurrentDeg_ >= config_.pitchMaxDeg && pitchTargetDeg_ >= pitchCurrentDeg_;
      const bool pitchLowerGuard = pitchCurrentDeg_ <= config_.pitchMinDeg && pitchTargetDeg_ <= pitchCurrentDeg_;
      const bool pitchSoftLowOutward = pitchRawCurrent_ <= pitchSoftLow && pitchTargetDeg_ <= pitchCurrentDeg_;
      const bool pitchSoftHighOutward = pitchRawCurrent_ >= pitchSoftHigh && pitchTargetDeg_ >= pitchCurrentDeg_;
      if (!kUnsafeManualCalibrationMode && drivePitch && (pitchSoftLowOutward || pitchSoftHighOutward)) {
        drivePitch = false;
        detachPitchOutput("pitch_soft_limit_guard");
        pitchIntegralPseudo_ = 0.0f;
        pitchPrevErrorPseudo_ = 0.0f;
        if (lastError_.indexOf("pitch soft limit guard") < 0) {
          lastError_ = pitchSoftLowOutward ? "pitch soft limit guard: low edge, outward drive blocked" :
                                             "pitch soft limit guard: high edge, outward drive blocked";
          Serial.print("[fleet][motion] ");
          Serial.println(lastError_);
        }
      }
      if (!kUnsafeManualCalibrationMode && (pitchUpperGuard || pitchLowerGuard)) {
        drivePitch = false;
        detachPitchOutput("pitch_safety_guard");
        pitchIntegralPseudo_ = 0.0f;
        pitchPrevErrorPseudo_ = 0.0f;
        if (lastError_.indexOf("pitch safety guard") < 0) {
          lastError_ = pitchUpperGuard ? "pitch safety guard: at upper limit" :
                                         "pitch safety guard: at lower limit";
          Serial.print("[fleet][motion] ");
          Serial.println(lastError_);
        }
      } else if (lastError_.startsWith("pitch safety guard")) {
        lastError_ = "";
      }

      const float yawGoalErrorDeg = fabs(yawGoalDeg_ - yawCurrentDeg_);
      const float pitchGoalErrorDeg = fabs(pitchGoalDeg_ - pitchCurrentDeg_);
      const bool yawNeedsGoal = driveYaw && yawGoalErrorDeg > kAimReachedToleranceDeg;
      const bool pitchNeedsGoal = drivePitch && pitchGoalErrorDeg > kAimReachedToleranceDeg;
      const bool keepYawLock = lockedMotionAxis_ == 'Y' && yawNeedsGoal &&
                               !(pitchNeedsGoal &&
                                 yawGoalErrorDeg <= kAxisLockReleaseDeg &&
                                 pitchGoalErrorDeg > yawGoalErrorDeg + kAxisSwitchHysteresisDeg);
      const bool keepPitchLock = lockedMotionAxis_ == 'P' && pitchNeedsGoal &&
                                 !(yawNeedsGoal &&
                                   pitchGoalErrorDeg <= kAxisLockReleaseDeg &&
                                   yawGoalErrorDeg > pitchGoalErrorDeg + kAxisSwitchHysteresisDeg);
      char desiredAxis = 'N';
      if (keepYawLock) {
        desiredAxis = 'Y';
      } else if (keepPitchLock) {
        desiredAxis = 'P';
      } else if (driveYaw && drivePitch) {
        // Drive exactly one axis at a time. The previous implementation
        // selected one axis but attached the new axis before detaching the old
        // one during Y<->P switches, creating a brief two-servo inrush that
        // matched the observed brownout. Lock onto one axis until that axis is
        // close to its final goal. Once the locked axis is within the readable
        // near-target band, allow a much larger opposite-axis error to take
        // ownership so HOME/pattern moves do not starve pitch behind a tiny
        // residual yaw error.
        const bool pitchNeedsUpFromLowAngle =
            pitchCurrentDeg_ <= kPitchUpPriorityBelowDeg &&
            pitchGoalDeg_ > pitchCurrentDeg_ &&
            pitchGoalErrorDeg > kAimReachedToleranceDeg;
        desiredAxis = pitchNeedsUpFromLowAngle || pitchGoalErrorDeg >= yawGoalErrorDeg ? 'P' : 'Y';
      } else if (driveYaw) {
        desiredAxis = 'Y';
      } else if (drivePitch) {
        desiredAxis = 'P';
      }
      lockedMotionAxis_ = desiredAxis;

      if (desiredAxis != 'Y') {
        detachYawOutput(desiredAxis == 'P' ? "axis_switch_to_pitch" : "motion_loop_axis_idle");
        yawIntegralPseudo_ = 0.0f;
        yawPrevErrorPseudo_ = 0.0f;
        resetAxisGuard('Y');
      }
      if (desiredAxis != 'P') {
        detachPitchOutput(desiredAxis == 'Y' ? "axis_switch_to_yaw" : "motion_loop_axis_idle");
        pitchIntegralPseudo_ = 0.0f;
        pitchPrevErrorPseudo_ = 0.0f;
        resetAxisGuard('P');
      }

      const bool axisCoolingDown = desiredAxis != 'N' &&
                                   !yawServoAttached_ && !pitchServoAttached_ &&
                                   lastAxisSwitchMs_ != 0 &&
                                   now - lastAxisSwitchMs_ < config_.axisSwitchCooldownMs;
      if (axisCoolingDown) {
        selectedMotionAxis_ = 'N';
      } else if (desiredAxis == 'Y') {
        if (!axisConvergenceAllowed("yaw", yawGoalDeg_ - yawCurrentDeg_,
                                    yawGuardStartErrorDeg_, yawGuardStartMs_, yawGuardSign_)) {
          stopMotionOutputs();
          resetPidState();
          lockedMotionAxis_ = 'N';
          if (patternPlan_.kind != PATTERN_NONE) {
            abortPattern(lastError_.length() > 0 ? lastError_.c_str() : "pattern aborted: yaw divergence guard");
          } else {
            mode_ = "WAIT_COMMAND";
          }
        } else {
          ensureYawAttached("motion_loop");
          selectedMotionAxis_ = 'Y';
          if (now - yawAttachedAtMs_ < config_.servoAttachSettleMs) {
            yawServo_.writeMicroseconds(config_.yawStopUs);
            yawLastCommandUs_ = config_.yawStopUs;
          } else {
            const bool yawOutsideSoft = yawRawCurrent_ <= yawSoftLow ||
                                        yawRawCurrent_ >= yawSoftHigh;
            int yawMaxDeltaInt = config_.yawMaxDeltaUs;
            if (yawMaxDeltaInt > kTrackingYawMaxDeltaUs) yawMaxDeltaInt = kTrackingYawMaxDeltaUs;
            if (yawOutsideSoft && yawMaxDeltaInt > kSoftLimitRescueDeltaUs) {
              yawMaxDeltaInt = kSoftLimitRescueDeltaUs;
            }
            int yawPlusMaxDeltaInt = config_.yawPlusMaxDeltaUs > 0
                                        ? config_.yawPlusMaxDeltaUs
                                        : yawMaxDeltaInt;
            int yawMinusMaxDeltaInt = config_.yawMinusMaxDeltaUs > 0
                                         ? config_.yawMinusMaxDeltaUs
                                         : yawMaxDeltaInt;
            if (yawPlusMaxDeltaInt > kTrackingYawMaxDeltaUs) yawPlusMaxDeltaInt = kTrackingYawMaxDeltaUs;
            if (yawMinusMaxDeltaInt > kTrackingYawMaxDeltaUs) yawMinusMaxDeltaInt = kTrackingYawMaxDeltaUs;
            if (yawOutsideSoft && yawPlusMaxDeltaInt > kSoftLimitRescueDeltaUs) {
              yawPlusMaxDeltaInt = kSoftLimitRescueDeltaUs;
            }
            if (yawOutsideSoft && yawMinusMaxDeltaInt > kSoftLimitRescueDeltaUs) {
              yawMinusMaxDeltaInt = kSoftLimitRescueDeltaUs;
            }
            const int yawMinDriveInt = config_.yawMinDriveUs > yawMaxDeltaInt
                                         ? yawMaxDeltaInt
                                         : config_.yawMinDriveUs;
            int yawPlusMinDriveInt = config_.yawPlusMinDriveUs > 0
                                        ? config_.yawPlusMinDriveUs
                                        : yawMinDriveInt;
            int yawMinusMinDriveInt = config_.yawMinusMinDriveUs > 0
                                         ? config_.yawMinusMinDriveUs
                                         : yawMinDriveInt;
            if (yawPlusMinDriveInt > yawPlusMaxDeltaInt) yawPlusMinDriveInt = yawPlusMaxDeltaInt;
            if (yawMinusMinDriveInt > yawMinusMaxDeltaInt) yawMinusMinDriveInt = yawMinusMaxDeltaInt;
            const float yawMaxDeltaUs = static_cast<float>(yawMaxDeltaInt);
            const float yawMinDriveUs = static_cast<float>(yawMinDriveInt);
            runPidAxis(yawServo_, yawCurrentDeg_, yawTargetDeg_, kYawAdcPerDeg,
                       kYawDeadbandPseudo, yawMaxDeltaUs, yawMinDriveUs,
                       static_cast<float>(yawPlusMaxDeltaInt), static_cast<float>(yawPlusMinDriveInt),
                       static_cast<float>(yawMinusMaxDeltaInt), static_cast<float>(yawMinusMinDriveInt),
                       yawInvertMotor_, config_.yawStopUs,
                       yawPrevErrorPseudo_, yawIntegralPseudo_, yawLastCommandUs_);
          }
        }
      } else if (desiredAxis == 'P') {
        if (!axisConvergenceAllowed("pitch", pitchGoalDeg_ - pitchCurrentDeg_,
                                    pitchGuardStartErrorDeg_, pitchGuardStartMs_, pitchGuardSign_)) {
          stopMotionOutputs();
          resetPidState();
          lockedMotionAxis_ = 'N';
          if (patternPlan_.kind != PATTERN_NONE) {
            abortPattern(lastError_.length() > 0 ? lastError_.c_str() : "pattern aborted: pitch divergence guard");
          } else {
            mode_ = "WAIT_COMMAND";
          }
        } else {
          ensurePitchAttached("motion_loop");
          selectedMotionAxis_ = 'P';
          if (now - pitchAttachedAtMs_ < config_.servoAttachSettleMs) {
            pitchServo_.writeMicroseconds(config_.pitchStopUs);
            pitchLastCommandUs_ = config_.pitchStopUs;
          } else {
            const bool pitchOutsideSoft = pitchRawCurrent_ <= pitchSoftLow || pitchRawCurrent_ >= pitchSoftHigh;
            int pitchMaxDeltaInt = config_.pitchMaxDeltaUs;
            if (pitchMaxDeltaInt > kTrackingPitchMaxDeltaUs) pitchMaxDeltaInt = kTrackingPitchMaxDeltaUs;
            if (pitchOutsideSoft && pitchMaxDeltaInt > kSoftLimitRescueDeltaUs) {
              pitchMaxDeltaInt = kSoftLimitRescueDeltaUs;
            }
            const int pitchMinDriveInt = config_.pitchMinDriveUs > pitchMaxDeltaInt
                                           ? pitchMaxDeltaInt
                                           : config_.pitchMinDriveUs;
            const float pitchMaxDeltaUs = static_cast<float>(pitchMaxDeltaInt);
            const float pitchMinDriveUs = static_cast<float>(pitchMinDriveInt);
            runPidAxis(pitchServo_, pitchCurrentDeg_, pitchTargetDeg_, kPitchAdcPerDeg,
                       kPitchDeadbandPseudo, pitchMaxDeltaUs, pitchMinDriveUs,
                       pitchMaxDeltaUs, pitchMinDriveUs,
                       pitchMaxDeltaUs, pitchMinDriveUs,
                       pitchInvertMotor_, config_.pitchStopUs,
                       pitchPrevErrorPseudo_, pitchIntegralPseudo_, pitchLastCommandUs_);
          }
        }
      } else {
        selectedMotionAxis_ = 'N';
        lockedMotionAxis_ = 'N';
        resetAxisGuard('A');
      }
    }
  }

  const bool finiteAimMode = mode_ == "HOME" || mode_ == "TARGET";
  if (finiteAimMode && aimReached()) {
    if (aimReachedSinceMs_ == 0) aimReachedSinceMs_ = now;
    if (now - aimReachedSinceMs_ >= kAimStableBeforeCompleteMs) {
      stopMotionOutputs();
      resetPidState();
      selectedMotionAxis_ = 'N';
      lockedMotionAxis_ = 'N';
      targetSlewActive_ = false;
      mode_ = "WAIT_COMMAND";
      lastError_ = "";
    }
  } else {
    aimReachedSinceMs_ = 0;
  }

  if (patternPlan_.kind != PATTERN_NONE) {
    updatePattern();
  }

  if (now - lastMotionDebugMs_ >= 1000) {
    lastMotionDebugMs_ = now;
    if (motionEnabled_ || mode_ == "TARGET" || mode_ == "IDLE" || mode_ == "DEAD" || mode_ == "PATTERN") {
      Serial.print("[fleet][motion] mode=");
      Serial.print(mode_);
      Serial.print(" yaw_raw=");
      Serial.print(yawRawCurrent_);
      Serial.print(" yaw_cur=");
      Serial.print(yawCurrentDeg_, 2);
      Serial.print(" yaw_tgt=");
      Serial.print(yawTargetDeg_, 2);
      Serial.print(" yaw_goal=");
      Serial.print(yawGoalDeg_, 2);
      Serial.print(" yaw_us=");
      Serial.print(yawLastCommandUs_);
      Serial.print(" yaw_att=");
      Serial.print(yawServoAttached_ ? "Y" : "N");
      Serial.print(" pitch_raw=");
      Serial.print(pitchRawCurrent_);
      Serial.print(" pitch_cur=");
      Serial.print(pitchCurrentDeg_, 2);
      Serial.print(" pitch_tgt=");
      Serial.print(pitchTargetDeg_, 2);
      Serial.print(" pitch_goal=");
      Serial.print(pitchGoalDeg_, 2);
      Serial.print(" pitch_us=");
      Serial.print(pitchLastCommandUs_);
      Serial.print(" pitch_att=");
      Serial.print(pitchServoAttached_ ? "Y" : "N");
      Serial.print(" axis=");
      Serial.print(selectedMotionAxis_);
      Serial.print(" lock=");
      Serial.print(lockedMotionAxis_);
      Serial.print(" slew=");
      Serial.print(targetSlewActive_ ? "Y" : "N");
      Serial.print(" aim_reached=");
      Serial.println(aimReached() ? "Y" : "N");
    }
  }

  lastLoopMs_ = now;
}

bool TurretControl::validateFrame(JsonVariantConst frameVariant, const char* command, const char* source) {
  const char* frame = frameVariant | "";
  if (frame[0] == '\0') return true;
  if (config_.frameId == frame) return true;

  lastError_ = String("frame_id mismatch for ") + command + ": expected=" + config_.frameId + " got=" + frame;
  Serial.print("[fleet][control] rejected from ");
  Serial.print(source);
  Serial.print(": ");
  Serial.println(lastError_);
  return false;
}

float TurretControl::targetUnitToCm(float value) const {
  return mqttTargetsInMeters(config_) ? value * 100.0f : value;
}

float TurretControl::yawCommandMinDeg() const {
  const float a = commandEnvelopeEdge(config_.yawMinDeg, config_.homeYawDeg, config_.commandEnvelopeRatio);
  const float b = commandEnvelopeEdge(config_.yawMaxDeg, config_.homeYawDeg, config_.commandEnvelopeRatio);
  return a < b ? a : b;
}

float TurretControl::yawCommandMaxDeg() const {
  const float a = commandEnvelopeEdge(config_.yawMinDeg, config_.homeYawDeg, config_.commandEnvelopeRatio);
  const float b = commandEnvelopeEdge(config_.yawMaxDeg, config_.homeYawDeg, config_.commandEnvelopeRatio);
  return a > b ? a : b;
}

float TurretControl::pitchCommandMinDeg() const {
  const float a = commandEnvelopeEdge(config_.pitchMinDeg, config_.homePitchDeg, config_.commandEnvelopeRatio);
  const float b = commandEnvelopeEdge(config_.pitchMaxDeg, config_.homePitchDeg, config_.commandEnvelopeRatio);
  return a < b ? a : b;
}

float TurretControl::pitchCommandMaxDeg() const {
  const float a = commandEnvelopeEdge(config_.pitchMinDeg, config_.homePitchDeg, config_.commandEnvelopeRatio);
  const float b = commandEnvelopeEdge(config_.pitchMaxDeg, config_.homePitchDeg, config_.commandEnvelopeRatio);
  return a > b ? a : b;
}

float TurretControl::clampYawCommand(float value) const {
  return clampf(value, yawCommandMinDeg(), yawCommandMaxDeg());
}

float TurretControl::clampPitchCommand(float value) const {
  return clampf(value, pitchCommandMinDeg(), pitchCommandMaxDeg());
}

int TurretControl::yawRawForDeg(float deg) const {
  const float raw = kYawLineB + (deg - config_.yawAxisOffsetDeg) * kYawLineM;
  return clampi(static_cast<int>(roundf(raw)), 0, 4095);
}

int TurretControl::pitchRawForDeg(float deg) const {
  const float raw = 2050.0f + ((deg - config_.pitchAxisOffsetDeg) * (3110.0f - 2050.0f) / 180.0f);
  return clampi(static_cast<int>(roundf(raw)), 0, 4095);
}

int TurretControl::yawSoftLowRaw() const {
  const int low = yawRawForDeg(config_.yawMinDeg);
  const int high = yawRawForDeg(config_.yawMaxDeg);
  return clampi(low < high ? low : high, kYawFeedbackRailLowCut, kYawFeedbackRailHighCut);
}

int TurretControl::yawSoftHighRaw() const {
  const int low = yawRawForDeg(config_.yawMinDeg);
  const int high = yawRawForDeg(config_.yawMaxDeg);
  return clampi(low > high ? low : high, kYawFeedbackRailLowCut, kYawFeedbackRailHighCut);
}

int TurretControl::pitchSoftLowRaw() const {
  const int low = pitchRawForDeg(config_.pitchMinDeg);
  const int high = pitchRawForDeg(config_.pitchMaxDeg);
  return clampi(low < high ? low : high, kPitchLowCut, kPitchHighCut);
}

int TurretControl::pitchSoftHighRaw() const {
  const int low = pitchRawForDeg(config_.pitchMinDeg);
  const int high = pitchRawForDeg(config_.pitchMaxDeg);
  return clampi(low > high ? low : high, kPitchLowCut, kPitchHighCut);
}

bool TurretControl::yawRawInsideSoftWindow(int raw) const {
  return raw >= yawSoftLowRaw() && raw <= yawSoftHighRaw();
}

bool TurretControl::pitchRawInsideSoftWindow(int raw) const {
  return raw >= pitchSoftLowRaw() && raw <= pitchSoftHighRaw();
}


bool TurretControl::applyTargetCm(float xCm,
                                  float yCm,
                                  float zCm,
                                  const char* source,
                                  bool allowDuringFire) {
  if (!config_.configured) {
    lastError_ = "target rejected: turret is unconfigured";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (fireState_ == "FIRING" && !allowDuringFire) {
    lastError_ = "target rejected during firing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }

  prepareForNewMotionCommand(source);

  lastTargetCmX_ = xCm;
  lastTargetCmY_ = yCm;
  lastTargetCmZ_ = zCm;
  lastTargetInputX_ = mqttTargetsInMeters(config_) ? xCm / 100.0f : xCm;
  lastTargetInputY_ = mqttTargetsInMeters(config_) ? yCm / 100.0f : yCm;
  lastTargetInputZ_ = mqttTargetsInMeters(config_) ? zCm / 100.0f : zCm;

  solvedYawDeg_ = computeYawDeg(lastTargetCmX_, lastTargetCmY_, config_.xCm, config_.yCm) + config_.yawOffsetDeg;
  pitchReachable_ = computePitchDeg(lastTargetCmX_, lastTargetCmY_, config_.xCm, config_.yCm,
                                    lastTargetCmZ_, config_.zCm, solvedPitchDeg_);
  solvedPitchDeg_ += config_.pitchOffsetDeg;
  clampedYawDeg_ = clampYawCommand(solvedYawDeg_);
  clampedPitchDeg_ = clampPitchCommand(solvedPitchDeg_);
  haveTarget_ = true;
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  aimReachedSinceMs_ = 0;
  patternState_ = "IDLE";
  lockedMotionAxis_ = 'N';
  resetPidState();
  const bool trackingAllowed = ensureMotionSafetyForTracking(source);
  bool pitchOnlyTrackingAllowed = false;
  if (trackingAllowed) {
    setTrackedTarget(clampedYawDeg_, clampedPitchDeg_);
    mode_ = "TARGET";
    lastError_ = "";
  } else if (ensurePitchSafetyForTracking(source)) {
    detachYawOutput("target_pitch_only_yaw_inhibited");
    setPitchOnlyTrackedTarget(clampedPitchDeg_);
    mode_ = "TARGET";
    pitchOnlyTrackingAllowed = true;
    lastError_ = "yaw tracking inhibited; pitch-only target tracking active";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  } else {
    stopMotionOutputs();
    yawTrackingSuppressed_ = false;
    yawTargetDeg_ = yawCurrentDeg_;
    pitchTargetDeg_ = pitchCurrentDeg_;
    yawGoalDeg_ = yawCurrentDeg_;
    pitchGoalDeg_ = pitchCurrentDeg_;
    targetSlewActive_ = false;
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  }

  Serial.println("========== FLEET TARGET UPDATE ==========");
  Serial.print("Source             : ");
  Serial.println(source);
  Serial.print("Frame ID           : ");
  Serial.println(config_.frameId);
  Serial.print("Target unit        : ");
  Serial.println(config_.mqttTargetUnit);
  Serial.print("Target input       : ");
  Serial.print(lastTargetInputX_, 3);
  Serial.print(", ");
  Serial.print(lastTargetInputY_, 3);
  Serial.print(", ");
  Serial.println(lastTargetInputZ_, 3);
  Serial.print("Target cm          : ");
  Serial.print(lastTargetCmX_, 3);
  Serial.print(", ");
  Serial.print(lastTargetCmY_, 3);
  Serial.print(", ");
  Serial.println(lastTargetCmZ_, 3);
  Serial.print("Computed Yaw [deg] : ");
  Serial.println(clampedYawDeg_, 3);
  Serial.print("Computed Pitch[deg]: ");
  if (pitchReachable_) {
    Serial.println(clampedPitchDeg_, 3);
  } else {
    Serial.println("INVALID (unreachable or too close; applied clamp)");
  }
  Serial.print("Yaw offset [deg]   : ");
  Serial.println(config_.yawOffsetDeg, 3);
  Serial.print("Pitch offset[deg]  : ");
  Serial.println(config_.pitchOffsetDeg, 3);
  Serial.print("Motion tracking    : ");
  Serial.println(trackingAllowed ? "ENABLED" : (pitchOnlyTrackingAllowed ? "PITCH_ONLY_YAW_INHIBITED" : "INHIBITED"));
  Serial.println("Auto fire on target: DISABLED");
  Serial.println("=========================================");
  return true;
}

bool TurretControl::applyTargetObject(JsonObjectConst target, const char* source) {
  if (!config_.configured) {
    lastError_ = "target rejected: turret is unconfigured";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (fireState_ == "FIRING") {
    lastError_ = "target rejected during firing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (target.isNull()) {
    lastError_ = "target object missing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (!target.containsKey("x") || !target.containsKey("y")) {
    lastError_ = "target x/y missing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (!validateFrame(target["frame_id"], "target", source)) return false;

  const float inputX = target["x"].as<float>();
  const float inputY = target["y"].as<float>();
  const float inputZ = target.containsKey("z") ? target["z"].as<float>() :
                                                (mqttTargetsInMeters(config_) ? config_.defaultTargetZCm / 100.0f : config_.defaultTargetZCm);
  return applyTargetCm(targetUnitToCm(inputX), targetUnitToCm(inputY), targetUnitToCm(inputZ), source);
}

bool TurretControl::applyDirectAimCommand(JsonDocument& doc, const char* source) {
  if (!config_.configured) {
    lastError_ = "aim rejected: turret is unconfigured";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (fireState_ == "FIRING") {
    lastError_ = "aim rejected during firing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (!validateFrame(doc["frame_id"], "aim", source)) return false;
  if (!doc["yaw_deg"].is<float>() || !doc["pitch_deg"].is<float>()) {
    lastError_ = "aim yaw_deg/pitch_deg missing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }

  prepareForNewMotionCommand(source);

  solvedYawDeg_ = doc["yaw_deg"].as<float>();
  solvedPitchDeg_ = doc["pitch_deg"].as<float>();
  pitchReachable_ = true;
  clampedYawDeg_ = clampYawCommand(solvedYawDeg_);
  clampedPitchDeg_ = clampPitchCommand(solvedPitchDeg_);
  haveTarget_ = false;
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  aimReachedSinceMs_ = 0;
  patternState_ = "IDLE";
  resetPidState();
  const bool trackingAllowed = ensureMotionSafetyForTracking(source);
  bool pitchOnlyTrackingAllowed = false;
  if (trackingAllowed) {
    setTrackedTarget(clampedYawDeg_, clampedPitchDeg_);
    mode_ = "TARGET";
    lastError_ = "";
  } else if (ensurePitchSafetyForTracking(source)) {
    detachYawOutput("aim_pitch_only_yaw_inhibited");
    setPitchOnlyTrackedTarget(clampedPitchDeg_);
    mode_ = "TARGET";
    pitchOnlyTrackingAllowed = true;
    lastError_ = "yaw tracking inhibited; pitch-only direct aim active";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  } else {
    stopMotionOutputs();
    yawTrackingSuppressed_ = false;
    yawTargetDeg_ = yawCurrentDeg_;
    pitchTargetDeg_ = pitchCurrentDeg_;
    yawGoalDeg_ = yawCurrentDeg_;
    pitchGoalDeg_ = pitchCurrentDeg_;
    targetSlewActive_ = false;
  }

  Serial.println("========== FLEET DIRECT AIM ==========");
  Serial.print("Source             : ");
  Serial.println(source);
  Serial.print("Frame ID           : ");
  Serial.println(config_.frameId);
  Serial.print("Requested Yaw [deg]: ");
  Serial.println(solvedYawDeg_, 3);
  Serial.print("Requested Pitch[deg]: ");
  Serial.println(solvedPitchDeg_, 3);
  Serial.print("Applied Yaw [deg]  : ");
  Serial.println(clampedYawDeg_, 3);
  Serial.print("Applied Pitch[deg] : ");
  Serial.println(clampedPitchDeg_, 3);
  Serial.print("Motion tracking    : ");
  Serial.println(trackingAllowed ? "ENABLED" : (pitchOnlyTrackingAllowed ? "PITCH_ONLY_YAW_INHIBITED" : "INHIBITED"));
  Serial.println("Direct aim is local turret yaw/pitch; no target coordinate solve");
  Serial.println("=======================================");
  return true;
}

bool TurretControl::applyHomeCommand(const char* source) {
  if (!config_.configured) {
    lastError_ = "home rejected: turret is unconfigured";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }
  if (fireState_ == "FIRING") {
    lastError_ = "home rejected during firing";
    Serial.print("[fleet][control] ");
    Serial.println(lastError_);
    return false;
  }

  forceFireOutputsSafeOff();
  prepareForNewMotionCommand(source);

  solvedYawDeg_ = config_.homeYawDeg;
  solvedPitchDeg_ = config_.homePitchDeg;
  pitchReachable_ = true;
  clampedYawDeg_ = clampYawCommand(solvedYawDeg_);
  clampedPitchDeg_ = clampPitchCommand(solvedPitchDeg_);
  haveTarget_ = false;
  pendingFire_ = false;
  pendingFireHoldMs_ = 0;
  aimReachedSinceMs_ = 0;
  patternState_ = "IDLE";
  lockedMotionAxis_ = 'N';
  resetPidState();

  const bool trackingAllowed = ensureMotionSafetyForTracking(source);
  bool pitchOnlyTrackingAllowed = false;
  if (trackingAllowed) {
    setTrackedTarget(clampedYawDeg_, clampedPitchDeg_);
    mode_ = "HOME";
    lastError_ = "";
  } else if (ensurePitchSafetyForTracking(source)) {
    detachYawOutput("home_pitch_only_yaw_inhibited");
    setPitchOnlyTrackedTarget(clampedPitchDeg_);
    mode_ = "HOME";
    pitchOnlyTrackingAllowed = true;
    lastError_ = "yaw tracking inhibited; pitch-only home active";
    Serial.print("[fleet][motion] ");
    Serial.println(lastError_);
  } else {
    stopMotionOutputs();
    yawTrackingSuppressed_ = false;
    yawTargetDeg_ = yawCurrentDeg_;
    pitchTargetDeg_ = pitchCurrentDeg_;
    yawGoalDeg_ = yawCurrentDeg_;
    pitchGoalDeg_ = pitchCurrentDeg_;
    targetSlewActive_ = false;
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  }

  Serial.println("========== FLEET HOME COMMAND ==========");
  Serial.print("Source             : ");
  Serial.println(source);
  Serial.print("Home Yaw [deg]     : ");
  Serial.println(clampedYawDeg_, 3);
  Serial.print("Home Pitch[deg]    : ");
  Serial.println(clampedPitchDeg_, 3);
  Serial.print("Motion tracking    : ");
  Serial.println(trackingAllowed ? "ENABLED" : (pitchOnlyTrackingAllowed ? "PITCH_ONLY_YAW_INHIBITED" : "INHIBITED"));
  Serial.println("Home/init is local turret yaw/pitch; no target coordinate solve");
  Serial.println("========================================");
  return true;
}

bool TurretControl::applyJogCommand(JsonDocument& doc, const char* source) {
  if (!config_.configured) {
    lastError_ = "jog rejected: turret is unconfigured";
    Serial.print("[fleet][jog] ");
    Serial.println(lastError_);
    return false;
  }
  if (fireState_ == "FIRING") {
    lastError_ = "jog rejected during firing";
    Serial.print("[fleet][jog] ");
    Serial.println(lastError_);
    return false;
  }

  const char* axis = doc["axis"] | "yaw";
  const char* direction = doc["direction"] | "";
  const bool isYaw = strcmp(axis, "yaw") == 0;
  const bool isPitch = strcmp(axis, "pitch") == 0;
  if (!isYaw && !isPitch) {
    lastError_ = String("jog rejected: unsupported axis=") + axis;
    Serial.print("[fleet][jog] ");
    Serial.println(lastError_);
    return false;
  }

  bool positive = true;
  if (strcmp(direction, "-") == 0 ||
      strcmp(direction, "minus") == 0 ||
      strcmp(direction, "negative") == 0 ||
      strcmp(direction, "ccw") == 0 ||
      strcmp(direction, "counterclockwise") == 0) {
    positive = false;
  } else if (strcmp(direction, "+") == 0 ||
             strcmp(direction, "plus") == 0 ||
             strcmp(direction, "positive") == 0 ||
             strcmp(direction, "cw") == 0 ||
             strcmp(direction, "clockwise") == 0) {
    positive = true;
  } else {
    lastError_ = "jog rejected: direction must be plus/minus/cw/ccw";
    Serial.print("[fleet][jog] ");
    Serial.println(lastError_);
    return false;
  }

  const int requestedDeltaUs = doc["delta_us"] | 20;
  const unsigned long requestedDurationMs = doc["duration_ms"] | 120;
  const int axisMaxDelta = isYaw ? config_.yawMaxDeltaUs : config_.pitchMaxDeltaUs;
  const int jogMaxDeltaUs = kUnsafeManualCalibrationMode ? 400 : axisMaxDelta;
  const int jogMinDeltaUs = kUnsafeManualCalibrationMode ? 1 : 20;
  const unsigned long jogMaxDurationMs = kUnsafeManualCalibrationMode ? 1200UL : 100UL;
  const unsigned long jogMinDurationMs = kUnsafeManualCalibrationMode ? 10UL : 20UL;
  const int safeDeltaUs = clampi(requestedDeltaUs, jogMinDeltaUs, jogMaxDeltaUs);
  const unsigned long safeDurationMs =
      clampi(static_cast<int>(requestedDurationMs),
             static_cast<int>(jogMinDurationMs),
             static_cast<int>(jogMaxDurationMs));
  const int stopUs = isYaw ? config_.yawStopUs : config_.pitchStopUs;
  const int commandUs = clampi(stopUs + (positive ? safeDeltaUs : -safeDeltaUs), 1000, 2000);

  if (isYaw) {
    detachPitchOutput("debug_jog_axis_select");
  } else {
    detachYawOutput("debug_jog_axis_select");
  }
  forceFireOutputsSafeOff();
  delay(config_.axisSwitchCooldownMs);
  updateCurrentAngles();
  const int rawBefore = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  const float degBefore = isYaw ? yawCurrentDeg_ : pitchCurrentDeg_;

  if (isYaw && (rawBefore <= kYawFeedbackRailLowCut || rawBefore >= kYawFeedbackRailHighCut)) {
    Serial.print("[fleet][jog] warning: yaw feedback near ADC rail raw=");
    Serial.print(rawBefore);
    Serial.print(" rail_guard=");
    Serial.print(kYawFeedbackRailLowCut);
    Serial.print("..");
    Serial.print(kYawFeedbackRailHighCut);
    Serial.println("; bounded debug jog still allowed");
  }
  if (isPitch && (rawBefore <= kPitchLowCut || rawBefore >= kPitchHighCut)) {
    Serial.print("[fleet][jog] warning: pitch feedback outside widened ADC range raw=");
    Serial.print(rawBefore);
    Serial.print(" range=");
    Serial.print(kPitchLowCut);
    Serial.print("..");
    Serial.print(kPitchHighCut);
    Serial.println("; bounded debug jog still allowed");
  }
  const int axisSoftLow = isYaw ? yawSoftLowRaw() : pitchSoftLowRaw();
  const int axisSoftHigh = isYaw ? yawSoftHighRaw() : pitchSoftHighRaw();
  if ((isYaw && (rawBefore < axisSoftLow || rawBefore > axisSoftHigh)) ||
      (isPitch && (rawBefore < axisSoftLow || rawBefore > axisSoftHigh))) {
    Serial.print("[fleet][jog] warning: ");
    Serial.print(isYaw ? "yaw" : "pitch");
    Serial.print(" outside nominal soft window raw=");
    Serial.print(rawBefore);
    Serial.print(" soft=");
    Serial.print(axisSoftLow);
    Serial.print("..");
    Serial.print(axisSoftHigh);
    Serial.println("; using caller-provided +/- direction for calibration");
  }

  if (isYaw) {
    ensureYawAttached("debug_jog");
    delay(kJogAttachSettleMs);
    yawServo_.writeMicroseconds(commandUs);
    yawLastCommandUs_ = commandUs;
  } else {
    ensurePitchAttached("debug_jog");
    delay(kJogAttachSettleMs);
    pitchServo_.writeMicroseconds(commandUs);
    pitchLastCommandUs_ = commandUs;
  }
  delay(safeDurationMs);
  if (isYaw) {
    yawServo_.writeMicroseconds(config_.yawStopUs);
    yawLastCommandUs_ = config_.yawStopUs;
  } else {
    pitchServo_.writeMicroseconds(config_.pitchStopUs);
    pitchLastCommandUs_ = config_.pitchStopUs;
  }
  delay(kJogStopMs);
  const bool detachAfter = doc["detach_after"] | false;
  if (detachAfter) {
    if (isYaw) {
      detachYawOutput("debug_jog_done");
    } else {
      detachPitchOutput("debug_jog_done");
    }
  } else {
    Serial.print("[fleet][jog] ");
    Serial.print(isYaw ? "yaw" : "pitch");
    Serial.println(" servo left attached at stop PWM for debug hold");
  }

  updateCurrentAngles();
  const int rawAfter = isYaw ? yawRawCurrent_ : pitchRawCurrent_;
  const float degAfter = isYaw ? yawCurrentDeg_ : pitchCurrentDeg_;
  const int rawDelta = rawAfter - rawBefore;
  int rawDeltaWrap = rawDelta;
  bool wrapPossible = false;
  if (isYaw && rawDeltaWrap > 2048) {
    rawDeltaWrap -= 4096;
    wrapPossible = true;
  } else if (isYaw && rawDeltaWrap < -2048) {
    rawDeltaWrap += 4096;
    wrapPossible = true;
  }

  haveTarget_ = false;
  yawTrackingSuppressed_ = false;
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  patternState_ = "IDLE";
  yawTargetDeg_ = yawCurrentDeg_;
  pitchTargetDeg_ = pitchCurrentDeg_;
  yawGoalDeg_ = yawCurrentDeg_;
  pitchGoalDeg_ = pitchCurrentDeg_;
  targetSlewActive_ = false;
  lockedMotionAxis_ = 'N';
  selectedMotionAxis_ = 'N';
  resetPidState();
  lastError_ = "";

  Serial.print("[fleet][jog] source=");
  Serial.print(source);
  Serial.print(" axis=");
  Serial.print(isYaw ? "yaw" : "pitch");
  Serial.print(" direction=");
  Serial.print(positive ? "plus/cw" : "minus/ccw");
  Serial.print(" pulse_us=");
  Serial.print(commandUs);
  Serial.print(" duration_ms=");
  Serial.print(safeDurationMs);
  Serial.print(" raw_before=");
  Serial.print(rawBefore);
  Serial.print(" raw_after=");
  Serial.print(rawAfter);
  Serial.print(" delta_raw=");
  Serial.print(rawDelta);
  Serial.print(" delta_raw_wrap=");
  Serial.print(rawDeltaWrap);
  Serial.print(" deg_before=");
  Serial.print(degBefore, 2);
  Serial.print(" deg_after=");
  Serial.print(degAfter, 2);
  Serial.print(" delta_deg=");
  Serial.print(degAfter - degBefore, 2);
  Serial.print(" wrap_possible=");
  Serial.println(wrapPossible ? "Y" : "N");
  return true;
}

void TurretControl::clearPatternState(const char* reason) {
  patternPlan_.clear();
  patternStepIndex_ = 0;
  patternStepStarted_ = false;
  patternStepStartedMs_ = 0;
  patternLoopIndex_ = 0;
  patternCurrentStepType_ = PATTERN_STEP_NONE;
  patternState_ = "IDLE";
  Serial.print("[fleet][pattern] cleared reason=");
  Serial.println(reason);
}

bool TurretControl::hasActivePattern() const {
  return patternPlan_.kind != PATTERN_NONE ||
         mode_ == "PATTERN" ||
         patternState_ != "IDLE";
}

void TurretControl::preemptActivePattern(const char* command, const char* source, bool forceFireSafeOff) {
  if (!hasActivePattern() && !isFireSequenceActive() && fireState_ != "FIRING") return;

  Serial.print("[fleet][pattern] preempt command=");
  Serial.print(command);
  Serial.print(" source=");
  Serial.println(source);
  stopMotionOutputs();
  if (forceFireSafeOff) {
    forceFireOutputsSafeOff();
  }
  clearPatternState(command);
  targetSlewActive_ = false;
  yawTrackingSuppressed_ = false;
  lockedMotionAxis_ = 'N';
  selectedMotionAxis_ = 'N';
  resetPidState();
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  patternState_ = "IDLE";
  lastPatternError_ = String("pattern preempted by ") + command;
}

bool TurretControl::validatePatternPointEnvelope(const PatternPoint& point,
                                                 uint8_t pointIndex,
                                                 const char* patternId) {
  const float solvedYaw = computeYawDeg(point.xCm, point.yCm, config_.xCm, config_.yCm) +
                          config_.yawOffsetDeg;
  float solvedPitch = 0.0f;
  const bool pitchReachable = computePitchDeg(point.xCm, point.yCm, config_.xCm, config_.yCm,
                                              point.zCm, config_.zCm, solvedPitch);
  solvedPitch += config_.pitchOffsetDeg;

  const float yawMin = yawCommandMinDeg() + kPatternCommandMarginDeg;
  const float yawMax = yawCommandMaxDeg() - kPatternCommandMarginDeg;
  const float pitchMin = pitchCommandMinDeg() + kPatternCommandMarginDeg;
  const float pitchMax = pitchCommandMaxDeg() - kPatternCommandMarginDeg;

  if (!pitchReachable || !isfinite(solvedYaw) || !isfinite(solvedPitch) ||
      solvedYaw < yawMin || solvedYaw > yawMax ||
      solvedPitch < pitchMin || solvedPitch > pitchMax) {
    lastPatternError_ = String("pattern rejected: ") + patternId +
                        " point " + String(pointIndex) +
                        " outside safe command envelope margin yaw=" +
                        String(solvedYaw, 2) + " pitch=" + String(solvedPitch, 2) +
                        " allowed_yaw=" + String(yawMin, 2) + ".." + String(yawMax, 2) +
                        " allowed_pitch=" + String(pitchMin, 2) + ".." + String(pitchMax, 2);
    lastError_ = lastPatternError_;
    return false;
  }
  return true;
}

bool TurretControl::validatePatternPlanEnvelope(const PatternPlan& plan, const char* patternId) {
  for (uint8_t index = 0; index < plan.pointCount; ++index) {
    if (!validatePatternPointEnvelope(plan.points[index], index, patternId)) return false;
  }
  return true;
}

bool TurretControl::applyPatternPoint(uint8_t pointIndex, const char* source, bool allowDuringFire) {
  if (pointIndex >= patternPlan_.pointCount) {
    lastPatternError_ = "pattern aborted: point index out of range";
    lastError_ = lastPatternError_;
    return false;
  }

  const PatternPoint& point = patternPlan_.points[pointIndex];
  const bool ok = applyTargetCm(point.xCm, point.yCm, point.zCm, source, allowDuringFire);
  if (!ok || motionSafetyInhibited_) return false;
  if (yawTrackingSuppressed_) {
    lastPatternError_ = "pattern rejected: full yaw/pitch tracking required";
    lastError_ = lastPatternError_;
    return false;
  }

  mode_ = "PATTERN";
  patternState_ = "MOVING";
  patternCurrentStepType_ = PATTERN_STEP_MOVE;
  return true;
}

void TurretControl::beginPatternStep(unsigned long now) {
  if (patternStepIndex_ >= patternPlan_.stepCount) {
    completePattern("steps_complete");
    return;
  }

  PatternStep& step = patternPlan_.steps[patternStepIndex_];
  patternStepStarted_ = true;
  patternStepStartedMs_ = now;
  patternCurrentStepType_ = step.type;

  Serial.print("[fleet][pattern] step=");
  Serial.print(patternStepIndex_);
  Serial.print("/");
  Serial.print(patternPlan_.stepCount);
  Serial.print(" type=");
  Serial.println(patternStepTypeName(step.type));

  switch (step.type) {
    case PATTERN_STEP_WAIT:
      patternState_ = "DWELL";
      mode_ = "PATTERN";
      break;
    case PATTERN_STEP_MOVE:
      if (!applyPatternPoint(step.pointIndex, "PATTERN(move)")) {
        abortPattern(lastError_.length() > 0 ? lastError_.c_str() : "pattern move rejected");
      }
      break;
    case PATTERN_STEP_DWELL:
      patternState_ = "DWELL";
      mode_ = "PATTERN";
      stopMotionOutputs();
      break;
    case PATTERN_STEP_FIRE:
      if (patternPlan_.noFire) {
        advancePatternStep();
        return;
      }
      if (mode_ == "DEAD" || brownoutLockoutActive_ || !config_.configured) {
        abortPattern("pattern fire rejected: unsafe state");
        return;
      }
      patternState_ = "FIRING";
      mode_ = "PATTERN";
      postFireMode_ = "PATTERN";
      startFireSequence(patternPlan_.fireMs, "PATTERN(fire)");
      break;
    case PATTERN_STEP_FIRE_MOVE:
      if (patternPlan_.noFire) {
        advancePatternStep();
        return;
      }
      if (!applyPatternPoint(step.pointIndex, "PATTERN(sweep_fire_move)", true)) {
        abortPattern(lastError_.length() > 0 ? lastError_.c_str() : "pattern sweep-fire move rejected");
        return;
      }
      if (mode_ == "DEAD" || brownoutLockoutActive_ || !config_.configured) {
        abortPattern("pattern sweep-fire rejected: unsafe state");
        return;
      }
      patternCurrentStepType_ = PATTERN_STEP_FIRE_MOVE;
      patternState_ = "FIRE_MOVE";
      mode_ = "PATTERN";
      postFireMode_ = "PATTERN";
      ensurePatternSweepFire(patternPlan_.fireMs);
      patternCurrentStepType_ = PATTERN_STEP_FIRE_MOVE;
      patternState_ = "FIRE_MOVE";
      break;
    case PATTERN_STEP_WAIT_FIRE_SAFE:
      patternState_ = "WAIT_FIRE_SAFE";
      mode_ = isFireSequenceActive() ? mode_ : String("PATTERN");
      break;
    case PATTERN_STEP_DONE:
    case PATTERN_STEP_NONE:
      completePattern("done_step");
      break;
  }
}

void TurretControl::advancePatternStep() {
  patternStepIndex_++;
  patternStepStarted_ = false;
  patternStepStartedMs_ = 0;
  if (patternPlan_.stepCount > 0) {
    patternLoopIndex_ = static_cast<uint8_t>((patternStepIndex_ * patternPlan_.loopCount) / patternPlan_.stepCount);
    if (patternLoopIndex_ >= patternPlan_.loopCount) patternLoopIndex_ = patternPlan_.loopCount - 1;
  }
}

void TurretControl::completePattern(const char* reason) {
  const String returnTo = patternPlan_.returnTo;
  lastPatternError_ = "";
  clearPatternState(reason);
  stopMotionOutputs();
  if (returnTo == "idle") {
    if (config_.configured) {
      updateCurrentAngles();
      haveTarget_ = false;
      yawTrackingSuppressed_ = false;
      yawTargetDeg_ = clampYawCommand(clampf(yawCurrentDeg_, config_.idleYawMinDeg, config_.idleYawMaxDeg));
      pitchTargetDeg_ = clampPitchCommand(clampf(pitchCurrentDeg_, config_.idlePitchMinDeg, config_.idlePitchMaxDeg));
      yawGoalDeg_ = yawTargetDeg_;
      pitchGoalDeg_ = pitchTargetDeg_;
      targetSlewActive_ = false;
      idleSweepForward_ = yawTargetDeg_ <= config_.idleYawMinDeg;
      idlePitchUp_ = pitchTargetDeg_ <= config_.idlePitchMinDeg;
      lockedMotionAxis_ = 'N';
      selectedMotionAxis_ = 'N';
      resetPidState();
      mode_ = "IDLE";
    } else {
      mode_ = "UNCONFIGURED";
    }
  } else {
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  }
  patternCurrentStepType_ = PATTERN_STEP_DONE;
  Serial.print("[fleet][pattern] complete return_to=");
  Serial.println(returnTo);
}

void TurretControl::abortPattern(const char* reason) {
  lastPatternError_ = reason;
  lastError_ = reason;
  clearPatternState("abort");
  stopMotionOutputs();
  forceFireOutputsSafeOff();
  mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
  patternCurrentStepType_ = PATTERN_STEP_DONE;
  Serial.print("[fleet][pattern] aborted: ");
  Serial.println(reason);
}

void TurretControl::updatePattern() {
  if (patternPlan_.kind == PATTERN_NONE) return;
  const unsigned long now = millis();

  if (mode_ == "DEAD" || brownoutLockoutActive_) {
    abortPattern("pattern aborted: unsafe state");
    return;
  }

  if (patternStepIndex_ >= patternPlan_.stepCount) {
    completePattern("steps_complete");
    return;
  }

  if (!patternStepStarted_) {
    beginPatternStep(now);
    return;
  }

  PatternStep& step = patternPlan_.steps[patternStepIndex_];
  switch (step.type) {
    case PATTERN_STEP_WAIT:
    case PATTERN_STEP_DWELL:
      if (now - patternStepStartedMs_ >= step.durationMs) {
        advancePatternStep();
      }
      break;
    case PATTERN_STEP_MOVE:
      if (aimReached()) {
        advancePatternStep();
      } else if (now - patternStepStartedMs_ >= step.durationMs * 2UL) {
        // Motion uses a one-axis-at-a-time scheduler to avoid brownout on this
        // fleet hardware, so point-shot moves get one configured timeout per
        // axis before failing. Sweep-fire legs are handled separately as
        // visible time-boxed sweeps.
        abortPattern("pattern move timeout before aim reached");
      }
      break;
    case PATTERN_STEP_FIRE_MOVE:
      patternState_ = "FIRE_MOVE";
      if (patternSweepYawReached()) {
        stopPatternSweepFireAtEndpoint("endpoint");
        advancePatternStep();
      } else if (now - patternStepStartedMs_ >= step.durationMs) {
        // A sweep-fire leg is deliberately time-boxed: the visible behavior is
        // "fire while sweeping toward the opposite lane edge." Unlike a
        // point-shot MOVE, reaching the exact endpoint is not required before
        // advancing, because the safety envelope already clamps the commanded
        // target and the fire sequence must be allowed to shut down cleanly.
        stopPatternSweepFireAtEndpoint("timeout");
        advancePatternStep();
      }
      break;
    case PATTERN_STEP_FIRE:
      advancePatternStep();
      break;
    case PATTERN_STEP_WAIT_FIRE_SAFE:
      patternState_ = "WAIT_FIRE_SAFE";
      if (!isFireSequenceActive() && fireState_ != "FIRING") {
        mode_ = "PATTERN";
        advancePatternStep();
      }
      break;
    case PATTERN_STEP_DONE:
    case PATTERN_STEP_NONE:
      completePattern("done");
      break;
  }
}

void TurretControl::startFireFromCommand(JsonDocument& doc, const char* source) {
  if (brownoutLockoutActive_) {
    forceFireOutputsSafeOff();
    pendingFire_ = false;
    pendingFireHoldMs_ = 0;
    lastError_ = String("fire rejected after brownout lockout from ") + source +
                 "; send recover after manually confirming pose/power";
    Serial.print("[fleet][safety] ");
    Serial.println(lastError_);
    return;
  }
  if (mode_ == "DEAD") {
    lastError_ = "fire rejected in DEAD mode";
    Serial.print("[fleet][fire] ");
    Serial.print(lastError_);
    Serial.print(" from ");
    Serial.println(source);
    return;
  }
  if (!config_.configured) {
    lastError_ = "fire rejected: turret is unconfigured";
    Serial.print("[fleet][fire] ");
    Serial.println(lastError_);
    return;
  }
  if (patternPlan_.kind != PATTERN_NONE) {
    lastError_ = "fire rejected during active pattern";
    Serial.print("[fleet][fire] ");
    Serial.println(lastError_);
    return;
  }
  updateCurrentAngles();
  if (!motionInsideSoftWindow()) {
    forceFireOutputsSafeOff();
    pendingFire_ = false;
    pendingFireHoldMs_ = 0;
    motionSafetyInhibited_ = true;
    lastError_ = String("fire rejected: pose outside calibrated safe envelope yaw_raw=") +
                 String(yawRawCurrent_) + " pitch_raw=" + String(pitchRawCurrent_);
    Serial.print("[fleet][safety] ");
    Serial.println(lastError_);
    return;
  }
  if (isFireSequenceActive()) {
    const unsigned long holdMs = normalizeFireHoldMs(doc, config_);
    fireRequestedHoldMs_ = holdMs;
    if (fireSequenceState_ == FIRE_SEQUENCE_RUNNING && fireKeepAliveUntilMs_ != 0) {
      const unsigned long now = millis();
      fireKeepAliveUntilMs_ = now + holdMs;
      fireHardOffAtMs_ = now + fireHardOffDelayMs(config_, holdMs);
    }
    if (fireSequenceState_ == FIRE_SEQUENCE_ESC_OFF_WAIT ||
        fireSequenceState_ == FIRE_SEQUENCE_CH3_OFF_WAIT ||
        fireSequenceState_ == FIRE_SEQUENCE_CH1_OFF_WAIT ||
        fireSequenceState_ == FIRE_SEQUENCE_CH2_OFF_WAIT) {
      fireRestartRequested_ = true;
    }
    Serial.print("[fleet][fire] keepalive refreshed hold_ms=");
    Serial.println(holdMs);
    return;
  }

  const unsigned long holdMs = normalizeFireHoldMs(doc, config_);
  postFireMode_ = (mode_ == "TARGET" || mode_ == "PATTERN" || mode_ == "HOME")
                    ? mode_
                    : String("WAIT_COMMAND");
  lastError_ = "";
  Serial.print("[fleet][fire] explicit fire command from ");
  Serial.print(source);
  Serial.print(" hold_ms=");
  Serial.println(holdMs);
  startFireSequence(holdMs, source);
}

void TurretControl::handlePatternCommand(JsonDocument& doc, const char* source) {
  if (!config_.configured) {
    lastError_ = "pattern rejected: turret is unconfigured";
    Serial.print("[fleet][pattern] ");
    Serial.println(lastError_);
    return;
  }
  if (!validateFrame(doc["frame_id"], "pattern", source)) return;
  if (mode_ == "DEAD" || brownoutLockoutActive_) {
    lastError_ = "pattern rejected: unsafe current state";
    Serial.print("[fleet][pattern] ");
    Serial.println(lastError_);
    return;
  }

  const char* patternId = doc["pattern_id"] | "";
  if (patternId[0] == '\0') {
    lastError_ = "pattern_id missing";
    Serial.print("[fleet][pattern] ");
    Serial.println(lastError_);
    return;
  }

  const PatternKind nextKind = patternKindFromId(patternId);
  if (nextKind == PATTERN_NONE) {
    lastError_ = String("pattern rejected: unsupported pattern_id=") + patternId;
    lastPatternError_ = lastError_;
    Serial.print("[fleet][pattern] ");
    Serial.println(lastError_);
    return;
  }

  if (hasActivePattern() || fireState_ == "FIRING" || isFireSequenceActive()) {
    preemptActivePattern("pattern", source, true);
  }

  JsonObjectConst params = doc["params"].as<JsonObjectConst>();
  PatternPlan nextPlan;
  const uint32_t patternSelectionSeed =
    static_cast<uint32_t>(micros()) ^
    (static_cast<uint32_t>(analogRead(kYawPotPin)) << 16) ^
    static_cast<uint32_t>(analogRead(kPitchPotPin)) ^
    static_cast<uint32_t>(random(0x7fffffff));
  if (!compilePatternPlan(params, nextKind, config_, nextPlan, patternSelectionSeed)) {
    lastPatternError_ = nextPlan.error.length() > 0 ? nextPlan.error : String("pattern rejected: no executable steps");
    lastError_ = lastPatternError_;
    Serial.print("[fleet][pattern] ");
    Serial.println(lastError_);
    return;
  }
  if (!validatePatternPlanEnvelope(nextPlan, patternId)) {
    Serial.print("[fleet][pattern] ");
    Serial.println(lastError_);
    return;
  }

  if (!ensureMotionSafetyForTracking(source)) return;

  clearPatternState("new_pattern");
  activePatternId_ = patternId;
  activePatternInstanceId_ = doc["pattern_instance_id"] | "";
  patternPlan_ = nextPlan;
  patternState_ = "ACTIVE";
  patternCurrentStepType_ = PATTERN_STEP_NONE;
  mode_ = "PATTERN";
  lastError_ = "";
  lastPatternError_ = "";

  Serial.print("[fleet][pattern] accepted pattern_id=");
  Serial.print(activePatternId_);
  Serial.print(" instance=");
  Serial.print(activePatternInstanceId_);
  Serial.print(" frame_id=");
  Serial.print(config_.frameId);
  Serial.print(" steps=");
  Serial.print(patternPlan_.stepCount);
  Serial.print(" loop=");
  Serial.print(patternPlan_.loopCount);
  Serial.print(" dwell_ms=");
  Serial.print(patternPlan_.dwellMs);
  Serial.print(" fire_ms=");
  Serial.println(patternPlan_.fireMs);

  if (activePatternId_ == "calibration_no_fire") {
    Serial.println("[fleet][pattern] calibration_no_fire: relay/ESC fire disabled by contract");
  }
}

void TurretControl::handleCommandJson(JsonDocument& doc, const char* source) {
  const char* command = doc["command"] | "";
  lastCommandId_ = doc["command_id"] | "";

  if (strcmp(command, "recover") == 0 || strcmp(command, "clear_brownout_lockout") == 0) {
    clearBrownoutLockoutIfSafe(source);
    return;
  }

  if (commandBlockedByBrownoutLockout(command, source)) return;

  if (strcmp(command, "idle") == 0) {
    preemptActivePattern("idle", source, true);
    clearPatternState("idle");
    if (!config_.configured) {
      mode_ = "UNCONFIGURED";
      lastError_ = "idle rejected: turret is unconfigured";
    } else {
      haveTarget_ = false;
      prepareForNewMotionCommand(source);
      const bool fullMotionAllowed = ensureMotionSafetyForTracking(source);
      bool pitchOnlyAllowed = false;
      if (!fullMotionAllowed) {
        pitchOnlyAllowed = ensurePitchSafetyForTracking(source);
        if (!pitchOnlyAllowed) return;
        detachYawOutput("idle_pitch_only_yaw_inhibited");
      }
      yawTrackingSuppressed_ = pitchOnlyAllowed;
      yawTargetDeg_ = pitchOnlyAllowed ? yawCurrentDeg_ :
                      clampYawCommand(clampf(yawCurrentDeg_, config_.idleYawMinDeg, config_.idleYawMaxDeg));
      pitchTargetDeg_ = clampPitchCommand(clampf(pitchCurrentDeg_, config_.idlePitchMinDeg, config_.idlePitchMaxDeg));
      yawGoalDeg_ = yawTargetDeg_;
      pitchGoalDeg_ = pitchTargetDeg_;
      targetSlewActive_ = false;
      idleSweepForward_ = yawTargetDeg_ <= config_.idleYawMinDeg;
      idlePitchUp_ = pitchTargetDeg_ <= config_.idlePitchMinDeg;
      lockedMotionAxis_ = 'N';
      resetPidState();
      mode_ = "IDLE";
      patternState_ = "IDLE";
      lastError_ = pitchOnlyAllowed ? "yaw tracking inhibited; pitch-only idle active" : "";
      if (pitchOnlyAllowed) {
        Serial.print("[fleet][motion] ");
        Serial.println(lastError_);
      }
    }
  } else if (strcmp(command, "dead") == 0) {
    preemptActivePattern("dead", source, true);
    clearPatternState("dead");
    haveTarget_ = false;
    prepareForNewMotionCommand(source);
    forceFireOutputsSafeOff();
    const bool fullMotionAllowed = ensureMotionSafetyForTracking(source);
    bool pitchOnlyAllowed = false;
    if (!fullMotionAllowed) {
      pitchOnlyAllowed = ensurePitchSafetyForTracking(source);
      if (!pitchOnlyAllowed) return;
      detachYawOutput("dead_pitch_only_yaw_inhibited");
    }
    yawTrackingSuppressed_ = pitchOnlyAllowed;
    deadYawHoldDeg_ = yawCurrentDeg_;
    yawTargetDeg_ = yawCurrentDeg_;
    pitchTargetDeg_ = pitchCurrentDeg_;
    yawGoalDeg_ = clampYawCommand(deadYawHoldDeg_);
    pitchGoalDeg_ = clampPitchCommand(config_.deadPitchDeg);
    targetSlewActive_ = true;
    lockedMotionAxis_ = 'N';
    resetPidState();
    mode_ = "DEAD";
    patternState_ = "IDLE";
    lastError_ = pitchOnlyAllowed ? "yaw tracking inhibited; pitch-only dead active" : "";
    if (pitchOnlyAllowed) {
      Serial.print("[fleet][motion] ");
      Serial.println(lastError_);
    }
  } else if (strcmp(command, "hold") == 0 || strcmp(command, "wait") == 0) {
    preemptActivePattern("hold", source, true);
    clearPatternState("hold");
    haveTarget_ = false;
    stopMotionOutputs();
    forceFireOutputsSafeOff();
    updateCurrentAngles();
    yawTargetDeg_ = yawCurrentDeg_;
    pitchTargetDeg_ = pitchCurrentDeg_;
    yawGoalDeg_ = yawCurrentDeg_;
    pitchGoalDeg_ = pitchCurrentDeg_;
    targetSlewActive_ = false;
    yawTrackingSuppressed_ = false;
    lockedMotionAxis_ = 'N';
    resetPidState();
    mode_ = config_.configured ? "WAIT_COMMAND" : "UNCONFIGURED";
    patternState_ = "IDLE";
    if (!motionInsideSoftWindow()) {
      motionSafetyInhibited_ = true;
      lastError_ = String("hold: pose outside calibrated safe envelope yaw_raw=") +
                   String(yawRawCurrent_) + " pitch_raw=" + String(pitchRawCurrent_);
      Serial.print("[fleet][safety] ");
      Serial.println(lastError_);
    } else {
      motionSafetyInhibited_ = false;
      lastError_ = "";
    }
  } else if (strcmp(command, "home") == 0 || strcmp(command, "init") == 0 ||
             strcmp(command, "initiate") == 0 || strcmp(command, "initialize") == 0) {
    preemptActivePattern("home", source, true);
    clearPatternState("home");
    applyHomeCommand(source);
    return;
  } else if (strcmp(command, "target") == 0) {
    if (!validateFrame(doc["frame_id"], "target", source)) return;
    preemptActivePattern("target", source, true);
    clearPatternState("target");
    applyTargetObject(doc["target"].as<JsonObjectConst>(), source);
    return;
  } else if (strcmp(command, "aim") == 0 || strcmp(command, "manual_aim") == 0) {
    preemptActivePattern("aim", source, true);
    clearPatternState("aim");
    applyDirectAimCommand(doc, source);
    return;
  } else if (strcmp(command, "jog") == 0 || strcmp(command, "debug_jog") == 0) {
    preemptActivePattern("jog", source, true);
    clearPatternState("jog");
    applyJogCommand(doc, source);
    return;
  } else if (strcmp(command, "fire") == 0) {
    startFireFromCommand(doc, source);
    return;
  } else if (strcmp(command, "pattern") == 0) {
    handlePatternCommand(doc, source);
    return;
  } else {
    lastError_ = String("unsupported command: ") + command;
    Serial.print("[fleet][control] unsupported command from ");
    Serial.print(source);
    Serial.print(": ");
    Serial.println(command);
    return;
  }

  Serial.print("[fleet][control] command from ");
  Serial.print(source);
  Serial.print(" -> mode=");
  Serial.print(mode_);
  Serial.print(" fire_state=");
  Serial.print(fireState_);
  Serial.print(" yaw_tgt=");
  Serial.print(yawTargetDeg_, 2);
  Serial.print(" pitch_tgt=");
  Serial.println(pitchTargetDeg_, 2);
}

void TurretControl::appendStatus(JsonObject doc) const {
  const unsigned long now = millis();
  const bool inProgress = commandInProgress();
  const bool ready = readyForNextCommand();
  doc["mode"] = mode_;
  doc["command_state"] = commandState();
  doc["command_in_progress"] = inProgress;
  doc["ready_for_next_command"] = ready;
  doc["preemptible"] = config_.configured && !brownoutLockoutActive_ && mode_ != "UNCONFIGURED";
  doc["active_command_id"] = lastCommandId_;
  doc["command_policy"] = "latest_wins_preemptive";
  doc["frame_id"] = config_.frameId;
  doc["pattern_state"] = patternState_;
  doc["pattern_id"] = activePatternId_;
  doc["pattern_instance_id"] = activePatternInstanceId_;
  doc["pattern_step_index"] = patternStepIndex_;
  doc["pattern_step_count"] = patternPlan_.stepCount;
  doc["pattern_step_type"] = patternStepTypeName(patternCurrentStepType_);
  doc["pattern_loop_index"] = patternLoopIndex_;
  doc["pattern_loop_count"] = patternPlan_.loopCount;
  doc["pattern_last_error"] = lastPatternError_;
  doc["fire_state"] = fireState_;
  doc["fire_sequence"] = fireSequenceName();
  doc["fire_remaining_ms"] = (isFireSequenceActive() && fireKeepAliveUntilMs_ > now)
                                ? static_cast<uint32_t>(fireKeepAliveUntilMs_ - now)
                                : 0;
  doc["fire_hard_off_remaining_ms"] = (isFireSequenceActive() && fireHardOffAtMs_ > now)
                                ? static_cast<uint32_t>(fireHardOffAtMs_ - now)
                                : 0;
  doc["last_error"] = lastError_;
  doc["last_command_id"] = lastCommandId_;

  JsonObject fire = doc.createNestedObject("fire_output_state");
  fire["esc_run_us_config"] = config_.fireEscRunUs;
  fire["esc_stop_us_config"] = config_.fireEscStopUs;
  fire["relay_step_delay_ms"] = config_.fireRelayStepDelayMs;
  fire["esc_attached"] = escAttached_;
  fire["esc_command_us"] = escLastCommandUs_;
  fire["relay_outputs_attached"] = relayOutputsAttached_;
  fire["relay_ch1_on"] = relayCh1On_;
  fire["relay_ch2_on"] = relayCh2On_;
  fire["relay_ch3_on"] = relayCh3On_;
  fire["requested_hold_ms"] = fireRequestedHoldMs_;
  fire["pending_fire"] = pendingFire_;
  fire["pending_fire_hold_ms"] = pendingFireHoldMs_;
  fire["aim_stable_ms"] = aimReachedSinceMs_ == 0 ? 0 : static_cast<uint32_t>(now - aimReachedSinceMs_);

  JsonObject motion = doc.createNestedObject("motion_state");
  motion["brownout_lockout"] = brownoutLockoutActive_;
  motion["attached"] = motionEnabled_;
  motion["yaw_attached"] = yawServoAttached_;
  motion["pitch_attached"] = pitchServoAttached_;
  motion["selected_axis"] = String(selectedMotionAxis_);
  motion["locked_axis"] = String(lockedMotionAxis_);
  motion["yaw_raw"] = yawRawCurrent_;
  motion["pitch_raw"] = pitchRawCurrent_;
  motion["yaw_current_deg"] = yawCurrentDeg_;
  motion["pitch_current_deg"] = pitchCurrentDeg_;
  motion["yaw_target_deg"] = yawTargetDeg_;
  motion["pitch_target_deg"] = pitchTargetDeg_;
  motion["yaw_goal_deg"] = yawGoalDeg_;
  motion["pitch_goal_deg"] = pitchGoalDeg_;
  motion["target_slew_active"] = targetSlewActive_;
  motion["yaw_tracking_suppressed"] = yawTrackingSuppressed_;
  motion["yaw_command_us"] = yawLastCommandUs_;
  motion["pitch_command_us"] = pitchLastCommandUs_;
  motion["aim_reached"] = aimReached();
  motion["tracking_active"] = (mode_ == "HOME" || mode_ == "TARGET" || mode_ == "PATTERN");
  motion["safety_inhibited"] = motionSafetyInhibited_;

  JsonObject motionConfig = doc.createNestedObject("motion_config");
  motionConfig["dead_pitch_deg"] = config_.deadPitchDeg;
  motionConfig["idle_yaw_min_deg"] = config_.idleYawMinDeg;
  motionConfig["idle_yaw_max_deg"] = config_.idleYawMaxDeg;
  motionConfig["idle_pitch_min_deg"] = config_.idlePitchMinDeg;
  motionConfig["idle_pitch_max_deg"] = config_.idlePitchMaxDeg;
  motionConfig["yaw_stop_us"] = config_.yawStopUs;
  motionConfig["pitch_stop_us"] = config_.pitchStopUs;
  motionConfig["servo_max_delta_us"] = config_.servoMaxDeltaUs;
  motionConfig["yaw_max_delta_us"] = config_.yawMaxDeltaUs;
  motionConfig["pitch_max_delta_us"] = config_.pitchMaxDeltaUs;
  motionConfig["yaw_min_drive_us"] = config_.yawMinDriveUs;
  motionConfig["yaw_plus_max_delta_us"] = config_.yawPlusMaxDeltaUs;
  motionConfig["yaw_minus_max_delta_us"] = config_.yawMinusMaxDeltaUs;
  motionConfig["yaw_plus_min_drive_us"] = config_.yawPlusMinDriveUs;
  motionConfig["yaw_minus_min_drive_us"] = config_.yawMinusMinDriveUs;
  motionConfig["pitch_min_drive_us"] = config_.pitchMinDriveUs;
  motionConfig["servo_attach_settle_ms"] = config_.servoAttachSettleMs;
  motionConfig["axis_switch_cooldown_ms"] = config_.axisSwitchCooldownMs;
  motionConfig["axis_divergence_guard_ms"] = config_.axisDivergenceGuardMs;
  motionConfig["axis_divergence_margin_deg"] = config_.axisDivergenceMarginDeg;
  motionConfig["yaw_invert_motor"] = yawInvertMotor_;
  motionConfig["pitch_invert_motor"] = pitchInvertMotor_;
  motionConfig["yaw_continuous_feedback"] = kYawContinuousFeedback;
  motionConfig["home_yaw_deg"] = config_.homeYawDeg;
  motionConfig["home_pitch_deg"] = config_.homePitchDeg;
  motionConfig["command_envelope_ratio"] = config_.commandEnvelopeRatio;
  motionConfig["yaw_command_min_deg"] = yawCommandMinDeg();
  motionConfig["yaw_command_max_deg"] = yawCommandMaxDeg();
  motionConfig["pitch_command_min_deg"] = pitchCommandMinDeg();
  motionConfig["pitch_command_max_deg"] = pitchCommandMaxDeg();
  motionConfig["yaw_soft_min_deg"] = config_.yawMinDeg;
  motionConfig["yaw_soft_max_deg"] = config_.yawMaxDeg;
  motionConfig["pitch_soft_min_deg"] = config_.pitchMinDeg;
  motionConfig["pitch_soft_max_deg"] = config_.pitchMaxDeg;
  motionConfig["yaw_soft_low_raw"] = yawSoftLowRaw();
  motionConfig["yaw_soft_high_raw"] = yawSoftHighRaw();
  motionConfig["pitch_soft_low_raw"] = pitchSoftLowRaw();
  motionConfig["pitch_soft_high_raw"] = pitchSoftHighRaw();

  JsonObject aim = doc.createNestedObject("aim_state");
  aim["has_target"] = haveTarget_;
  aim["target_unit"] = config_.mqttTargetUnit;
  aim["yaw_zero_reference"] = config_.yawZeroReference;
  aim["yaw_offset_deg"] = config_.yawOffsetDeg;
  aim["pitch_offset_deg"] = config_.pitchOffsetDeg;
  aim["yaw_axis_offset_deg"] = config_.yawAxisOffsetDeg;
  aim["pitch_axis_offset_deg"] = config_.pitchAxisOffsetDeg;
  aim["pitch_reachable"] = pitchReachable_;
  aim["solved_yaw_deg"] = solvedYawDeg_;
  aim["solved_pitch_deg"] = solvedPitchDeg_;
  aim["clamped_yaw_deg"] = clampedYawDeg_;
  aim["clamped_pitch_deg"] = clampedPitchDeg_;
  aim["yaw_error_deg"] = yawGoalDeg_ - yawCurrentDeg_;
  aim["pitch_error_deg"] = pitchGoalDeg_ - pitchCurrentDeg_;
  aim["yaw_setpoint_error_deg"] = yawTargetDeg_ - yawCurrentDeg_;
  aim["pitch_setpoint_error_deg"] = pitchTargetDeg_ - pitchCurrentDeg_;

  JsonObject pose = aim.createNestedObject("turret_pose_cm");
  pose["x"] = config_.xCm;
  pose["y"] = config_.yCm;
  pose["z"] = config_.zCm;

  JsonObject input = aim.createNestedObject("last_target_input");
  input["x"] = lastTargetInputX_;
  input["y"] = lastTargetInputY_;
  input["z"] = lastTargetInputZ_;

  JsonObject targetCm = aim.createNestedObject("last_target_cm");
  targetCm["x"] = lastTargetCmX_;
  targetCm["y"] = lastTargetCmY_;
  targetCm["z"] = lastTargetCmZ_;
}

const char* TurretControl::mode() const {
  return mode_.c_str();
}

const char* TurretControl::fireState() const {
  return fireState_.c_str();
}

const char* TurretControl::patternState() const {
  return patternState_.c_str();
}

bool TurretControl::commandInProgress() const {
  const bool patternActive = patternPlan_.kind != PATTERN_NONE ||
                             mode_ == "PATTERN" ||
                             patternState_ != "IDLE";
  const bool fireActive = fireState_ == "FIRING" ||
                          isFireSequenceActive() ||
                          pendingFire_;
  const bool finiteMotionActive = (mode_ == "HOME" || mode_ == "TARGET") &&
                                  (targetSlewActive_ ||
                                   selectedMotionAxis_ != 'N' ||
                                   !aimReached());
  // IDLE is a long-running behavior rather than a finite job, but it is still
  // an active commanded state; Command Center should not treat it as "done".
  return patternActive || fireActive || finiteMotionActive || mode_ == "IDLE";
}

bool TurretControl::readyForNextCommand() const {
  return config_.configured &&
         mode_ == "WAIT_COMMAND" &&
         patternPlan_.kind == PATTERN_NONE &&
         patternState_ == "IDLE" &&
         fireState_ != "FIRING" &&
         !isFireSequenceActive() &&
         !pendingFire_ &&
         !targetSlewActive_ &&
         selectedMotionAxis_ == 'N' &&
         !brownoutLockoutActive_ &&
         !motionSafetyInhibited_;
}

const char* TurretControl::commandState() const {
  if (!config_.configured || mode_ == "UNCONFIGURED") return "unconfigured";
  if (brownoutLockoutActive_) return "blocked_brownout";
  if (motionSafetyInhibited_) return "blocked_safety";
  if (mode_ == "DEAD") return "dead";
  if (readyForNextCommand()) return "ready";
  if (commandInProgress()) return "busy";
  return "settling";
}

String TurretControl::statusSignature() const {
  String signature;
  signature.reserve(160 + lastCommandId_.length() + lastError_.length() +
                    activePatternId_.length() + activePatternInstanceId_.length());
  signature += mode_;
  signature += '|';
  signature += commandState();
  signature += '|';
  signature += commandInProgress() ? '1' : '0';
  signature += '|';
  signature += readyForNextCommand() ? '1' : '0';
  signature += '|';
  signature += fireState_;
  signature += '|';
  signature += fireSequenceName();
  signature += '|';
  signature += patternState_;
  signature += '|';
  signature += activePatternId_;
  signature += '|';
  signature += activePatternInstanceId_;
  signature += '|';
  signature += String(patternStepIndex_);
  signature += '/';
  signature += String(patternPlan_.stepCount);
  signature += '|';
  signature += patternStepTypeName(patternCurrentStepType_);
  signature += '|';
  signature += lastCommandId_;
  signature += '|';
  signature += lastError_;
  return signature;
}

bool TurretControl::isSafeForOta() const {
  if (brownoutLockoutActive_ || motionSafetyInhibited_) return false;
  if (fireState_ == "FIRING" || isFireSequenceActive()) return false;
  if (patternPlan_.kind != PATTERN_NONE) return false;
  if (mode_ == "PATTERN" || mode_ == "FIRING" || mode_ == "IDLE" || mode_ == "DEAD") return false;
  if (mode_ == "HOME" || mode_ == "TARGET") {
    return aimReached() && !targetSlewActive_ &&
           selectedMotionAxis_ == 'N' &&
           yawLastCommandUs_ == config_.yawStopUs &&
           pitchLastCommandUs_ == config_.pitchStopUs;
  }
  return mode_ == "WAIT_COMMAND" || mode_ == "UNCONFIGURED";
}

bool TurretControl::wantsFastStatus() const {
  return mode_ == "PATTERN" ||
         mode_ == "HOME" ||
         mode_ == "TARGET" ||
         fireState_ == "FIRING" ||
         isFireSequenceActive() ||
         patternPlan_.kind != PATTERN_NONE;
}

}  // namespace turret_fleet
}  // namespace battlebang
