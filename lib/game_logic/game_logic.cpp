#include "game_logic.h"
#include <cstring>

int hpToBand(int hpVal) {
  if (hpVal <= 0) return -1;
  return (hpVal - 1) / HP_PER_LAP;
}

int hpToLapHp(int hpVal) {
  if (hpVal <= 0) return 0;
  int r = hpVal % HP_PER_LAP;
  return (r == 0) ? HP_PER_LAP : r;
}

int lapHpToLit(int lapHp) {
  lapHp = clamp_int(lapHp, 0, HP_PER_LAP);
  return (long)lapHp * NUM_LEDS / HP_PER_LAP;
}

void addBlinkSegmentSameBand(int oldHp, int newHp, bool* blinkMask, int numLeds) {
  int oldLit = lapHpToLit(hpToLapHp(oldHp));
  int newLit = lapHpToLit(hpToLapHp(newHp));
  if (newLit < oldLit) {
    for (int i = newLit; i < oldLit; i++) {
      if (i >= 0 && i < numLeds) blinkMask[i] = true;
    }
  }
}

static void clearBlinkMask(bool* blinkMask, int numLeds) {
  for (int i = 0; i < numLeds; i++) blinkMask[i] = false;
}

DamageResult applyDamageLogic(int hp, bool isDead, bool* blinkMask, int numLeds, int dmg) {
  DamageResult r = { hp, isDead, true };
  if (isDead) return r;

  int oldHp = hp;
  int oldBand = hpToBand(oldHp);

  hp -= dmg;
  if (hp < 0) hp = 0;
  r.newHp = hp;

  int newBand = hpToBand(hp);

  if (newBand != oldBand) clearBlinkMask(blinkMask, numLeds);

  if (hp > 0 && newBand == oldBand) {
    addBlinkSegmentSameBand(oldHp, hp, blinkMask, numLeds);
  } else if (hp > 0 && newBand != oldBand) {
    int newLit = lapHpToLit(hpToLapHp(hp));
    for (int i = newLit; i < numLeds; i++) blinkMask[i] = true;
  }

  if (hp == 0) r.isDead = true;
  return r;
}

int targetIdToDmg(int targetId) {
  return (targetId == 1) ? DMG_T1 : DMG_T2;
}

static bool isSpaceChar(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void trimInPlace(char* s, int maxLen) {
  size_t n = 0;
  while (n < (size_t)maxLen && s[n]) n++;
  int i = 0;
  while (i < (int)n && isSpaceChar(s[i])) i++;
  if (i > 0) memmove(s, s + i, n - i + 1);

  size_t len = strlen(s);
  while (len > 0 && isSpaceChar(s[len - 1])) { s[len - 1] = 0; len--; }
}

void toLowerInPlace(char* s) {
  for (; *s; s++) {
    if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
  }
}

int parseCommand(const char* cmd) {
  if (!cmd || cmd[0] == 0) return 0;
  if (strcmp(cmd, "q") == 0) return 1;
  if (strcmp(cmd, "w") == 0) return 2;
  if (strcmp(cmd, "r") == 0) return 3;
  if (strcmp(cmd, "f") == 0) return 4;
  return 0;
}
