from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_hit_target_config_exposes_tunable_gameplay_and_migrated_wiring() -> None:
    config = json.loads(read("src/hit_target/config.json"))
    defaults = config["defaults"]

    assert defaults["hp_phase_count"] == 3
    assert defaults["hits_per_phase"] == 5
    assert defaults["max_hits"] == defaults["hp_phase_count"] * defaults["hits_per_phase"]
    assert defaults["hit_threshold"] == 1400
    assert defaults["hit_rearm_threshold"] == 800
    assert defaults["hit_threshold"] > defaults["hit_rearm_threshold"]
    assert defaults["hit_cooldown_ms"] > 0
    assert defaults["hit_rearm_stable_ms"] > 0
    assert defaults["digital_hit_min_edges"] == 2
    assert defaults["digital_isr_debounce_us"] == 5000
    assert 40 <= defaults["hit_flash_ms"] <= 60
    assert 300 <= defaults["damage_chip_ms"] <= 650
    assert defaults["phase_backfill_gap_leds"] == 1
    assert 1 <= defaults["phase_backfill_scale"] <= 255
    assert 120 <= defaults["hp_hit_pulse_ms"] <= 240
    assert 0 <= defaults["defeat_blackout_ms"] <= 500
    assert 500 <= defaults["defeat_rainbow_ms"] <= 1500
    assert defaults["defeat_rainbow_spins"] == 2
    assert defaults["cooldown_blink_ms"] == 60
    assert defaults["orbit_step_ms"] > 0
    assert defaults["led_pin"] == 18
    assert defaults["num_leds"] == 60
    assert defaults["led_type"] == "WS2812B"
    assert defaults["color_order"] == "GRB"
    assert defaults["led_brightness"] == 80
    assert defaults["led_max_ma"] == 1500
    assert defaults["piezo_do_pin"] == 27
    assert defaults["piezo_ao_pin"] == -1
    assert defaults["reset_button_pin"] == 0
    assert defaults["reset_button_hold_ms"] >= 1000


def test_hit_target_uses_mac_derived_identity_not_numbered_profiles() -> None:
    config_text = read("src/hit_target/config.json")
    source = read("src/hit_target/main.cpp")
    build_config = read("src/hit_target/build_config.h")
    script = read("scripts/hit_target_config.py")

    assert "targets" not in json.loads(config_text)
    assert "target_01" not in config_text
    assert "target_01" not in source
    assert "ESP.getEfuseMac()" in source
    # ESP.getEfuseMac() presents the canonical printed MAC with the first octet
    # in the least-significant byte; keep target_id aligned with esptool logs.
    assert "static_cast<uint8_t>(efuseMac & 0xFF)" in source
    assert "static_cast<uint8_t>((efuseMac >> 40) & 0xFF)" in source
    assert source.index("static_cast<uint8_t>(efuseMac & 0xFF)") < source.index(
        "static_cast<uint8_t>((efuseMac >> 40) & 0xFF)"
    )
    assert 'TARGET_ID_PREFIX = "hit_target"' in build_config
    assert "BATTLEBANG_HIT_TARGET_BUILD_TARGET_ID" not in build_config
    assert "BATTLEBANG_HIT_TARGET_ID" not in script
    assert "id_source=esp32_efuse_mac" in script


def test_hit_target_uses_wall_target_led_macros_without_old_path() -> None:
    source = read("src/hit_target/main.cpp")
    build_config = read("src/hit_target/build_config.h")
    script = read("scripts/hit_target_config.py")

    assert "BATTLEBANG_HIT_TARGET_LED_TYPE" in source
    assert "BATTLEBANG_HIT_TARGET_COLOR_ORDER" in source
    assert "#define BATTLEBANG_HIT_TARGET_LED_TYPE WS2812B" in build_config
    assert "#define BATTLEBANG_HIT_TARGET_COLOR_ORDER GRB" in build_config
    assert "BATTLEBANG_HIT_TARGET_BUILD_LED_TYPE" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_COLOR_ORDER" in script
    assert not (ROOT / "src" / "Wall_Target").exists()


