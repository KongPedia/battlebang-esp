#include "pattern_plan.h"

#include <string.h>

namespace battlebang {
namespace turret_fleet {
namespace {

const unsigned long kPatternDefaultDwellMs = 500;
const unsigned long kPatternMinDwellMs = 100;
const unsigned long kPatternMaxDwellMs = 5000;
const unsigned long kPatternDefaultMoveTimeoutMs = 4000;
const unsigned long kPatternMinMoveTimeoutMs = 500;
const unsigned long kPatternMaxMoveTimeoutMs = 60000;
const unsigned long kPatternMaxFireMs = 5000;
const unsigned long kPatternMaxPhaseOffsetMs = 5000;
const uint8_t kPatternMaxLoopCount = 3;
const size_t kPatternPresetDocCapacity = 4096;
const size_t kPatternMergedParamsCapacity = 4096;

unsigned long clampUnsignedLong(unsigned long value, unsigned long lo, unsigned long hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

unsigned long getUnsignedLongOr(JsonVariantConst value, unsigned long fallback) {
  if (value.is<unsigned long>()) return value.as<unsigned long>();
  if (value.is<int>()) {
    const int intValue = value.as<int>();
    return intValue > 0 ? static_cast<unsigned long>(intValue) : 0;
  }
  return fallback;
}

uint8_t normalizePatternLoopCount(JsonObjectConst params, uint8_t fallback) {
  unsigned long loopValue = fallback;
  if (!params.isNull()) {
    if (params["loop"].is<unsigned long>() || params["loop"].is<int>()) {
      loopValue = getUnsignedLongOr(params["loop"], fallback);
    } else if (params["loops"].is<unsigned long>() || params["loops"].is<int>()) {
      loopValue = getUnsignedLongOr(params["loops"], fallback);
    }
  }
  if (loopValue == 0) loopValue = fallback;
  if (loopValue > kPatternMaxLoopCount) loopValue = kPatternMaxLoopCount;
  return static_cast<uint8_t>(loopValue);
}

unsigned long normalizePatternDwellMs(JsonObjectConst params) {
  const unsigned long requested = params.isNull()
                                    ? kPatternDefaultDwellMs
                                    : getUnsignedLongOr(params["dwell_ms"], kPatternDefaultDwellMs);
  return clampUnsignedLong(requested, kPatternMinDwellMs, kPatternMaxDwellMs);
}

unsigned long normalizePatternMoveTimeoutMs(JsonObjectConst params) {
  const unsigned long requested = params.isNull()
                                    ? kPatternDefaultMoveTimeoutMs
                                    : getUnsignedLongOr(params["move_timeout_ms"], kPatternDefaultMoveTimeoutMs);
  return clampUnsignedLong(requested, kPatternMinMoveTimeoutMs, kPatternMaxMoveTimeoutMs);
}

unsigned long normalizePatternPhaseOffsetMs(JsonObjectConst params) {
  if (params.isNull()) return 0;
  return clampUnsignedLong(getUnsignedLongOr(params["phase_offset_ms"], 0), 0, kPatternMaxPhaseOffsetMs);
}

unsigned long normalizePatternFireMs(JsonObjectConst params, const RuntimeConfig& config) {
  const unsigned long requested = params.isNull()
                                    ? config.fireDefaultHoldMs
                                    : getUnsignedLongOr(params["fire_ms"], config.fireDefaultHoldMs);
  unsigned long maxFireMs = config.fireMaxHoldMs;
  if (maxFireMs > kPatternMaxFireMs) maxFireMs = kPatternMaxFireMs;
  return clampUnsignedLong(requested, config.fireMinHoldMs, maxFireMs);
}

bool copyJsonObject(JsonObject target, JsonObjectConst source) {
  if (source.isNull()) return true;
  for (JsonPairConst pair : source) {
    if (!target[pair.key()].set(pair.value())) return false;
  }
  return true;
}

bool copyConfiguredPresetParams(PatternKind kind,
                                const RuntimeConfig& config,
                                JsonObject merged,
                                PatternPlan& plan) {
  if (config.patternPresetsJson.length() == 0) return true;

  DynamicJsonDocument presetsDoc(kPatternPresetDocCapacity);
  DeserializationError err = deserializeJson(presetsDoc, config.patternPresetsJson);
  if (err) {
    plan.error = String("pattern rejected: invalid configured pattern presets: ") + err.c_str();
    return false;
  }

  JsonObjectConst patterns = presetsDoc.as<JsonObjectConst>();
  JsonObjectConst presets = patterns["presets"].as<JsonObjectConst>();
  if (presets.isNull()) presets = patterns;
  JsonObjectConst preset = presets[patternIdForKind(kind)].as<JsonObjectConst>();
  if (preset.isNull()) return true;

  if (!copyJsonObject(merged, preset)) {
    plan.error = "pattern rejected: configured pattern preset too large";
    return false;
  }
  return true;
}

float targetUnitToCm(float value, const RuntimeConfig& config) {
  return mqttTargetsInMeters(config) ? value * 100.0f : value;
}

bool validatePointFrame(JsonVariantConst frameVariant, const RuntimeConfig& config, PatternPlan& plan) {
  const char* frame = frameVariant | "";
  if (frame[0] == '\0') return true;
  if (config.frameId == frame) return true;

  plan.error = String("frame_id mismatch for pattern_point: expected=") + config.frameId + " got=" + frame;
  return false;
}

bool parsePatternPoints(JsonArrayConst points,
                        uint8_t minPoints,
                        uint8_t maxPoints,
                        const RuntimeConfig& config,
                        const char* patternId,
                        PatternPlan& plan) {
  if (points.isNull() || points.size() < minPoints) {
    plan.error = String("pattern rejected: ") + patternId + " requires at least " +
                 String(minPoints) + " point(s)";
    return false;
  }

  plan.pointCount = 0;
  const uint8_t cappedMax = maxPoints > kMaxPatternPoints ? kMaxPatternPoints : maxPoints;
  for (JsonObjectConst point : points) {
    if (plan.pointCount >= cappedMax) break;
    if (point.isNull() || !point.containsKey("x") || !point.containsKey("y")) {
      plan.error = String("pattern rejected: ") + patternId + " point x/y missing";
      return false;
    }
    if (!validatePointFrame(point["frame_id"], config, plan)) return false;

    const float inputX = point["x"].as<float>();
    const float inputY = point["y"].as<float>();
    const float inputZ = point.containsKey("z") ? point["z"].as<float>() :
                                                  (mqttTargetsInMeters(config) ? config.defaultTargetZCm / 100.0f : config.defaultTargetZCm);
    plan.points[plan.pointCount].xCm = targetUnitToCm(inputX, config);
    plan.points[plan.pointCount].yCm = targetUnitToCm(inputY, config);
    plan.points[plan.pointCount].zCm = targetUnitToCm(inputZ, config);
    plan.pointCount++;
  }

  if (plan.pointCount < minPoints) {
    plan.error = String("pattern rejected: ") + patternId + " point count below minimum";
    return false;
  }
  return true;
}

int getOptionalPointIndex(JsonObjectConst params, uint8_t pointCount, PatternPlan& plan) {
  if (params.isNull()) return -1;
  JsonVariantConst indexVariant = params["selected_point_index"];
  if (indexVariant.isNull()) indexVariant = params["point_index"];
  if (indexVariant.isNull()) return -1;
  if (!indexVariant.is<int>() && !indexVariant.is<unsigned int>() &&
      !indexVariant.is<long>() && !indexVariant.is<unsigned long>()) {
    plan.error = "pattern rejected: telegraph_column point_index must be an integer";
    return -2;
  }
  const int index = indexVariant.as<int>();
  if (index < 0 || index >= static_cast<int>(pointCount)) {
    plan.error = "pattern rejected: telegraph_column point_index out of range";
    return -2;
  }
  return index;
}

bool compileLaneSweepPattern(JsonObjectConst params, const RuntimeConfig& config, PatternPlan& plan) {
  plan.loopCount = normalizePatternLoopCount(params, 1);
  JsonArrayConst points = params["points"].as<JsonArrayConst>();
  if (!parsePatternPoints(points, 2, 2, config, "lane_sweep", plan)) return false;

  if (!plan.addStep(PATTERN_STEP_MOVE, 0, plan.moveTimeoutMs)) return false;
  if (!plan.addStep(PATTERN_STEP_DWELL, 0, plan.dwellMs)) return false;

  const bool pingPong = params.isNull() ? true : (params["ping_pong"] | true);
  for (uint8_t loop = 0; loop < plan.loopCount; ++loop) {
    if (!plan.addSweep(1, true)) return false;
    if (pingPong) {
      if (!plan.addStep(PATTERN_STEP_DWELL, 1, plan.dwellMs)) return false;
      if (!plan.addSweep(0, true)) return false;
      // Keep the final return edge in PATTERN long enough for the closed-loop
      // yaw controller to settle after the fire window is cut. Without this
      // dwell the final sweep can report DONE immediately after a transient
      // endpoint reading, and WAIT_COMMAND then freezes the current yaw as the
      // new goal before the turret visibly reaches the lane edge.
      if (!plan.addStep(PATTERN_STEP_DWELL, 0, plan.dwellMs)) return false;
    } else if (loop + 1 < plan.loopCount) {
      if (!plan.addStep(PATTERN_STEP_MOVE, 0, plan.moveTimeoutMs)) return false;
      if (!plan.addStep(PATTERN_STEP_DWELL, 0, plan.dwellMs)) return false;
    }
  }
  return true;
}

bool compileTwoPointBouncePattern(JsonObjectConst params, const RuntimeConfig& config, PatternPlan& plan) {
  plan.loopCount = normalizePatternLoopCount(params, 2);
  JsonArrayConst points = params["points"].as<JsonArrayConst>();
  if (!parsePatternPoints(points, 2, 2, config, "two_point_bounce", plan)) return false;

  for (uint8_t loop = 0; loop < plan.loopCount; ++loop) {
    if (!plan.addVisit(0, true)) return false;
    if (!plan.addVisit(1, true)) return false;
  }
  return true;
}

bool compileTelegraphColumnPattern(JsonObjectConst params,
                                   const RuntimeConfig& config,
                                   PatternPlan& plan,
                                   uint32_t selectionSeed) {
  plan.loopCount = normalizePatternLoopCount(params, 1);
  JsonArrayConst points = params["points"].as<JsonArrayConst>();
  if (!parsePatternPoints(points, 1, kMaxPatternPoints, config, "telegraph_column", plan)) return false;

  const int requestedIndex = getOptionalPointIndex(params, plan.pointCount, plan);
  if (requestedIndex == -2) return false;
  const bool randomSelect = requestedIndex >= 0 ? false : (params.isNull() ? true : (params["random"] | true));
  for (uint8_t loop = 0; loop < plan.loopCount; ++loop) {
    if (requestedIndex >= 0) {
      if (!plan.addVisit(static_cast<uint8_t>(requestedIndex), true)) return false;
    } else if (randomSelect && plan.pointCount > 1) {
      const uint8_t selected = static_cast<uint8_t>((selectionSeed + loop) % plan.pointCount);
      if (!plan.addVisit(selected, true)) return false;
    } else {
      for (uint8_t pointIndex = 0; pointIndex < plan.pointCount; ++pointIndex) {
        if (!plan.addVisit(pointIndex, true)) return false;
      }
    }
  }
  return true;
}

bool compileCalibrationNoFirePattern(JsonObjectConst params, const RuntimeConfig& config, PatternPlan& plan) {
  plan.noFire = true;
  plan.loopCount = normalizePatternLoopCount(params, 1);
  JsonArrayConst points = params["points"].as<JsonArrayConst>();
  if (!parsePatternPoints(points, 1, kMaxPatternPoints, config, "calibration_no_fire", plan)) return false;

  for (uint8_t loop = 0; loop < plan.loopCount; ++loop) {
    for (uint8_t pointIndex = 0; pointIndex < plan.pointCount; ++pointIndex) {
      if (!plan.addVisit(pointIndex, false)) return false;
    }
  }
  return true;
}

}  // namespace

void PatternPlan::clear() {
  kind = PATTERN_NONE;
  pointCount = 0;
  stepCount = 0;
  dwellMs = kPatternDefaultDwellMs;
  fireMs = 500;
  moveTimeoutMs = kPatternDefaultMoveTimeoutMs;
  loopCount = 1;
  noFire = false;
  returnTo = "wait_command";
  error = "";
}

bool PatternPlan::addStep(PatternStepType type, uint8_t pointIndex, unsigned long durationMs) {
  if (stepCount >= kMaxPatternSteps) {
    error = "pattern rejected: too many compiled steps";
    return false;
  }
  PatternStep& step = steps[stepCount++];
  step.type = type;
  step.pointIndex = pointIndex;
  step.durationMs = durationMs;
  return true;
}

bool PatternPlan::addVisit(uint8_t pointIndex, bool fire) {
  if (!addStep(PATTERN_STEP_MOVE, pointIndex, moveTimeoutMs)) return false;
  if (!addStep(PATTERN_STEP_DWELL, pointIndex, dwellMs)) return false;
  if (fire && !noFire) {
    if (!addStep(PATTERN_STEP_FIRE, pointIndex, fireMs)) return false;
    if (!addStep(PATTERN_STEP_WAIT_FIRE_SAFE, pointIndex, 0)) return false;
  }
  return true;
}

bool PatternPlan::addSweep(uint8_t pointIndex, bool waitForFireSafe) {
  if (!addStep(PATTERN_STEP_FIRE_MOVE, pointIndex, moveTimeoutMs)) return false;
  if (waitForFireSafe && !addStep(PATTERN_STEP_WAIT_FIRE_SAFE, pointIndex, 0)) return false;
  return true;
}

const char* patternIdForKind(PatternKind kind) {
  switch (kind) {
    case PATTERN_LANE_SWEEP:
      return "lane_sweep";
    case PATTERN_TWO_POINT_BOUNCE:
      return "two_point_bounce";
    case PATTERN_TELEGRAPH_COLUMN:
      return "telegraph_column";
    case PATTERN_CALIBRATION_NO_FIRE:
      return "calibration_no_fire";
    case PATTERN_NONE:
      return "";
  }
  return "";
}

PatternKind patternKindFromId(const char* patternId) {
  if (patternId == nullptr || patternId[0] == '\0') return PATTERN_NONE;
  if (strcmp(patternId, "lane_sweep") == 0) return PATTERN_LANE_SWEEP;
  if (strcmp(patternId, "two_point_bounce") == 0) return PATTERN_TWO_POINT_BOUNCE;
  if (strcmp(patternId, "telegraph_column") == 0) return PATTERN_TELEGRAPH_COLUMN;
  if (strcmp(patternId, "calibration_no_fire") == 0) return PATTERN_CALIBRATION_NO_FIRE;
  return PATTERN_NONE;
}

const char* patternStepTypeName(PatternStepType type) {
  switch (type) {
    case PATTERN_STEP_NONE:
      return "NONE";
    case PATTERN_STEP_WAIT:
      return "WAIT";
    case PATTERN_STEP_MOVE:
      return "MOVE";
    case PATTERN_STEP_DWELL:
      return "DWELL";
    case PATTERN_STEP_FIRE:
      return "FIRE";
    case PATTERN_STEP_FIRE_MOVE:
      return "FIRE_MOVE";
    case PATTERN_STEP_WAIT_FIRE_SAFE:
      return "WAIT_FIRE_SAFE";
    case PATTERN_STEP_DONE:
      return "DONE";
  }
  return "UNKNOWN";
}

bool compilePatternPlan(JsonObjectConst params,
                        PatternKind kind,
                        const RuntimeConfig& config,
                        PatternPlan& plan,
                        uint32_t selectionSeed) {
  plan.clear();
  plan.kind = kind;

  DynamicJsonDocument mergedDoc(kPatternMergedParamsCapacity);
  JsonObject mergedParams = mergedDoc.to<JsonObject>();
  if (!copyConfiguredPresetParams(kind, config, mergedParams, plan)) return false;
  if (!copyJsonObject(mergedParams, params)) {
    plan.error = "pattern rejected: command pattern params too large";
    return false;
  }
  JsonObjectConst effectiveParams = mergedParams;

  plan.dwellMs = normalizePatternDwellMs(effectiveParams);
  plan.fireMs = normalizePatternFireMs(effectiveParams, config);
  plan.moveTimeoutMs = normalizePatternMoveTimeoutMs(effectiveParams);
  plan.returnTo = effectiveParams["return_to"] | "wait_command";
  if (effectiveParams["return_to_idle"] | false) plan.returnTo = "idle";

  const unsigned long phaseOffsetMs = normalizePatternPhaseOffsetMs(effectiveParams);
  if (phaseOffsetMs > 0 && !plan.addStep(PATTERN_STEP_WAIT, 0, phaseOffsetMs)) return false;

  bool compiled = false;
  switch (kind) {
    case PATTERN_LANE_SWEEP:
      compiled = compileLaneSweepPattern(effectiveParams, config, plan);
      break;
    case PATTERN_TWO_POINT_BOUNCE:
      compiled = compileTwoPointBouncePattern(effectiveParams, config, plan);
      break;
    case PATTERN_TELEGRAPH_COLUMN:
      compiled = compileTelegraphColumnPattern(effectiveParams, config, plan, selectionSeed);
      break;
    case PATTERN_CALIBRATION_NO_FIRE:
      compiled = compileCalibrationNoFirePattern(effectiveParams, config, plan);
      break;
    case PATTERN_NONE:
      plan.error = "pattern rejected: unsupported pattern_id";
      return false;
  }

  if (!compiled) return false;
  if (plan.stepCount == 0) {
    plan.error = "pattern rejected: no executable steps";
    return false;
  }
  return true;
}

}  // namespace turret_fleet
}  // namespace battlebang
