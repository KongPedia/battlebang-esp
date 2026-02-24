#include "game_logic.h"
#include <gtest/gtest.h>
#include <cstring>

// ============== hpToBand ==============
TEST(GameLogic, HpToBand_ZeroOrNegative) {
  EXPECT_EQ(hpToBand(0), -1);
  EXPECT_EQ(hpToBand(-1), -1);
}

TEST(GameLogic, HpToBand_RedBand) {
  EXPECT_EQ(hpToBand(1), 0);
  EXPECT_EQ(hpToBand(500), 0);
  EXPECT_EQ(hpToBand(1000), 0);
}

TEST(GameLogic, HpToBand_YellowBand) {
  EXPECT_EQ(hpToBand(1001), 1);
  EXPECT_EQ(hpToBand(1500), 1);
  EXPECT_EQ(hpToBand(2000), 1);
}

TEST(GameLogic, HpToBand_GreenBand) {
  EXPECT_EQ(hpToBand(2001), 2);
  EXPECT_EQ(hpToBand(3000), 2);
}

// ============== hpToLapHp ==============
TEST(GameLogic, HpToLapHp_ZeroOrNegative) {
  EXPECT_EQ(hpToLapHp(0), 0);
  EXPECT_EQ(hpToLapHp(-1), 0);
}

TEST(GameLogic, HpToLapHp_FirstLap) {
  EXPECT_EQ(hpToLapHp(1), 1);
  EXPECT_EQ(hpToLapHp(999), 999);
  EXPECT_EQ(hpToLapHp(1000), 1000);
}

TEST(GameLogic, HpToLapHp_SecondLap) {
  EXPECT_EQ(hpToLapHp(1001), 1);
  EXPECT_EQ(hpToLapHp(2000), 1000);
}

TEST(GameLogic, HpToLapHp_ThirdLap) {
  EXPECT_EQ(hpToLapHp(3000), 1000);
}

// ============== lapHpToLit ==============
TEST(GameLogic, LapHpToLit_Bounds) {
  EXPECT_EQ(lapHpToLit(0), 0);
  EXPECT_EQ(lapHpToLit(1000), NUM_LEDS);
}

TEST(GameLogic, LapHpToLit_Half) {
  EXPECT_EQ(lapHpToLit(500), 20);  // 500/1000 * 40 = 20
}

// ============== addBlinkSegmentSameBand ==============
TEST(GameLogic, AddBlinkSegmentSameBand_Decrease) {
  bool mask[40] = {false};
  addBlinkSegmentSameBand(800, 400, mask, 40);  // 같은 밴드(0) 내 감소
  int count = 0;
  for (int i = 0; i < 40; i++) if (mask[i]) count++;
  EXPECT_GT(count, 0);
  // 800 -> lap 800 -> lit 32, 400 -> lap 400 -> lit 16. So 16..31 should be true
  EXPECT_TRUE(mask[16]);
  EXPECT_TRUE(mask[31]);
  EXPECT_FALSE(mask[15]);
  EXPECT_FALSE(mask[32]);
}

TEST(GameLogic, AddBlinkSegmentSameBand_NoDecrease) {
  bool mask[40] = {false};
  addBlinkSegmentSameBand(400, 800, mask, 40);
  for (int i = 0; i < 40; i++) EXPECT_FALSE(mask[i]);
}

// ============== applyDamageLogic ==============
TEST(GameLogic, ApplyDamage_Normal) {
  bool mask[40] = {false};
  auto r = applyDamageLogic(HP_MAX, false, mask, 40, 100);
  EXPECT_EQ(r.newHp, 2900);
  EXPECT_FALSE(r.isDead);
  EXPECT_TRUE(r.ledsDirty);
}

TEST(GameLogic, ApplyDamage_ToZero) {
  bool mask[40] = {false};
  auto r = applyDamageLogic(100, false, mask, 40, 100);
  EXPECT_EQ(r.newHp, 0);
  EXPECT_TRUE(r.isDead);
}

TEST(GameLogic, ApplyDamage_WhenDead_NoChange) {
  bool mask[40] = {false};
  auto r = applyDamageLogic(0, true, mask, 40, 50);
  EXPECT_EQ(r.newHp, 0);
  EXPECT_TRUE(r.isDead);
}

TEST(GameLogic, ApplyDamage_ClampAtZero) {
  bool mask[40] = {false};
  auto r = applyDamageLogic(30, false, mask, 40, 100);
  EXPECT_EQ(r.newHp, 0);
  EXPECT_TRUE(r.isDead);
}

TEST(GameLogic, ApplyDamage_BandChange) {
  bool mask[40] = {false};
  // 1000 -> 900: 같은 밴드(0), 일부 blink
  auto r = applyDamageLogic(1000, false, mask, 40, 100);
  EXPECT_EQ(r.newHp, 900);
  EXPECT_EQ(hpToBand(900), 0);
}

// ============== targetIdToDmg ==============
TEST(GameLogic, TargetIdToDmg) {
  EXPECT_EQ(targetIdToDmg(1), DMG_T1);
  EXPECT_EQ(targetIdToDmg(2), DMG_T2);
}

// ============== trimInPlace, toLowerInPlace, parseCommand ==============
TEST(GameLogic, TrimInPlace) {
  char buf[64];
  strncpy(buf, "  r  ", sizeof(buf) - 1); buf[sizeof(buf)-1] = 0;
  trimInPlace(buf, 64);
  EXPECT_STREQ(buf, "r");
}

TEST(GameLogic, ToLowerInPlace) {
  char buf[64];
  strncpy(buf, "Q", sizeof(buf) - 1); buf[1] = 0;
  toLowerInPlace(buf);
  EXPECT_STREQ(buf, "q");
}

TEST(GameLogic, ParseCommand_Q) {
  EXPECT_EQ(parseCommand("q"), 1);
  EXPECT_EQ(parseCommand("Q"), 0);  // parseCommand는 소문자 가정 (toLower 후 호출)
}

TEST(GameLogic, ParseCommand_All) {
  EXPECT_EQ(parseCommand("q"), 1);
  EXPECT_EQ(parseCommand("w"), 2);
  EXPECT_EQ(parseCommand("r"), 3);
  EXPECT_EQ(parseCommand("f"), 4);
  EXPECT_EQ(parseCommand(""), 0);
  EXPECT_EQ(parseCommand("x"), 0);
}

TEST(GameLogic, CommandPipeline_TrimAndLowerThenParse) {
  char buf[64];
  strncpy(buf, "  F  ", sizeof(buf) - 1); buf[sizeof(buf)-1] = 0;
  trimInPlace(buf, 64);
  toLowerInPlace(buf);
  EXPECT_EQ(parseCommand(buf), 4);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
