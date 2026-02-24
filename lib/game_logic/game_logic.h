#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

// main.cpp와 동일한 상수 (테스트용, Arduino 없음)
constexpr int HP_MAX = 3000;
constexpr int HP_PER_LAP = 1000;
constexpr int NUM_LEDS = 40;
constexpr int HIT_THRESHOLD = 2500;
constexpr int DMG_T1 = 150;
constexpr int DMG_T2 = 50;

// constrain 대체 (호스트용)
inline static int clamp_int(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// HP -> 밴드 인덱스 (0=빨강, 1=노랑, 2=초록, -1=죽음)
int hpToBand(int hpVal);

// HP -> 현재 랩 내 HP (1..1000)
int hpToLapHp(int hpVal);

// 랩 내 HP -> 켜질 LED 개수 (0..NUM_LEDS)
int lapHpToLit(int lapHp);

// 같은 밴드 내에서 데미지로 꺼진 구간을 blinkMask에 표시
void addBlinkSegmentSameBand(int oldHp, int newHp, bool* blinkMask, int numLeds);

// 데미지 적용 결과 (hp, isDead, blinkMask 갱신) — 상태를 인자로 받음
struct DamageResult {
  int newHp;
  bool isDead;
  bool ledsDirty;
};
DamageResult applyDamageLogic(int hp, bool isDead, bool* blinkMask, int numLeds, int dmg);

// 타겟 ID -> 데미지
int targetIdToDmg(int targetId);

// 명령 파싱용: trim, toLower (main.cpp와 동일 동작)
void trimInPlace(char* s, int maxLen);
void toLowerInPlace(char* s);

// 명령 문자열 -> 동작 식별 (0=unknown, 1=q, 2=w, 3=r, 4=f)
int parseCommand(const char* cmd);

#endif
