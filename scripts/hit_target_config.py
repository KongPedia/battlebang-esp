"""Inject generic Hit Target PlatformIO build macros.

Device identity is intentionally not a build-time profile. The firmware derives
its target_id at boot from the ESP32 eFuse MAC address.

Usage:
  pio run -e esp32dev_hit_target
  BATTLEBANG_HIT_TARGET_HIT_THRESHOLD=2600 pio run -e esp32dev_hit_target
  BATTLEBANG_HIT_TARGET_HP_PHASE_COUNT=3 BATTLEBANG_HIT_TARGET_HITS_PER_PHASE=5 BATTLEBANG_HIT_TARGET_MAX_HITS=15 pio run -e esp32dev_hit_target
  BATTLEBANG_HIT_TARGET_LED_TYPE=WS2812B BATTLEBANG_HIT_TARGET_COLOR_ORDER=GRB pio run -e esp32dev_hit_target
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path

from SCons.Script import Exit, Import  # type: ignore

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
PIO_ENV = env.subst("$PIOENV")
CONFIG_PATH = PROJECT_DIR / "src" / "hit_target" / "config.json"
TOKEN_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

INT_OPTIONS: tuple[tuple[str, str, str], ...] = (
    ("max_hits", "BATTLEBANG_HIT_TARGET_MAX_HITS", "BATTLEBANG_HIT_TARGET_BUILD_MAX_HITS"),
    (
        "hp_phase_count",
        "BATTLEBANG_HIT_TARGET_HP_PHASE_COUNT",
        "BATTLEBANG_HIT_TARGET_BUILD_HP_PHASE_COUNT",
    ),
    (
        "hits_per_phase",
        "BATTLEBANG_HIT_TARGET_HITS_PER_PHASE",
        "BATTLEBANG_HIT_TARGET_BUILD_HITS_PER_PHASE",
    ),
    ("hit_threshold", "BATTLEBANG_HIT_TARGET_HIT_THRESHOLD", "BATTLEBANG_HIT_TARGET_BUILD_HIT_THRESHOLD"),
    (
        "hit_rearm_threshold",
        "BATTLEBANG_HIT_TARGET_HIT_REARM_THRESHOLD",
        "BATTLEBANG_HIT_TARGET_BUILD_HIT_REARM_THRESHOLD",
    ),
    (
        "hit_cooldown_ms",
        "BATTLEBANG_HIT_TARGET_HIT_COOLDOWN_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_HIT_COOLDOWN_MS",
    ),
    (
        "hit_rearm_stable_ms",
        "BATTLEBANG_HIT_TARGET_HIT_REARM_STABLE_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_HIT_REARM_STABLE_MS",
    ),
    (
        "hit_rearm_check_ms",
        "BATTLEBANG_HIT_TARGET_HIT_REARM_CHECK_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_HIT_REARM_CHECK_MS",
    ),
    (
        "digital_hit_min_edges",
        "BATTLEBANG_HIT_TARGET_DIGITAL_HIT_MIN_EDGES",
        "BATTLEBANG_HIT_TARGET_BUILD_DIGITAL_HIT_MIN_EDGES",
    ),
    (
        "digital_isr_debounce_us",
        "BATTLEBANG_HIT_TARGET_DIGITAL_ISR_DEBOUNCE_US",
        "BATTLEBANG_HIT_TARGET_BUILD_DIGITAL_ISR_DEBOUNCE_US",
    ),
    (
        "capture_window_ms",
        "BATTLEBANG_HIT_TARGET_CAPTURE_WINDOW_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_CAPTURE_WINDOW_MS",
    ),
    ("hit_flash_ms", "BATTLEBANG_HIT_TARGET_HIT_FLASH_MS", "BATTLEBANG_HIT_TARGET_BUILD_HIT_FLASH_MS"),
    (
        "damage_chip_ms",
        "BATTLEBANG_HIT_TARGET_DAMAGE_CHIP_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_DAMAGE_CHIP_MS",
    ),
    (
        "phase_backfill_gap_leds",
        "BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_GAP_LEDS",
        "BATTLEBANG_HIT_TARGET_BUILD_PHASE_BACKFILL_GAP_LEDS",
    ),
    (
        "phase_backfill_scale",
        "BATTLEBANG_HIT_TARGET_PHASE_BACKFILL_SCALE",
        "BATTLEBANG_HIT_TARGET_BUILD_PHASE_BACKFILL_SCALE",
    ),
    (
        "hp_hit_pulse_ms",
        "BATTLEBANG_HIT_TARGET_HP_HIT_PULSE_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_HP_HIT_PULSE_MS",
    ),
    (
        "defeat_rainbow_ms",
        "BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_DEFEAT_RAINBOW_MS",
    ),
    (
        "defeat_blackout_ms",
        "BATTLEBANG_HIT_TARGET_DEFEAT_BLACKOUT_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_DEFEAT_BLACKOUT_MS",
    ),
    (
        "defeat_rainbow_spins",
        "BATTLEBANG_HIT_TARGET_DEFEAT_RAINBOW_SPINS",
        "BATTLEBANG_HIT_TARGET_BUILD_DEFEAT_RAINBOW_SPINS",
    ),
    ("led_pin", "BATTLEBANG_HIT_TARGET_LED_PIN", "BATTLEBANG_HIT_TARGET_BUILD_LED_PIN"),
    ("num_leds", "BATTLEBANG_HIT_TARGET_NUM_LEDS", "BATTLEBANG_HIT_TARGET_BUILD_NUM_LEDS"),
    (
        "led_brightness",
        "BATTLEBANG_HIT_TARGET_LED_BRIGHTNESS",
        "BATTLEBANG_HIT_TARGET_BUILD_LED_BRIGHTNESS",
    ),
    ("led_max_ma", "BATTLEBANG_HIT_TARGET_LED_MAX_MA", "BATTLEBANG_HIT_TARGET_BUILD_LED_MAX_MA"),
    ("piezo_do_pin", "BATTLEBANG_HIT_TARGET_PIEZO_DO_PIN", "BATTLEBANG_HIT_TARGET_BUILD_PIEZO_DO_PIN"),
    ("piezo_ao_pin", "BATTLEBANG_HIT_TARGET_PIEZO_AO_PIN", "BATTLEBANG_HIT_TARGET_BUILD_PIEZO_AO_PIN"),
    (
        "reset_button_pin",
        "BATTLEBANG_HIT_TARGET_RESET_BUTTON_PIN",
        "BATTLEBANG_HIT_TARGET_BUILD_RESET_BUTTON_PIN",
    ),
    (
        "reset_button_hold_ms",
        "BATTLEBANG_HIT_TARGET_RESET_BUTTON_HOLD_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_RESET_BUTTON_HOLD_MS",
    ),
    (
        "cooldown_blink_ms",
        "BATTLEBANG_HIT_TARGET_COOLDOWN_BLINK_MS",
        "BATTLEBANG_HIT_TARGET_BUILD_COOLDOWN_BLINK_MS",
    ),
    ("orbit_step_ms", "BATTLEBANG_HIT_TARGET_ORBIT_STEP_MS", "BATTLEBANG_HIT_TARGET_BUILD_ORBIT_STEP_MS"),
    ("orbit_tail_leds", "BATTLEBANG_HIT_TARGET_ORBIT_TAIL_LEDS", "BATTLEBANG_HIT_TARGET_BUILD_ORBIT_TAIL_LEDS"),
)

TOKEN_OPTIONS: tuple[tuple[str, str, str], ...] = (
    ("led_type", "BATTLEBANG_HIT_TARGET_LED_TYPE", "BATTLEBANG_HIT_TARGET_BUILD_LED_TYPE"),
    ("color_order", "BATTLEBANG_HIT_TARGET_COLOR_ORDER", "BATTLEBANG_HIT_TARGET_BUILD_COLOR_ORDER"),
)


def token_value(value: object, name: str) -> str:
    token = str(value)
    if not TOKEN_RE.fullmatch(token):
        print(f"[hit_target_config] invalid token for {name}: {token!r}")
        Exit(1)
    return token


def append_default_defines(defines: list[tuple[str, str]], defaults: dict) -> None:
    for json_key, _env_name, macro_name in INT_OPTIONS:
        if json_key in defaults:
            defines.append((macro_name, str(int(defaults[json_key]))))

    for json_key, _env_name, macro_name in TOKEN_OPTIONS:
        if json_key in defaults:
            defines.append((macro_name, token_value(defaults[json_key], json_key)))


def append_env_defines(defines: list[tuple[str, str]]) -> None:
    for _json_key, env_name, macro_name in INT_OPTIONS:
        value = os.environ.get(env_name)
        if value is not None:
            defines.append((macro_name, str(int(value))))

    for _json_key, env_name, macro_name in TOKEN_OPTIONS:
        value = os.environ.get(env_name)
        if value is not None:
            defines.append((macro_name, token_value(value, env_name)))


if not CONFIG_PATH.exists():
    print(f"[hit_target_config] missing config: {CONFIG_PATH}")
    Exit(1)

with CONFIG_PATH.open("r", encoding="utf-8") as f:
    config = json.load(f)

defaults = config.get("defaults", {})
defines: list[tuple[str, str]] = []
append_default_defines(defines, defaults)
append_env_defines(defines)

env.Append(CPPDEFINES=defines)
print(
    "[hit_target_config] "
    f"{PIO_ENV}: id_source=esp32_efuse_mac "
    f"max_hits={defaults.get('max_hits')} "
    f"hp_phase_count={defaults.get('hp_phase_count')} "
    f"hits_per_phase={defaults.get('hits_per_phase')} "
    f"threshold={defaults.get('hit_threshold')} "
    f"cooldown_ms={defaults.get('hit_cooldown_ms')} "
    f"hit_flash_ms={defaults.get('hit_flash_ms')} "
    f"damage_chip_ms={defaults.get('damage_chip_ms')} "
    f"cooldown_blink_ms={defaults.get('cooldown_blink_ms')} "
    f"phase_backfill_gap_leds={defaults.get('phase_backfill_gap_leds')} "
    f"phase_backfill_scale={defaults.get('phase_backfill_scale')} "
    f"hp_hit_pulse_ms={defaults.get('hp_hit_pulse_ms')} "
    f"defeat_blackout_ms={defaults.get('defeat_blackout_ms')} "
    f"defeat_rainbow_ms={defaults.get('defeat_rainbow_ms')} "
    f"defeat_rainbow_spins={defaults.get('defeat_rainbow_spins')} "
    f"rearm_stable_ms={defaults.get('hit_rearm_stable_ms')} "
    f"digital_hit_min_edges={defaults.get('digital_hit_min_edges')} "
    f"digital_isr_debounce_us={defaults.get('digital_isr_debounce_us')} "
    f"orbit_step_ms={defaults.get('orbit_step_ms')} "
    f"led_pin={defaults.get('led_pin')} "
    f"num_leds={defaults.get('num_leds')} "
    f"led_type={defaults.get('led_type')} "
    f"color_order={defaults.get('color_order')} "
    f"piezo_do={defaults.get('piezo_do_pin')} "
    f"piezo_ao={defaults.get('piezo_ao_pin')} "
    f"reset_button={defaults.get('reset_button_pin')} "
    f"reset_hold_ms={defaults.get('reset_button_hold_ms')}"
)
