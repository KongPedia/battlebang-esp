#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

#include "../config/runtime_config.h"

namespace battlebang {
namespace turret_fleet {

class TurretControl {
 public:
  void begin(const RuntimeConfig& config);
  void applyConfig(const RuntimeConfig& config);
  void setBrownoutLockout(bool active);
  bool brownoutLockoutActive() const;
  bool recoverBrownoutLockoutIfSafe(const char* source);
  void enterBootInitialTarget(bool motionAllowed = true);
  void loop();
  void handleCommandJson(JsonDocument& doc, const char* source);
  void appendStatus(JsonObject doc) const;
  const char* mode() const;
  const char* fireState() const;
  const char* patternState() const;
  bool isSafeForOta() const;

 private:
  RuntimeConfig config_;
  String mode_ = "BOOT";
  String fireState_ = "SAFE_OFF";
  String patternState_ = "IDLE";
  String lastError_;
  String lastCommandId_;
  String activePatternId_;
  String activePatternInstanceId_;
  String postFireMode_ = "WAIT_COMMAND";

  Servo yawServo_;
  Servo pitchServo_;
  bool yawServoAttached_ = false;
  bool pitchServoAttached_ = false;
  bool motionEnabled_ = false;
  unsigned long lastLoopMs_ = 0;
  unsigned long lastMotionDebugMs_ = 0;
  float yawTargetDeg_ = 0.0f;
  float pitchTargetDeg_ = 0.0f;
  float yawGoalDeg_ = 0.0f;
  float pitchGoalDeg_ = 0.0f;
  bool targetSlewActive_ = false;
  float yawCurrentDeg_ = 0.0f;
  float pitchCurrentDeg_ = 0.0f;
  int yawRawCurrent_ = 0;
  int pitchRawCurrent_ = 0;
  bool yawInvertMotor_ = false;
  bool pitchInvertMotor_ = false;
  float yawPrevErrorPseudo_ = 0.0f;
  float pitchPrevErrorPseudo_ = 0.0f;
  float yawIntegralPseudo_ = 0.0f;
  float pitchIntegralPseudo_ = 0.0f;
  float yawWarpAdc_ = 0.0f;
  bool idleSweepForward_ = true;
  bool idlePitchUp_ = true;
  float deadYawHoldDeg_ = 0.0f;
  int yawLastCommandUs_ = 1500;
  int pitchLastCommandUs_ = 1500;
  unsigned long yawAttachedAtMs_ = 0;
  unsigned long pitchAttachedAtMs_ = 0;
  unsigned long lastAxisSwitchMs_ = 0;
  char selectedMotionAxis_ = 'N';
  char lockedMotionAxis_ = 'N';
  float yawGuardStartErrorDeg_ = 0.0f;
  float pitchGuardStartErrorDeg_ = 0.0f;
  unsigned long yawGuardStartMs_ = 0;
  unsigned long pitchGuardStartMs_ = 0;
  int yawGuardSign_ = 0;
  int pitchGuardSign_ = 0;

  float lastTargetInputX_ = 0.0f;
  float lastTargetInputY_ = 0.0f;
  float lastTargetInputZ_ = 0.0f;
  float lastTargetCmX_ = 0.0f;
  float lastTargetCmY_ = 0.0f;
  float lastTargetCmZ_ = 0.0f;
  float solvedYawDeg_ = 0.0f;
  float solvedPitchDeg_ = 0.0f;
  float clampedYawDeg_ = 0.0f;
  float clampedPitchDeg_ = 0.0f;
  bool pitchReachable_ = true;
  bool haveTarget_ = false;
  bool motionSafetyInhibited_ = false;
  bool yawTrackingSuppressed_ = false;
  bool brownoutLockoutActive_ = false;

  enum FireSequenceState {
    FIRE_SEQUENCE_IDLE,
    FIRE_SEQUENCE_CH2_ON_WAIT,
    FIRE_SEQUENCE_CH1_ON_WAIT,
    FIRE_SEQUENCE_CH3_ON_WAIT,
    FIRE_SEQUENCE_RUNNING,
    FIRE_SEQUENCE_ESC_OFF_WAIT,
    FIRE_SEQUENCE_CH3_OFF_WAIT,
    FIRE_SEQUENCE_CH1_OFF_WAIT,
    FIRE_SEQUENCE_CH2_OFF_WAIT
  };

