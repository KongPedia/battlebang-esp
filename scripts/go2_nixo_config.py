"""Inject generic integrated Go2+Nixo hardware build macros.

Robot identity and nixo_id are deliberately not read from PlatformIO env names.
Flash a generic `esp32dev_go2_nixo*` firmware, then provision identity into NVS
with `scripts/go2_nixo/provision.py --robot-id go2_XX`.
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
CONFIG_PATH = PROJECT_DIR / "firmware" / "go2_nixo" / "hardware_profile.json"
VARIANTS_DIR = PROJECT_DIR / "firmware" / "go2_nixo" / "variants"
LOG_PREFIX = "[go2_nixo_config]"


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


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


def relay_variant_name() -> str:
    for env_name in ("GO2_NIXO_RELAY_VARIANT", "NIXO_RELAY_VARIANT"):
        env_value = clean_string_value(os.environ.get(env_name, ""))
        if env_value:
            return env_value
    option_value = clean_string_value(project_option("custom_nixo_variant"))
    return option_value or "relay_1ch"


def relay_variant_slug(name: str) -> str:
    if name == "relay_1ch":
        return "1ch"
    if name == "relay_2ch":
        return "2ch"
    return name.replace("relay_", "").replace("_", "-")


def append_variant_identity_defines(defines: list[tuple[str, str]], variant_name: str) -> None:
    slug = relay_variant_slug(variant_name)
    packet_v2 = PIO_ENV.endswith("_packet_v2")
    channel = f"go2-nixo-{slug}{'-packet-v2' if packet_v2 else ''}"
    hardware = f"esp32dev-go2-nixo-relay-{slug}{'-packet-v2' if packet_v2 else ''}-v1"
    defines.extend(
        [
            ("BB_GO2_NIXO_RELAY_VARIANT", c_string(variant_name)),
            ("BB_GO2_NIXO_HARDWARE", c_string(hardware)),
            ("BB_GO2_NIXO_OTA_CHANNEL", c_string(channel)),
            ("BB_GO2_NIXO_STABLE_TAG", c_string(f"{channel}-latest")),
            ("BB_GO2_NIXO_MANIFEST_NAME", c_string(f"{channel}-manifest.json")),
        ]
    )


def load_relay_variant(name: str) -> dict:
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        raise ValueError(f"invalid Go2 Nixo relay variant name: {name!r}")
    config_path = VARIANTS_DIR / name / "config.json"
    if not config_path.exists():
        raise FileNotFoundError(f"Go2 Nixo relay variant config not found: {config_path}")
    with config_path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    data.setdefault("name", name)
    validate_relay_variant(name, data)
    return data


def validate_relay_variant(name: str, data: dict) -> None:
    relay1_pin = int(data["nixo_relay1_pin"])
    relay2_pin = int(data.get("nixo_relay2_pin", -1))
    delay1_ms = int(data.get("nixo_relay_delay1_ms", 800))

    if relay1_pin < 0:
        raise ValueError(f"Go2 Nixo relay variant {name!r} requires nixo_relay1_pin >= 0")
    if relay2_pin >= 0 and relay1_pin == relay2_pin:
        raise ValueError(f"Go2 Nixo relay variant {name!r} relay pins must be different")
    if delay1_ms <= 0:
        raise ValueError(f"Go2 Nixo relay variant {name!r} nixo_relay_delay1_ms must be positive")


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
        "nixo_mqtt_topic_prefix": "BATTLEBANG_BUILD_NIXO_MQTT_TOPIC_PREFIX",
    }
    int_profile_macros = {
        "hit_cooldown_ms": "BATTLEBANG_BUILD_HIT_COOLDOWN_MS",
        "offline_hit_queue_capacity": "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY",
        "offline_hit_queue_flush_interval_ms": "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS",
        "led_pin": "BATTLEBANG_BUILD_LED_PIN",
        "num_leds": "BATTLEBANG_BUILD_NUM_LEDS",
        "led_brightness": "BATTLEBANG_BUILD_LED_BRIGHTNESS",
        "ring_led_pin": "BATTLEBANG_BUILD_RING_LED_PIN",
        "ring_num_leds": "BATTLEBANG_BUILD_RING_NUM_LEDS",
        "ring_led_brightness": "BATTLEBANG_BUILD_RING_LED_BRIGHTNESS",
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
        "nixo_relay1_pin": "BATTLEBANG_BUILD_NIXO_RELAY1_PIN",
        "nixo_relay2_pin": "BATTLEBANG_BUILD_NIXO_RELAY2_PIN",
        "nixo_relay_on_level": "BATTLEBANG_BUILD_NIXO_RELAY_ON_LEVEL",
        "nixo_relay_off_level": "BATTLEBANG_BUILD_NIXO_RELAY_OFF_LEVEL",
        "nixo_relay_delay1_ms": "BATTLEBANG_BUILD_NIXO_RELAY_DELAY1_MS",
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
    # Bench-only fallback overrides. Runtime identity/network/OTA should be
    # provisioned into NVS for fleet devices.
    string_env_macros = [
        (("ESP_WIFI_SSID", "BATTLEBANG_WIFI_SSID"), "BATTLEBANG_BUILD_WIFI_SSID"),
        (("ESP_WIFI_PASSWORD", "BATTLEBANG_WIFI_PASSWORD"), "BATTLEBANG_BUILD_WIFI_PASSWORD"),
        (("ESP_MQTT_HOST", "BATTLEBANG_MQTT_HOST"), "BATTLEBANG_BUILD_MQTT_HOST"),
        (("ESP_MQTT_TOPIC_PREFIX", "BATTLEBANG_MQTT_TOPIC_PREFIX"), "BATTLEBANG_BUILD_MQTT_TOPIC_PREFIX"),
        (("NIXO_MQTT_TOPIC_PREFIX", "BATTLEBANG_NIXO_MQTT_TOPIC_PREFIX"), "BATTLEBANG_BUILD_NIXO_MQTT_TOPIC_PREFIX"),
    ]
    int_env_macros = [
        (("ESP_MQTT_PORT", "BATTLEBANG_MQTT_PORT"), "BATTLEBANG_BUILD_MQTT_PORT"),
        (("BATTLEBANG_HIT_COOLDOWN_MS",), "BATTLEBANG_BUILD_HIT_COOLDOWN_MS"),
        (("BATTLEBANG_OFFLINE_HIT_QUEUE_CAPACITY",), "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_CAPACITY"),
        (("BATTLEBANG_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS",), "BATTLEBANG_BUILD_OFFLINE_HIT_QUEUE_FLUSH_INTERVAL_MS"),
        (("BATTLEBANG_LED_PIN",), "BATTLEBANG_BUILD_LED_PIN"),
        (("BATTLEBANG_NUM_LEDS",), "BATTLEBANG_BUILD_NUM_LEDS"),
        (("BATTLEBANG_LED_BRIGHTNESS",), "BATTLEBANG_BUILD_LED_BRIGHTNESS"),
        (("BATTLEBANG_RING_LED_PIN",), "BATTLEBANG_BUILD_RING_LED_PIN"),
        (("BATTLEBANG_RING_NUM_LEDS",), "BATTLEBANG_BUILD_RING_NUM_LEDS"),
        (("BATTLEBANG_RING_LED_BRIGHTNESS",), "BATTLEBANG_BUILD_RING_LED_BRIGHTNESS"),
        (("BATTLEBANG_T1_DO_PIN",), "BATTLEBANG_BUILD_T1_DO_PIN"),
        (("BATTLEBANG_T2_DO_PIN",), "BATTLEBANG_BUILD_T2_DO_PIN"),
        (("BATTLEBANG_PIEZO_AO_PIN", "BATTLEBANG_LEFT_PIEZO_AO_PIN"), "BATTLEBANG_BUILD_LEFT_PIEZO_AO_PIN"),
        (("BATTLEBANG_RIGHT_PIEZO_AO_PIN",), "BATTLEBANG_BUILD_RIGHT_PIEZO_AO_PIN"),
        (("BATTLEBANG_FRONT_PIEZO_AO_PIN",), "BATTLEBANG_BUILD_FRONT_PIEZO_AO_PIN"),
        (("BATTLEBANG_PIEZO_AO_THRESHOLD_RAW",), "BATTLEBANG_BUILD_PIEZO_AO_THRESHOLD_RAW"),
        (("BATTLEBANG_PIEZO_AO_REARM_RAW",), "BATTLEBANG_BUILD_PIEZO_AO_REARM_RAW"),
        (("BATTLEBANG_PIEZO_AO_CAPTURE_WINDOW_MS",), "BATTLEBANG_BUILD_PIEZO_AO_CAPTURE_WINDOW_MS"),
        (("BATTLEBANG_PIEZO_AO_DEBUG_PERIOD_MS",), "BATTLEBANG_BUILD_PIEZO_AO_DEBUG_PERIOD_MS"),
        (("NIXO_RELAY1_PIN", "BATTLEBANG_NIXO_RELAY1_PIN"), "BATTLEBANG_BUILD_NIXO_RELAY1_PIN"),
        (("NIXO_RELAY2_PIN", "BATTLEBANG_NIXO_RELAY2_PIN"), "BATTLEBANG_BUILD_NIXO_RELAY2_PIN"),
        (("NIXO_RELAY_ON_LEVEL", "BATTLEBANG_NIXO_RELAY_ON_LEVEL"), "BATTLEBANG_BUILD_NIXO_RELAY_ON_LEVEL"),
        (("NIXO_RELAY_OFF_LEVEL", "BATTLEBANG_NIXO_RELAY_OFF_LEVEL"), "BATTLEBANG_BUILD_NIXO_RELAY_OFF_LEVEL"),
        (("NIXO_RELAY_DELAY1_MS", "BATTLEBANG_NIXO_RELAY_DELAY1_MS"), "BATTLEBANG_BUILD_NIXO_RELAY_DELAY1_MS"),
        (("NIXO_FIRE_DEFAULT_DURATION_MS", "BATTLEBANG_NIXO_FIRE_DEFAULT_DURATION_MS"), "BATTLEBANG_BUILD_NIXO_FIRE_DEFAULT_DURATION_MS"),
        (("NIXO_FIRE_MIN_DURATION_MS", "BATTLEBANG_NIXO_FIRE_MIN_DURATION_MS"), "BATTLEBANG_BUILD_NIXO_FIRE_MIN_DURATION_MS"),
        (("NIXO_FIRE_MAX_DURATION_MS", "BATTLEBANG_NIXO_FIRE_MAX_DURATION_MS"), "BATTLEBANG_BUILD_NIXO_FIRE_MAX_DURATION_MS"),
        (("NIXO_FIRE_COOLDOWN_MS", "BATTLEBANG_NIXO_FIRE_COOLDOWN_MS"), "BATTLEBANG_BUILD_NIXO_FIRE_COOLDOWN_MS"),
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
    print(f"{LOG_PREFIX} missing hardware profile: {CONFIG_PATH}")
    Exit(1)

with CONFIG_PATH.open("r", encoding="utf-8") as f:
    config = json.load(f)

variant_name = relay_variant_name()
relay_variant = load_relay_variant(variant_name)
profile = deep_merge(config.get("defaults", {}), relay_variant)
packet_v2 = PIO_ENV.endswith("_packet_v2")
identity_suffix = "-packet-v2" if packet_v2 else ""
defines: list[tuple[str, str]] = []
append_variant_identity_defines(defines, variant_name)
append_profile_defines(defines, profile)
append_env_defines(defines)

env.Append(CPPDEFINES=defines)
print(
    f"{LOG_PREFIX} "
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
    f"ring_led_pin={profile.get('ring_led_pin', 'default')} "
    f"ring_num_leds={profile.get('ring_num_leds', 'default')} "
    f"mqtt_topic_prefix={profile.get('mqtt_topic_prefix', 'default')} "
    f"nixo_id=NVS-derived "
    f"hardware=esp32dev-go2-nixo-relay-{relay_variant_slug(variant_name)}{identity_suffix}-v1 "
    f"ota_channel=go2-nixo-{relay_variant_slug(variant_name)}{identity_suffix} "
    f"nixo_variant={variant_name} "
    f"nixo_relay1={profile.get('nixo_relay1_pin', 'default')} "
    f"nixo_relay2={profile.get('nixo_relay2_pin', 'default')} "
    f"nixo_relay_on={profile.get('nixo_relay_on_level', 'default')} "
    f"nixo_relay_off={profile.get('nixo_relay_off_level', 'default')} "
    f"nixo_delay1_ms={profile.get('nixo_relay_delay1_ms', 'default')}"
)