def test_hit_target_ready_green_then_phase_hp_with_red_damage_chip_and_neutral_orbit() -> None:
    source = read("src/hit_target/main.cpp")
    build_config = read("src/hit_target/build_config.h")
    script = read("scripts/hit_target_config.py")

    assert "bool damaged = false;" in source
    assert "static constexpr int HP_PHASE_COUNT = BATTLEBANG_HIT_TARGET_HP_PHASE_COUNT;" in build_config
    assert "static constexpr int HITS_PER_PHASE = BATTLEBANG_HIT_TARGET_HITS_PER_PHASE;" in build_config
    assert "static constexpr int MAX_HITS = HP_PHASE_COUNT * HITS_PER_PHASE;" in build_config
    assert "CRGB phaseColor(int phaseIndex)" in source
    assert "if (phaseIndex == 0) return CRGB(0, 150, 0);" in source
    assert "if (phaseIndex == HP_PHASE_COUNT - 1) return CRGB(190, 0, 0);" in source
    assert "if (HP_PHASE_COUNT == 3) return CRGB(190, 130, 0);" in source
    assert "int phaseIndexForHp(int hp)" in source
    assert "int phaseHitsRemaining(int hp)" in source
    assert "int phaseLitCount(int hp)" in source
    assert "hitsConsumed / HITS_PER_PHASE" in source
    assert "consumedInPhase = hitsConsumed % HITS_PER_PHASE" in source
    assert "phaseLitCount(target.hpRemaining)" in source
    assert "bool phaseRevealPending(uint32_t now)" in source
    assert "void renderPhaseBackfill(int phaseIndex, int lit)" in source
    assert "renderPhaseBackfill(phaseIndex, lit);" in source
    assert "if (phaseRevealPending(now)) {" in source
    assert "renderPhaseBackfill(damageChip.previousPhaseIndex, damageChip.endLed);" in source
    assert "void renderPhaseTransitionReveal(uint32_t now)" in source
    assert "renderPhaseTransitionReveal(now);" in source
    assert "phaseColor(phaseIndex + 1)" in source
    assert "PHASE_BACKFILL_GAP_LEDS" in source
    assert "PHASE_BACKFILL_SCALE" in source
    assert "CRGB applyHpHitPulse(const CRGB& color, uint32_t now)" in source
    assert "timers.hpPulseUntilMs = timers.hitFlashUntilMs + HP_HIT_PULSE_MS;" in source
    assert "color = applyHpHitPulse(color, now);" in source
    assert "renderDamageLayer(now);" in source
    assert "damageChip.capture(previousHp, target.hpRemaining, now);" in source
    assert "phaseTransition = currentHp > 0 && currentPhaseIndex != previousPhaseIndex;" in source
    assert "firstLed = 0;" in source
    assert "endLed = phaseLitCount(previousHp);" in source
    assert '\\"hp_phase\\":%d' in source
    assert '\\"hp_phase_count\\":%d' in source
    assert '\\"hits_per_phase\\":%d' in source
    assert '\\"phase_hits_remaining\\":%d' in source
    assert '\\"phase_transition\\":%s' in source
    assert '\\"phase_backfill_gap_leds\\":%d' in source
    assert '\\"phase_backfill_scale\\":%u' in source
    assert '\\"digital_edges\\":%u' in source
    assert '\\"digital_hit_min_edges\\":%u' in source
    assert '\\"digital_isr_debounce_us\\":%lu' in source
    assert '\\"defeat_blackout_ms\\":%lu' in source
    assert '\\"defeat_rainbow_ms\\":%lu' in source
    assert '\\"defeat_rainbow_spins\\":%u' in source
    assert "int visibleCount(uint32_t now) const" in source
    assert "return constrain(total - expired, 0, total);" in source
    assert "int expiredCount(uint32_t now) const" in source
    assert "int expired = damageChip.expiredCount(now);" in source
    assert "int start = constrain(damageChip.endLed - expired, damageChip.firstLed, damageChip.endLed);" in source
    assert "CRGB nextColor = phaseColor(damageChip.previousPhaseIndex + 1);" in source
    assert "hpLayer[i] = nextColor;" in source
    assert "edgeFade = 255 - static_cast<uint8_t>((edgePhase * 255) / DAMAGE_CHIP_MS);" in source
    assert "for (int offset = 0; offset < visibleCount; offset++)" in source
    assert "damageLayer[i] = CRGB(intensity, scale8(intensity, 70), 0);" in source
    assert "void renderDefeatRainbow(uint32_t now)" in source
    assert "leds[i] = CHSV(hue, 255, scale8(230, fade));" in source
    assert "void delayUntil(uint32_t startMs)" in source
    assert "damageChip.delayUntil(timers.hitFlashUntilMs);" in source
    assert "timers.defeatStartedMs = damageChip.visibleUntilMs + DEFEAT_BLACKOUT_MS;" in source
    assert "timers.defeatUntilMs = timers.defeatStartedMs + DEFEAT_RAINBOW_MS;" in source
    assert source.index("if (damageChip.visible(now) && (int32_t)(timers.defeatStartedMs - now) > 0)") < source.index("renderDefeatRainbow(now);")
    assert "renderDestroySpark" not in source
    assert "if (!damageMode) return;" not in source
    assert "orbitLayer[index] = CRGB(level, level, level);" in source
    assert "BATTLEBANG_HIT_TARGET_COOLDOWN_BLINK_MS 60" in build_config
    assert "BATTLEBANG_HIT_TARGET_HIT_FLASH_MS 50" in build_config
    assert "BATTLEBANG_HIT_TARGET_HIT_THRESHOLD 1400" in build_config
    assert "BATTLEBANG_HIT_TARGET_HIT_REARM_THRESHOLD 800" in build_config
    assert "BATTLEBANG_HIT_TARGET_HIT_COOLDOWN_MS 200" in build_config
    assert "BATTLEBANG_HIT_TARGET_HIT_REARM_STABLE_MS 80" in build_config
    assert "BATTLEBANG_HIT_TARGET_DAMAGE_CHIP_MS 580" in build_config
    assert "BATTLEBANG_HIT_TARGET_ORBIT_STEP_MS 20" in build_config
    assert "BATTLEBANG_HIT_TARGET_ORBIT_TAIL_LEDS 6" in build_config
    assert "BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_GAP_LEDS 1" in build_config
    assert "BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_SCALE 96" in build_config
    assert "BATTLEBANG_HIT_TARGET_HP_HIT_PULSE_MS 180" in build_config
    assert "BATTLEBANG_HIT_TARGET_DEFEAT_BLACKOUT_MS 90" in build_config
    assert "BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_MS 900" in build_config
    assert "BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_SPINS 2" in build_config
    assert "BATTLEBANG_HIT_TARGET_BUILD_HP_PHASE_COUNT" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_HITS_PER_PHASE" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_DIGITAL_HIT_MIN_EDGES" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_DIGITAL_ISR_DEBOUNCE_US" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_HIT_FLASH_MS" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_DAMAGE_CHIP_MS" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_PHASE_BACKFILL_GAP_LEDS" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_PHASE_BACKFILL_SCALE" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_HP_HIT_PULSE_MS" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_DEFEAT_BLACKOUT_MS" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_DEFEAT_RAINBOW_MS" in script
    assert "BATTLEBANG_HIT_TARGET_BUILD_DEFEAT_RAINBOW_SPINS" in script


