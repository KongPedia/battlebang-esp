#include "go2/game/game_state.h"

namespace go2 {

void GameState::begin(HardwareSerial& hpSerial) {
  hpSerial_ = &hpSerial;
  hp_ = HP_MAX;
  dead_ = false;
  sendHpToJetson();
}

void GameState::reset(LedRing& ledRing) {
  hp_ = HP_MAX;
  dead_ = false;
  ledRing.clearRemoteDisplay();
  ledRing.clearDamageBlink();
  ledRing.markDirty();
  sendHpToJetson();
}

void GameState::applyDamage(int damage, LedRing& ledRing) {
  if (dead_) return;

  int oldHp = hp_;
  hp_ -= damage;
  if (hp_ < 0) hp_ = 0;
  ledRing.applyDamageBlink(oldHp, hp_);

  if (hp_ == 0) {
    dead_ = true;
  }
  sendHpToJetson();
  ledRing.markDirty();
}

void GameState::applyAuthorityDown(bool down, LedRing& ledRing) {
  if (down) {
    hp_ = 0;
    dead_ = true;
  } else if (dead_) {
    dead_ = false;
    if (hp_ <= 0) hp_ = HP_MAX;
    ledRing.clearDamageBlink();
  }
  ledRing.markDirty();
}

void GameState::sendHpToJetson() {
  if (hpSerial_ == nullptr) return;
  hpSerial_->println(hp_);
}

int GameState::hp() const {
  return hp_;
}

bool GameState::isDead() const {
  return dead_;
}

}  // namespace go2
