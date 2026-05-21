#include "go2/fallback/offline_hit_fallback.h"

namespace go2 {

void OfflineHitFallback::begin(HardwareSerial& hpSerial) {
  hpSerial_ = &hpSerial;
  hp_ = HP_MAX;
  down_ = false;
  sendHpToJetson();
}

void OfflineHitFallback::reset(RingDisplay& ringDisplay) {
  hp_ = HP_MAX;
  down_ = false;
  ringDisplay.clearRemoteDisplay();
  ringDisplay.clearDamageBlink();
  ringDisplay.markDirty();
  sendHpToJetson();
}

void OfflineHitFallback::applyDamage(int damage, RingDisplay& ringDisplay) {
  if (down_) return;

  int oldHp = hp_;
  hp_ -= damage;
  if (hp_ < 0) hp_ = 0;
  ringDisplay.applyDamageBlink(oldHp, hp_);

  if (hp_ == 0) {
    down_ = true;
  }
  sendHpToJetson();
  ringDisplay.markDirty();
}

void OfflineHitFallback::sendHpToJetson() {
  if (hpSerial_ == nullptr) return;
  hpSerial_->println(hp_);
}

int OfflineHitFallback::hp() const {
  return hp_;
}

bool OfflineHitFallback::down() const {
  return down_;
}

}  // namespace go2
