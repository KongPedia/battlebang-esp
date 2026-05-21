#include "go2/fire/fire_control.h"

namespace go2 {

void FireControl::begin() {
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  relayOff();
  servoAttachPwm();
  reset();
}

void FireControl::reset() {
  state_ = FIRE_IDLE;
  relayOff();
  servoCur_ = SERVO_HOME_ANGLE;
  servoTarget_ = SERVO_HOME_ANGLE;
  servoWriteAngle(servoCur_);
}

bool FireControl::start(bool dead, uint32_t now) {
  if (dead || isFiring()) return false;
  if (now - lastFireStartMs_ < FIRE_COOLDOWN_MS) return false;

  lastFireStartMs_ = now;
  int next = (servoCur_ == FIRE_POS_A) ? FIRE_POS_B : FIRE_POS_A;
  setServoTarget(next);
  state_ = FIRE_SERVO_MOVING;
  Serial.println("[FIRE] start");
  return true;
}

void FireControl::update(uint32_t now) {
  switch (state_) {
    case FIRE_IDLE:
      return;
    case FIRE_SERVO_MOVING:
      if (updateServoMove(now)) {
        digitalWrite(RELAY1_PIN, RELAY_ON);
        digitalWrite(RELAY2_PIN, RELAY_OFF);
        state_ = FIRE_RELAY_WAIT1;
        fireTimerMs_ = now;
        Serial.println("[RELAY] CH1 ON");
      }
      return;
    case FIRE_RELAY_WAIT1:
      if (now - fireTimerMs_ >= RELAY_DELAY1_MS) {
        digitalWrite(RELAY2_PIN, RELAY_ON);
        state_ = FIRE_RELAY_WAIT2;
        fireTimerMs_ = now;
        Serial.println("[RELAY] CH2 ON");
      }
      return;
    case FIRE_RELAY_WAIT2:
      if (now - fireTimerMs_ >= RELAY_DELAY2_MS) {
        relayOff();
        state_ = FIRE_IDLE;
        Serial.println("[RELAY] ALL OFF / FIRE done");
      }
      return;
  }
}

bool FireControl::isFiring() const {
  return state_ != FIRE_IDLE;
}

uint32_t FireControl::usToDuty(int us) {
  us = constrain(us, 500, 2400);
  return (uint32_t)((((uint64_t)us) * ((1UL << SERVO_PWM_RES) - 1)) / 20000ULL);
}

int FireControl::angleToUs(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, 500, 2400);
}

void FireControl::servoAttachPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SERVO_PIN, SERVO_PWM_FREQ, SERVO_PWM_RES);
#else
  ledcSetup(SERVO_PWM_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RES);
  ledcAttachPin(SERVO_PIN, SERVO_PWM_CHANNEL);
#endif
}

void FireControl::servoWriteDuty(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(SERVO_PIN, duty);
#else
  ledcWrite(SERVO_PWM_CHANNEL, duty);
#endif
}

void FireControl::servoWriteAngle(int angle) {
  servoWriteDuty(usToDuty(angleToUs(angle)));
}

void FireControl::relayOff() {
  digitalWrite(RELAY1_PIN, RELAY_OFF);
  digitalWrite(RELAY2_PIN, RELAY_OFF);
}

void FireControl::setServoTarget(int angle) {
  servoTarget_ = constrain(angle, 0, 180);
}

bool FireControl::updateServoMove(uint32_t now) {
  if (servoCur_ == servoTarget_) return true;
  if (now - lastServoMs_ < SERVO_STEP_DT_MS) return false;
  lastServoMs_ = now;

  if (servoCur_ < servoTarget_) servoCur_ = min(servoCur_ + SERVO_STEP, servoTarget_);
  else servoCur_ = max(servoCur_ - SERVO_STEP, servoTarget_);

  servoWriteAngle(servoCur_);
  return servoCur_ == servoTarget_;
}

}  // namespace go2