def test_hit_target_lockout_prevents_rapid_multi_hit_drop() -> None:
    source = read("src/hit_target/main.cpp")
    script = read("scripts/hit_target_config.py")

    assert "bool isLockedOut(uint32_t now)" in source
    assert "void registerHit(uint32_t now, uint16_t peak, const char* source, uint16_t digitalEdges = 0)" in source
    assert "timers.lockoutUntilMs = now + HIT_COOLDOWN_MS;" in source
    assert "sensor.armed = false;" in source
    assert "if (!sensor.armed) return;" in source
    assert "clearPiezoDoFlag();" in source
    assert "digitalEdges >= DIGITAL_HIT_MIN_EDGES" in source
    assert "uint16_t popPiezoDoEdges()" in source
    assert "DIGITAL_ISR_DEBOUNCE_US = BATTLEBANG_HIT_TARGET_DIGITAL_ISR_DEBOUNCE_US" in source or (
        "DIGITAL_ISR_DEBOUNCE_US = BATTLEBANG_HIT_TARGET_DIGITAL_ISR_DEBOUNCE_US" in read(
            "src/hit_target/build_config.h"
        )
    )
    assert "static constexpr uint32_t ISR_DEBOUNCE_US = DIGITAL_ISR_DEBOUNCE_US;" in read(
        "src/hit_target/build_config.h"
    )
    assert 'registerHit(now, peak, "piezo", digitalEdges);' in source
    assert 'registerHit(millis(), readPiezoAnalog(), "serial");' in source
    assert '\\"source\\":\\"%s\\"' in source
    assert '"BATTLEBANG_HIT_TARGET_MAX_HITS"' in script
    assert '"BATTLEBANG_HIT_TARGET_HP_PHASE_COUNT"' in script
    assert '"BATTLEBANG_HIT_TARGET_HITS_PER_PHASE"' in script
    assert '"BATTLEBANG_HIT_TARGET_DIGITAL_HIT_MIN_EDGES"' in script
    assert '"BATTLEBANG_HIT_TARGET_DIGITAL_ISR_DEBOUNCE_US"' in script
    assert '"BATTLEBANG_HIT_TARGET_HIT_COOLDOWN_MS"' in script
    assert '"BATTLEBANG_HIT_TARGET_HIT_REARM_STABLE_MS"' in script