  Servo esc_;
  bool escAttached_ = false;
  bool relayOutputsAttached_ = false;
  bool relayCh1On_ = false;
  bool relayCh2On_ = false;
  bool relayCh3On_ = false;
  int escLastCommandUs_ = 1000;
  FireSequenceState fireSequenceState_ = FIRE_SEQUENCE_IDLE;
  unsigned long fireStateTs_ = 0;
  unsigned long fireKeepAliveUntilMs_ = 0;
  unsigned long fireStartedMs_ = 0;
  unsigned long fireRequestedHoldMs_ = 0;
  bool fireRestartRequested_ = false;
  bool pendingFire_ = false;
  unsigned long pendingFireHoldMs_ = 0;
  unsigned long aimReachedSinceMs_ = 0;

  void ensureYawAttached(const char* reason);
  void ensurePitchAttached(const char* reason);
  void detachYawOutput(const char* reason);
  void detachPitchOutput(const char* reason);
  void stopMotionOutputs();
  bool runBootAxisProbe();
  bool probeAxisDirection(const char* axis, bool positivePulse);
  bool recoverMotionSoftWindow(const char* source);
  bool recoverAxisTowardSoftWindow(const char* axis);
  bool motionInsideSoftWindow() const;
  bool pitchInsideSoftWindow() const;
  bool motionReadingsStableInSoftWindow(const char* source);
  bool pitchReadingsStableInSoftWindow(const char* source);
  bool ensureMotionSafetyForTracking(const char* source);
  bool ensurePitchSafetyForTracking(const char* source);
  void resetPidState();
  void updateCurrentAngles();
  void setTrackedTarget(float yawDeg, float pitchDeg);
  void setPitchOnlyTrackedTarget(float pitchDeg);
  void updateTrackedTargetSlew(float dtS);
  void updateIdleSweep(float dtS);
  void updateDeadTarget(float dtS);
  bool axisConvergenceAllowed(const char* axis,
                              float goalErrorDeg,
                              float& guardStartErrorDeg,
                              unsigned long& guardStartMs,
                              int& guardSign);
  void resetAxisGuard(char axis);
  void runPidAxis(Servo& servo,
                  float currentDeg,
                  float targetDeg,
                  float adcPerDeg,
                  float deadband,
                  float maxDeltaUs,
                  float minDriveUs,
                  bool invertMotor,
                  int stopUs,
                  float& prevErrorPseudo,
                  float& integralPseudo,
                  int& lastCommandUs);
  bool validateFrame(JsonVariantConst frameVariant, const char* command, const char* source);
  bool applyTargetObject(JsonObjectConst target, const char* source);
  bool applyTargetCm(float xCm, float yCm, float zCm, const char* source);
  bool applyDirectAimCommand(JsonDocument& doc, const char* source);
  bool applyHomeCommand(const char* source);
  bool applyJogCommand(JsonDocument& doc, const char* source);
  void handlePatternCommand(JsonDocument& doc, const char* source);
  void startFireFromCommand(JsonDocument& doc, const char* source);
  bool commandBlockedByBrownoutLockout(const char* command, const char* source);
  bool clearBrownoutLockoutIfSafe(const char* source);
  void parkRelayPinsSafeOff();
  void ensureRelayOutputsAttached(const char* reason);
  void relayWrite(int pin, bool on);
  void relayAllOff();
  void ensureEscAttached(const char* reason);
  void ensureEscStopSignal(const char* reason);
  void runEscNow(const char* reason);
  void forceFireOutputsSafeOff();
  void startFireSequence(unsigned long holdMs, const char* source);
  void updateFireSequence();
  bool isFireSequenceActive() const;
  bool aimReached() const;
  const char* fireSequenceName() const;
  float targetUnitToCm(float value) const;
  float clampYawCommand(float value) const;
  float clampPitchCommand(float value) const;
  int yawRawForDeg(float deg) const;
  int pitchRawForDeg(float deg) const;
  int yawSoftLowRaw() const;
  int yawSoftHighRaw() const;
  int pitchSoftLowRaw() const;
  int pitchSoftHighRaw() const;
  bool yawRawInsideSoftWindow(int raw) const;
  bool pitchRawInsideSoftWindow(int raw) const;
};

}  // namespace turret_fleet
}  // namespace battlebang
