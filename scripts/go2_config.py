"""Inject generic Go2 hit ESP hardware build macros from firmware/go2/hardware_profile.json.

Robot identity is deliberately not read from PlatformIO env names. Flash the generic
`esp32dev_go2` firmware, then provision `robot_id`/device identity into NVS with
`scripts/go2/provision.py --robot-id go2_XX`.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

from SCons.Script import Exit, Import  # type: ignore

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
PIO_ENV = env.subst("$PIOENV")
CONFIG_PATH = PROJECT_DIR / "firmware" / "go2" / "hardware_profile.json"


def clean_string_value(value: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        return text[1:-1]
    return text


def c_string(value: str) -> str:
    # PlatformIO/SCons CPPDEFINES passes values directly to -DNAME=value.
    # Use escaped quotes so the preprocessor sees a real C string literal.
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


def deep_merge(base: dict, override: dict) -> dict:
    merged = dict(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = deep_merge(merged[key], value)
        else:
            merged[key] = value
    return merged


def append_profile_defines(defines: list[tuple[str, str]], profile: dict) -> None:
    string_profile_macros = {
        "mqtt_topic_prefix": "BATTLEBANG_BUILD_MQTT_TOPIC_PREFIX",
    }
    int_profile_macros = {
        "hit_cooldown_ms": "BATTLEBANG_BUILD_HIT_COOLDOWN_MS",
        "offline_hit_queue_capacity": "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY",
        "offline_hit_queue_flush_interval_ms": "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS",
        "led_pin": "BATTLEBANG_BUILD_LED_PIN",
        "num_leds": "BATTLEBANG_BUILD_NUM_LEDS",
        "led_brightness": "BATTLEBANG_BUILD_LED_BRIGHTNESS",
        "t1_do_pin": "BATTLEBANG_BUILD_T1_DO_PIN",
        "t2_do_pin": "BATTLEBANG_BUILD_T2_DO_PIN",
        "piezo_ao_pin": "BATTLEBANG_BUILD_LEFT_PIEZO_AO_PIN",
        "piezo_left_pin": "BATTLEBANG_BUILD_LEFT_PIEZO_AO_PIN",
        "piezo_right_pin": "BATTLEBANG_BUILD_RIGHT_PIEZO_AO_PIN",
        "piezo_front_pin": "BATTLEBANG_BUILD_FRONT_PIEZO_AO_PIN",
        "piezo_ao_threshold_raw": "BATTLEBANG_BUILD_PIEZO_AO_THRESHOLD_RAW",
        "piezo_ao_rearm_raw": "BATTLEBANG_BUILD_PIEZO_AO_REARM_RAW",
        "piezo_ao_capture_window_ms": "BATTLEBANG_BUILD_PIEZO_AO_CAPTURE_WINDOW_MS",
        "piezo_ao_debug_period_ms": "BATTLEBANG_BUILD_PIEZO_AO_DEBUG_PERIOD_MS",
    }

    for json_key, macro_name in string_profile_macros.items():
        if json_key in profile:
            defines.append((macro_name, c_string(str(profile[json_key]))))

    for json_key, macro_name in int_profile_macros.items():
        if json_key in profile:
            defines.append((macro_name, str(int(profile[json_key]))))


def append_env_defines(defines: list[tuple[str, str]]) -> None:
    # Bench-only fallback overrides. Runtime identity/network/OTA should be
    # provisioned into NVS for fleet devices.
    string_env_macros = [
        (("ESP_WIFI_SSID", "BATTLEBANG_WIFI_SSID"), "BATTLEBANG_BUILD_WIFI_SSID"),
        (("ESP_WIFI_PASSWORD", "BATTLEBANG_WIFI_PASSWORD"), "BATTLEBANG_BUILD_WIFI_PASSWORD"),
        (("ESP_MQTT_HOST", "BATTLEBANG_MQTT_HOST"), "BATTLEBANG_BUILD_MQTT_HOST"),
        (("ESP_MQTT_TOPIC_PREFIX", "BATTLEBANG_MQTT_TOPIC_PREFIX"), "BATTLEBANG_BUILD_MQTT_TOPIC_PREFIX"),
    ]
    int_env_macros = [
        (("ESP_MQTT_PORT", "BATTLEBANG_MQTT_PORT"), "BATTLEBANG_BUILD_MQTT_PORT"),
        (("BATTLEBANG_HIT_COOLDOWN_MS",), "BATTLEBANG_BUILD_HIT_COOLDOWN_MS"),
        (("BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY",), "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY"),
        (("BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS",), "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS"),
        (("BATTLEBANG_LED_PIN",), "BATTLEBANG_BUILD_LED_PIN"),
        (("BATTLEBANG_NUM_LEDS",), "BATTLEBANG_BUILD_NUM_LEDS"),
        (("BATTLEBANG_LED_BRIGHTNESS",), "BATTLEBANG_BUILD_LED_BRIGHTNESS"),
        (("BATTLEBANG_T1_DO_PIN",), "BATTLEBANG_BUILD_T1_DO_PIN"),
        (("BATTLEBANG_T2_DO_PIN",), "BATTLEBANG_BUILD_T2_DO_PIN"),
        (("BATTLEBANG_PIEZO_AO_PIN", "BATTLEBANG_LEFT_PIEZO_AO_PIN"), "BATTLEBANG_BUILD_LEFT_PIEZO_AO_PIN"),
        (("BATTLEBANG_RIGHT_PIEZO_AO_PIN",), "BATTLEBANG_BUILD_RIGHT_PIEZO_AO_PIN"),
        (("BATTLEBANG_FRONT_PIEZO_AO_PIN",), "BATTLEBANG_BUILD_FRONT_PIEZO_AO_PIN"),
        (("BATTLEBANG_PIEZO_AO_THRESHOLD_RAW",), "BATTLEBANG_BUILD_PIEZO_AO_THRESHOLD_RAW"),
        (("BATTLEBANG_PIEZO_AO_REARM_RAW",), "BATTLEBANG_BUILD_PIEZO_AO_REARM_RAW"),
        (("BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS",), "BATTLEBANG_BUILD_PIEZO_AO_CAPTURE_WINDOW_MS"),
        (("BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS",), "BATTLEBANG_BUILD_PIEZO_AO_DEBUG_PERIOD_MS"),
    ]

    for env_names, macro_name in string_env_macros:
        value = next((os.environ[name] for name in env_names if os.environ.get(name) is not None), None)
        if value is not None:
            defines.append((macro_name, c_string(value)))

    for env_names, macro_name in int_env_macros:
        value = next((os.environ[name] for name in env_names if os.environ.get(name) is not None), None)
        if value is not None:
            defines.append((macro_name, str(int(value))))


if not CONFIG_PATH.exists():
    print(f"[go2_config] missing hardware profile: {CONFIG_PATH}")
    Exit(1)

with CONFIG_PATH.open("r", encoding="utf-8") as f:
    config = json.load(f)

profile = deep_merge({}, config.get("defaults", {}))
defines: list[tuple[str, str]] = []
append_profile_defines(defines, profile)
append_env_defines(defines)

env.Append(CPPDEFINES=defines)
print(
    "[go2_config] "
    f"{PIO_ENV}: generic_runtime_identity=NVS/MAC "
    f"hardware_profile={CONFIG_PATH.relative_to(PROJECT_DIR)} "
    f"hit_cooldown_ms={profile.get('hit_cooldown_ms', 'default')} "
    f"piezo_left={profile.get('piezo_left_pin', profile.get('piezo_ao_pin', 'default'))} "
    f"piezo_right={profile.get('piezo_right_pin', 'default')} "
    f"piezo_front={profile.get('piezo_front_pin', 'default')} "
    f"ao_threshold={profile.get('piezo_ao_threshold_raw', 'default')} "
    f"ao_rearm={profile.get('piezo_ao_rearm_raw', 'default')} "
    f"offline_queue={profile.get('offline_hit_queue_capacity', 'default')} "
    f"led_pin={profile.get('led_pin', 'default')} "
    f"num_leds={profile.get('num_leds', 'default')} "
    f"led_brightness={profile.get('led_brightness', 'default')} "
    f"mqtt_topic_prefix={profile.get('mqtt_topic_prefix', 'default')}"
)
