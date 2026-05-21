#pragma once

#include <Arduino.h>

#include "../config.h"

namespace go2 {

enum FireState {
  FIRE_IDLE,
  FIRE_SERVO_MOVING,
  FIRE_RELAY_WAIT1,
  FIRE_RELAY_WAIT2,
};

class FireControl {
 public:
  void begin();
  void reset();
  bool start(bool dead, uint32_t now);
  void update(uint32_t now);
  bool isFiring() const;

 private:
  FireState state_ = FIRE_IDLE;
  int servoCur_ = SERVO_HOME_ANGLE;
  int servoTarget_ = SERVO_HOME_ANGLE;
  uint32_t lastServoMs_ = 0;
  uint32_t fireTimerMs_ = 0;
  uint32_t lastFireStartMs_ = 0;

  static uint32_t usToDuty(int us);
  static int angleToUs(int angle);
  static void servoAttachPwm();
  static void servoWriteDuty(uint32_t duty);
  static void servoWriteAngle(int angle);
  static void relayOff();
  void setServoTarget(int angle);
  bool updateServoMove(uint32_t now);
};

}  // namespace go2
