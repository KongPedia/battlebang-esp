#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../../config/runtime_config.h"

namespace battlebang {
namespace turret_fleet {

// Pattern planning is intentionally pure: it turns a Command Center pattern
// payload into bounded local steps. TurretControl remains the hardware executor
// that applies motion and fire side effects.
enum PatternKind {
  PATTERN_NONE,
  PATTERN_LANE_SWEEP,
  PATTERN_TWO_POINT_BOUNCE,
  PATTERN_TELEGRAPH_COLUMN,
  PATTERN_CALIBRATION_NO_FIRE
};

enum PatternStepType {
  PATTERN_STEP_NONE,
  PATTERN_STEP_WAIT,
  PATTERN_STEP_MOVE,
  PATTERN_STEP_DWELL,
  PATTERN_STEP_FIRE,
  PATTERN_STEP_FIRE_MOVE,
  PATTERN_STEP_WAIT_FIRE_SAFE,
  PATTERN_STEP_DONE
};

struct PatternPoint {
  float xCm = 0.0f;
  float yCm = 0.0f;
  float zCm = 0.0f;
};

struct PatternStep {
  PatternStepType type = PATTERN_STEP_NONE;
  uint8_t pointIndex = 0;
  unsigned long durationMs = 0;
};

static const uint8_t kMaxPatternPoints = 6;
static const uint8_t kMaxPatternSteps = 40;

struct PatternPlan {
  PatternKind kind = PATTERN_NONE;
  PatternPoint points[kMaxPatternPoints];
  uint8_t pointCount = 0;
  PatternStep steps[kMaxPatternSteps];
  uint8_t stepCount = 0;
  unsigned long dwellMs = 500;
  unsigned long fireMs = 500;
  unsigned long moveTimeoutMs = 4000;
  uint8_t loopCount = 1;
  bool noFire = false;
  String returnTo = "wait_command";
  String error;

  void clear();
  bool addStep(PatternStepType type, uint8_t pointIndex, unsigned long durationMs);
  bool addVisit(uint8_t pointIndex, bool fire);
  bool addSweep(uint8_t pointIndex, bool waitForFireSafe = true);
};

const char* patternIdForKind(PatternKind kind);
PatternKind patternKindFromId(const char* patternId);
const char* patternStepTypeName(PatternStepType type);
bool compilePatternPlan(JsonObjectConst params,
                        PatternKind kind,
                        const RuntimeConfig& config,
                        PatternPlan& plan,
                        uint32_t selectionSeed = 0);

}  // namespace turret_fleet
}  // namespace battlebang