def test_hit_target_supports_esp32_boot_button_initialize() -> None:
    source = read("src/hit_target/main.cpp")
    build_config = read("src/hit_target/build_config.h")
    script = read("scripts/hit_target_config.py")

    assert "RESET_BUTTON_PIN = BATTLEBANG_HIT_TARGET_RESET_BUTTON_PIN" in build_config
    assert "RESET_BUTTON_HOLD_MS = BATTLEBANG_HIT_TARGET_RESET_BUTTON_HOLD_MS" in build_config
    assert "#define BATTLEBANG_HIT_TARGET_RESET_BUTTON_PIN 0" in build_config
    assert "#define BATTLEBANG_HIT_TARGET_RESET_BUTTON_HOLD_MS 1200" in build_config
    assert "pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);" in source
    assert "digitalRead(RESET_BUTTON_PIN) == LOW" in source
    assert 'resetTarget("button");' in source
    assert '"BATTLEBANG_HIT_TARGET_RESET_BUTTON_PIN"' in script
    assert '"BATTLEBANG_HIT_TARGET_RESET_BUTTON_HOLD_MS"' in script


def test_hit_target_platformio_env_is_distinct_from_go2_mounted_firmware() -> None:
    pio = read("platformio.ini")
    readme = read("README.md")

    assert "[env:esp32dev_hit_target]" in pio
    assert "+<hit_target/**>" in pio
    assert "+<go2_nixo/**>" in pio
    assert "src/go2_nixo/" in readme
    assert "src/hit_target/" in readme
    assert "Go2-mounted" in readme
    assert "Generic standalone hit target" in readme


def test_legacy_target_module_experiment_is_removed_after_migration() -> None:
    assert not (ROOT / "src" / "target_module-v2").exists()
