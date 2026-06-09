"""Inject per-Go2 integrated Go2+Nixo ESP PlatformIO build macros from src/go2_nixo/robots.json.

Usage:
  pio run -e esp32dev_go2_nixo_go2_05
  GO2_ID=go2_05 pio run -e esp32dev_go2_nixo
  ESP_MQTT_HOST=COMMAND_CENTER_IP_OR_DNS pio run -e esp32dev_go2_nixo_go2_05
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
CONFIG_PATH = PROJECT_DIR / "src" / "go2_nixo" / "robots.json"


def project_option(name: str) -> str:
    try:
        value = env.GetProjectOption(name)
    except Exception:
        return ""
    return "" if value is None else str(value).strip()


def clean_string_value(value: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        return text[1:-1]
    return text


def detect_robot_id() -> str:
    for env_name in ("GO2_ID", "ROBOT_ID", "ESP_ROBOT_ID", "BATTLEBANG_ROBOT_ID"):
        env_override = os.environ.get(env_name, "").strip()
        if env_override:
            return clean_string_value(env_override)

    option_value = project_option("custom_robot_id")
    if option_value:
        return option_value

    match = re.search(r"(go2_\d+)$", PIO_ENV)
    if match:
        return match.group(1)

    return "go2_05"


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
        "nixo_id": "BATTLEBANG_BUILD_NIXO_ID",
        "nixo_mqtt_topic_prefix": "BATTLEBANG_BUILD_NIXO_MQTT_TOPIC_PREFIX",
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
        "nixo_relay1_pin": "BATTLEBANG_BUILD_NIXO_RELAY1_PIN",
        "nixo_relay2_pin": "BATTLEBANG_BUILD_NIXO_RELAY2_PIN",
        "nixo_relay_on_level": "BATTLEBANG_BUILD_NIXO_RELAY_ON_LEVEL",
        "nixo_relay_off_level": "BATTLEBANG_BUILD_NIXO_RELAY_OFF_LEVEL",
        "nixo_fire_default_duration_ms": "BATTLEBANG_BUILD_NIXO_FIRE_DEFAULT_DURATION_MS",
        "nixo_fire_min_duration_ms": "BATTLEBANG_BUILD_NIXO_FIRE_MIN_DURATION_MS",
        "nixo_fire_max_duration_ms": "BATTLEBANG_BUILD_NIXO_FIRE_MAX_DURATION_MS",
        "nixo_fire_cooldown_ms": "BATTLEBANG_BUILD_NIXO_FIRE_COOLDOWN_MS",
    }

    for json_key, macro_name in string_profile_macros.items():
        if json_key in profile:
            defines.append((macro_name, c_string(str(profile[json_key]))))

    for json_key, macro_name in int_profile_macros.items():
        if json_key in profile:
            defines.append((macro_name, str(int(profile[json_key]))))


def append_env_defines(defines: list[tuple[str, str]]) -> None:
    # Operator-facing network env names use ESP_* to match local_secrets.h.
    # Keep BATTLEBANG_* aliases for older shells/scripts.
    string_env_macros = [
        (("ESP_WIFI_SSID", "BATTLEBANG_WIFI_SSID"), "BATTLEBANG_BUILD_WIFI_SSID"),
        (
            ("ESP_WIFI_PASSWORD", "BATTLEBANG_WIFI_PASSWORD"),
            "BATTLEBANG_BUILD_WIFI_PASSWORD",
        ),
        (("ESP_MQTT_HOST", "BATTLEBANG_MQTT_HOST"), "BATTLEBANG_BUILD_MQTT_HOST"),
        (
            ("ESP_MQTT_TOPIC_PREFIX", "BATTLEBANG_MQTT_TOPIC_PREFIX"),
            "BATTLEBANG_BUILD_MQTT_TOPIC_PREFIX",
        ),
        (("NIXO_ID", "BATTLEBANG_NIXO_ID"), "BATTLEBANG_BUILD_NIXO_ID"),
        (
            ("NIXO_MQTT_TOPIC_PREFIX", "BATTLEBANG_NIXO_MQTT_TOPIC_PREFIX"),
            "BATTLEBANG_BUILD_NIXO_MQTT_TOPIC_PREFIX",
        ),
    ]
    int_env_macros = [
        (("ESP_MQTT_PORT", "BATTLEBANG_MQTT_PORT"), "BATTLEBANG_BUILD_MQTT_PORT"),
        (("BATTLEBANG_HIT_COOLDOWN_MS",), "BATTLEBANG_BUILD_HIT_COOLDOWN_MS"),
        (("BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY",), "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY"),
        (
            ("BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS",),
            "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS",
        ),
        (("BATTLEBANG_LED_PIN",), "BATTLEBANG_BUILD_LED_PIN"),
        (("BATTLEBANG_NUM_LEDS",), "BATTLEBANG_BUILD_NUM_LEDS"),
        (("BATTLEBANG_LED_BRIGHTNESS",), "BATTLEBANG_BUILD_LED_BRIGHTNESS"),
        (("BATTLEBANG_T1_DO_PIN",), "BATTLEBANG_BUILD_T1_DO_PIN"),
        (("BATTLEBANG_T2_DO_PIN",), "BATTLEBANG_BUILD_T2_DO_PIN"),
        (("NIXO_RELAY1_PIN", "BATTLEBANG_NIXO_RELAY1_PIN"), "BATTLEBANG_BUILD_NIXO_RELAY1_PIN"),
        (("NIXO_RELAY2_PIN", "BATTLEBANG_NIXO_RELAY2_PIN"), "BATTLEBANG_BUILD_NIXO_RELAY2_PIN"),
        (
            ("NIXO_RELAY_ON_LEVEL", "BATTLEBANG_NIXO_RELAY_ON_LEVEL"),
            "BATTLEBANG_BUILD_NIXO_RELAY_ON_LEVEL",
        ),
        (
            ("NIXO_RELAY_OFF_LEVEL", "BATTLEBANG_NIXO_RELAY_OFF_LEVEL"),
            "BATTLEBANG_BUILD_NIXO_RELAY_OFF_LEVEL",
        ),
        (
            ("NIXO_FIRE_DEFAULT_DURATION_MS", "BATTLEBANG_NIXO_FIRE_DEFAULT_DURATION_MS"),
            "BATTLEBANG_BUILD_NIXO_FIRE_DEFAULT_DURATION_MS",
        ),
        (
            ("NIXO_FIRE_MIN_DURATION_MS", "BATTLEBANG_NIXO_FIRE_MIN_DURATION_MS"),
            "BATTLEBANG_BUILD_NIXO_FIRE_MIN_DURATION_MS",
        ),
        (
            ("NIXO_FIRE_MAX_DURATION_MS", "BATTLEBANG_NIXO_FIRE_MAX_DURATION_MS"),
            "BATTLEBANG_BUILD_NIXO_FIRE_MAX_DURATION_MS",
        ),
        (
            ("NIXO_FIRE_COOLDOWN_MS", "BATTLEBANG_NIXO_FIRE_COOLDOWN_MS"),
            "BATTLEBANG_BUILD_NIXO_FIRE_COOLDOWN_MS",
        ),
    ]

    for env_names, macro_name in string_env_macros:
        value = next(
            (
                os.environ[name]
                for name in env_names
                if os.environ.get(name) is not None
            ),
            None,
        )
        if value is not None:
            defines.append((macro_name, c_string(value)))

    for env_names, macro_name in int_env_macros:
        value = next(
            (
                os.environ[name]
                for name in env_names
                if os.environ.get(name) is not None
            ),
            None,
        )
        if value is not None:
            defines.append((macro_name, str(int(value))))


LOG_PREFIX = "[go2_nixo_config]"


if not CONFIG_PATH.exists():
    print(f"{LOG_PREFIX} missing config: {CONFIG_PATH}")
    Exit(1)

with CONFIG_PATH.open("r", encoding="utf-8") as f:
    config = json.load(f)

robot_id = detect_robot_id()
robots = config.get("robots", {})
entry = robots.get(robot_id)
if entry is None:
    print(f"{LOG_PREFIX} unknown robot_id={robot_id!r}; edit {CONFIG_PATH}")
    print(f"{LOG_PREFIX} known: {', '.join(sorted(robots))}")
    Exit(1)

if not entry.get("configured", False):
    print(f"{LOG_PREFIX} {robot_id} is intentionally blank.")
    print(f"{LOG_PREFIX} Set configured=true in {CONFIG_PATH} before building it.")
    Exit(1)

profile = deep_merge(config.get("defaults", {}), entry)
defines = [("BATTLEBANG_BUILD_ROBOT_ID", c_string(robot_id))]
append_profile_defines(defines, profile)
append_env_defines(defines)

env.Append(CPPDEFINES=defines)
print(
    f"{LOG_PREFIX} "
    f"{PIO_ENV}: robot_id={robot_id} "
        f"hit_cooldown_ms={profile.get('hit_cooldown_ms', 'default')} "
        f"offline_queue={profile.get('offline_hit_queue_capacity', 'default')} "
        f"mqtt_topic_prefix={profile.get('mqtt_topic_prefix', 'default')} "
        f"nixo_id={profile.get('nixo_id', 'derived')}"
    )
