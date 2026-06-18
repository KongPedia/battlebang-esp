from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARES = (
    (
        "go2",
        ROOT / "src/go2/display/bar_display.cpp",
        ROOT / "src/go2/display/ring_display.cpp",
        ROOT / "src/go2/build_config.h",
        ROOT / "src/go2/robots.json",
    ),
    (
        "go2_nixo",
        ROOT / "src/go2_nixo/ring_led/bar_display.cpp",
        ROOT / "src/go2_nixo/ring_led/ring_display.cpp",
        ROOT / "src/go2_nixo/build_config.h",
        ROOT / "src/go2_nixo/robots.json",
    ),
)

HP_BAR_GROUP_COUNT = 28
HP_BAR_LEDS_PER_GROUP = 3
HP_BAR_LED_COUNT = HP_BAR_GROUP_COUNT * HP_BAR_LEDS_PER_GROUP
RING_LED_COUNT = 40


def linked_group_indices(group_1_based: int) -> tuple[int, int, int]:
    """Mirror the Go2 HP bar harness mapping from HW_Go2_HP_BAR."""
    return (
        group_1_based - 1,
        2 * HP_BAR_GROUP_COUNT - group_1_based,
        2 * HP_BAR_GROUP_COUNT - 1 + group_1_based,
    )


def healthy_groups(fill_ratio: float) -> int:
    return max(0, min(int(fill_ratio * HP_BAR_GROUP_COUNT + 0.5), HP_BAR_GROUP_COUNT))


def test_hp_bar_reference_group_mapping() -> None:
    assert linked_group_indices(1) == (0, 55, 56)  # LEDs 1, 56, 57
    assert linked_group_indices(2) == (1, 54, 57)  # LEDs 2, 55, 58
    assert linked_group_indices(28) == (27, 28, 83)  # LEDs 28, 29, 84

    all_indices = {
        idx
        for group in range(1, HP_BAR_GROUP_COUNT + 1)
        for idx in linked_group_indices(group)
    }
    assert all_indices == set(range(HP_BAR_LED_COUNT))


def test_hp_bar_sample_hp_fill_counts() -> None:
    assert healthy_groups(1.0) == 28
    assert healthy_groups(0.5) == 14
    assert healthy_groups(0.25) == 7
    assert healthy_groups(0.0) == 0


def test_defaults_separate_hp_bar_and_cooldown_ring_pins() -> None:
    for firmware, _bar_cpp, _ring_cpp, _build_config, robots_json in FIRMWARES:
        defaults = json.loads(robots_json.read_text())["defaults"]
        assert defaults["led_pin"] == 18, firmware
        assert defaults["num_leds"] == HP_BAR_LED_COUNT, firmware
        assert defaults["led_brightness"] == 120, firmware
        assert defaults["ring_led_pin"] == 4, firmware
        assert defaults["ring_num_leds"] == RING_LED_COUNT, firmware
        assert defaults["ring_led_brightness"] == 80, firmware
        assert defaults["nixo_fire_cooldown_ms"] == 10000, firmware


def test_hp_bar_renderer_uses_bar_led_layout() -> None:
    for firmware, bar_cpp, _ring_cpp, _build_config, _robots_json in FIRMWARES:
        source = bar_cpp.read_text()
        header = bar_cpp.with_suffix(".h").read_text()
        assert "CRGB leds_[HP_BAR_NUM_LEDS] = {};" in header, firmware
        assert "BarDisplay::begin" in source, firmware
        assert "FastLED.addLeds<WS2815, HP_BAR_LED_PIN, RGB>" in source, firmware
        assert "i < HP_BAR_NUM_LEDS" in source, firmware
        assert "i < NUM_LEDS" not in source, firmware
        assert "setHpBarGroup" in source, firmware
        assert "group 1  -> LEDs 1, 56, 57" in source, firmware
        assert "group 28 -> LEDs 28, 29, 84" in source, firmware
        assert "row2Index = 2 * HP_BAR_GROUP_COUNT - group1Based" in source, firmware
        assert "row3Index = 2 * HP_BAR_GROUP_COUNT - 1 + group1Based" in source, firmware


def test_cooldown_ring_renderer_uses_original_ring_pin() -> None:
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in FIRMWARES:
        source = ring_cpp.read_text()
        assert "RingDisplay::begin" in source, firmware
        assert "FastLED.addLeds<WS2811, RING_LED_PIN, RGB>" in source, firmware
        assert "renderCooldown" in source, firmware
        assert "RING_NUM_LEDS" in source, firmware


def test_cooldown_ring_renders_fire_and_ten_step_green_fill() -> None:
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in FIRMWARES:
        source = ring_cpp.read_text()
        assert "renderFiring" in source, firmware
        assert "renderInhibited" not in source, firmware
        assert "blinkOn_" not in source, firmware
        assert "constexpr uint8_t RING_COOLDOWN_FILL_STEPS = 10" in source, firmware
        assert "if (firing_)" in source, firmware
        assert "if (inhibited_)" in source, firmware
        assert "if (firing_ || inhibited_)" not in source, firmware
        assert "CRGB color = scaled(96, 0, 0)" in source, firmware
        assert "uint32_t elapsed = cooldownDurationMs_ > remaining ? cooldownDurationMs_ - remaining : 0;" in source, firmware
        assert "completedSteps * RING_NUM_LEDS" in source, firmware
        assert "CRGB cooldownColor = scaled(0, 64, 0)" in source, firmware
        assert "scaled(96, 48, 0)" not in source, firmware
        assert (
            "cooldownActive(uint32_t now) const {\n"
            "  return firing_ || inhibited_ || remainingMs(now) > 0;\n"
            "}"
        ) in source, firmware
        assert "if (remaining > 0)" in source, firmware


def test_cooldown_ring_uses_duration_sentinel_not_zero_start_time() -> None:
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in FIRMWARES:
        source = ring_cpp.read_text()
        header = ring_cpp.with_suffix(".h").read_text()
        assert "uint32_t cooldownDurationMs_ = 0;" in header, firmware
        assert "if (cooldownDurationMs_ == 0) return 0;" in source, firmware
        assert "if (cooldownStartedMs_ == 0) return 0;" not in source, firmware
        assert "if (!externalState_ && cooldownDurationMs_ != 0)" in source, firmware
        assert "cooldownDurationMs_ = 0;" in source, firmware


def test_bar_remote_ttl_is_bounded_for_signed_expiry_math() -> None:
    for firmware, bar_cpp, _ring_cpp, _build_config, _robots_json in FIRMWARES:
        source = bar_cpp.read_text()
        assert "if (ttlMs < 1) ttlMs = 1;" in source, firmware
        assert "if (ttlMs > 0x7ffffffful) ttlMs = 0x7ffffffful;" in source, firmware
        assert "remoteExpiresMs_ = now + ttlMs;" in source, firmware


def test_cooldown_ring_marks_dirty_only_when_frame_changes() -> None:
    for firmware, _bar_cpp, ring_cpp, _build_config, _robots_json in FIRMWARES:
        source = ring_cpp.read_text()
        assert "bool mismatch = false;" in source, firmware
        assert "CRGB expected = (i < lit) ? cooldownColor : CRGB::Black;" in source, firmware
        assert "if (leds_[i] != expected)" in source, firmware
        assert "if (mismatch) {" in source, firmware
        assert (
            "for (int i = 0; i < RING_NUM_LEDS; i++) {\n"
            "    leds_[i] = (i < lit) ? cooldownColor : CRGB::Black;\n"
            "  }\n"
            "  dirty_ = true;"
        ) not in source, firmware


def test_build_config_locks_bar_and_ring_shapes() -> None:
    for firmware, _bar_cpp, _ring_cpp, build_config, _robots_json in FIRMWARES:
        source = build_config.read_text()
        assert "#define BATTLEBANG_LED_PIN 18" in source, firmware
        assert "#define BATTLEBANG_NUM_LEDS 84" in source, firmware
        assert "#define BATTLEBANG_LED_BRIGHTNESS 120" in source, firmware
        assert "#define BATTLEBANG_RING_LED_PIN 4" in source, firmware
        assert "#define BATTLEBANG_RING_NUM_LEDS 40" in source, firmware
        assert "#define BATTLEBANG_RING_LED_BRIGHTNESS 80" in source, firmware
        assert "#define BATTLEBANG_NIXO_FIRE_COOLDOWN_MS 10000" in source, firmware
        assert "#define BATTLEBANG_HP_BAR_GROUP_COUNT 28" in source, firmware
        assert "#define BATTLEBANG_HP_BAR_LEDS_PER_GROUP 3" in source, firmware
        assert "HP bar LED count must match grouped bar layout" in source, firmware
        assert "HP bar and cooldown ring pins must be different" in source, firmware


def test_go2_mirrors_nixo_command_for_cooldown_ring() -> None:
    source = (ROOT / "src/go2/mqtt/hit_mqtt_client.cpp").read_text()
    header = (ROOT / "src/go2/mqtt/hit_mqtt_client.h").read_text()
    main = (ROOT / "src/go2/main.cpp").read_text()
    assert "nixoCommandTopic_" in source
    assert "handleNixoCommandMessage" in source
    assert "using NixoFireMirrorHandler = void (*)(bool enabled, uint32_t fireDurationMs, uint32_t cooldownMs);" in header
    assert 'doc["enabled"].is<bool>()' in source
    assert 'const bool enabled = doc["enabled"].as<bool>();' in source
    assert 'doc["enabled"] | true' not in source
    assert 'uint32_t durationMs = doc["duration_ms"] | NIXO_FIRE_DEFAULT_DURATION_MS' in source
    assert "nixoFireMirrorHandler_(false, 0, NIXO_FIRE_COOLDOWN_MS)" in source
    assert "nixoFireMirrorHandler_(true, durationMs, NIXO_FIRE_COOLDOWN_MS)" in source
    assert "ringDisplay.clearCooldown()" in main
    assert "ringDisplay.startFire(fireDurationMs, cooldownMs, now)" in main


def test_go2_nixo_drives_ring_from_local_fire_state() -> None:
    main = (ROOT / "src/go2_nixo/main.cpp").read_text()
    fire_header = (ROOT / "src/go2_nixo/nixo/nixo_fire_client.h").read_text()
    fire_source = (ROOT / "src/go2_nixo/nixo/nixo_fire_client.cpp").read_text()
    assert "ringDisplay.setCooldownState" in main
    assert "nixoFire.isFiring()" in main
    assert "nixoFire.cooldownRemainingMs(now)" in main
    assert "uint32_t cooldownRemainingMs(uint32_t now) const;" in fire_header
    assert "uint32_t cooldownStartedMs_ = 0;" in fire_header
    assert "void beginCooldown(uint32_t now);" in fire_header
    assert "uint32_t remainingMs = cooldownRemainingMs(now)" in fire_source
    assert "beginCooldown(now)" in fire_source
    assert "bool wasFiring = isFiring();" in fire_source
    assert "beginCooldown(millis());" in fire_source
    assert 'doc["enabled"].is<bool>()' in fire_source
    assert 'const bool enabled = doc["enabled"].as<bool>();' in fire_source
    assert 'doc["enabled"] | true' not in fire_source
    assert "uint32_t elapsed = now - cooldownStartedMs_;" in fire_source
    stop_block = fire_source.split("void NixoFireClient::stopFire", 1)[1].split(
        "const char* NixoFireClient::commandTopic",
        1,
    )[0]
    assert "cooldownStartedMs_ = 0;" not in stop_block


def test_go2_nixo_integrated_fire_supports_1ch_and_2ch_variants() -> None:
    robots = json.loads((ROOT / "src/go2_nixo/robots.json").read_text())["defaults"]
    relay_1ch = json.loads((ROOT / "src/go2_nixo/variants/relay_1ch/config.json").read_text())
    relay_2ch = json.loads((ROOT / "src/go2_nixo/variants/relay_2ch/config.json").read_text())
    platformio = (ROOT / "platformio.ini").read_text()
    build_config = (ROOT / "src/go2_nixo/build_config.h").read_text()
    fire_source = (ROOT / "src/go2_nixo/nixo/nixo_fire_client.cpp").read_text()
    config_script = (ROOT / "scripts/go2_nixo_config.py").read_text()

    assert robots["nixo_relay1_pin"] == 23
    assert robots["nixo_relay2_pin"] == -1
    assert robots["nixo_relay_on_level"] == 1
    assert robots["nixo_relay_off_level"] == 0
    assert robots["nixo_relay_delay1_ms"] == 800

    assert relay_1ch["nixo_relay1_pin"] == 23
    assert relay_1ch["nixo_relay2_pin"] == -1
    assert relay_1ch["nixo_relay_on_level"] == 1
    assert relay_1ch["nixo_relay_off_level"] == 0
    assert relay_2ch["nixo_relay1_pin"] == 22
    assert relay_2ch["nixo_relay2_pin"] == 23
    assert relay_2ch["nixo_relay_on_level"] == 0
    assert relay_2ch["nixo_relay_off_level"] == 1
    assert relay_2ch["nixo_relay_delay1_ms"] == 150

    assert "custom_nixo_variant = relay_1ch" in platformio
    assert "custom_nixo_variant = relay_2ch" in platformio
    assert "[env:esp32dev_go2_nixo_1ch_go2_06]" in platformio
    assert "[env:esp32dev_go2_nixo_2ch_go2_06]" in platformio

    assert "def load_relay_variant" in config_script
    assert '"GO2_NIXO_RELAY_VARIANT"' in config_script
    assert '"custom_nixo_variant"' in config_script
    assert '"nixo_relay_delay1_ms": "BATTLEBANG_BUILD_NIXO_RELAY_DELAY1_MS"' in config_script
    assert "#define BATTLEBANG_NIXO_RELAY1_PIN 23" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY2_PIN -1" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY_ON_LEVEL HIGH" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY_OFF_LEVEL LOW" in build_config
    assert "#define BATTLEBANG_NIXO_RELAY_DELAY1_MS 800" in build_config
    assert "static constexpr uint32_t NIXO_RELAY_DELAY1_MS =\n    BATTLEBANG_NIXO_RELAY_DELAY1_MS;" in build_config

    begin_block = fire_source.split("void NixoFireClient::begin()", 1)[1].split("mqttClient_.setServer", 1)[0]
    assert begin_block.index("digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);") < begin_block.index(
        "digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);"
    )
    relay_off_block = fire_source.split("void NixoFireClient::relayOff()", 1)[1].split(
        "void NixoFireClient::updateFireSequence",
        1,
    )[0]
    assert relay_off_block.index("digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);") < relay_off_block.index(
        "digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);"
    )
    update_block = fire_source.split("void NixoFireClient::updateFireSequence", 1)[1]
    two_channel_done = update_block.split("case FIRE_RELAY_WAIT2:", 1)[1].split("return;", 1)[0]
    assert two_channel_done.index("digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);") < two_channel_done.index(
        "digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);"
    )
    assert two_channel_done.index("CH2 OFF pin=%d level=%d readback=%d") < two_channel_done.index(
        "CH1 OFF pin=%d level=%d readback=%d"
    )


def test_standalone_nixo_stop_preserves_local_cooldown_gate() -> None:
    source = (ROOT / "src/nIxo/main.cpp").read_text()
    build_config = (ROOT / "src/nIxo/build_config.h").read_text()
    stop_block = source.split("static void stopFireSequence", 1)[1].split("static bool startFireSequence", 1)[0]
    assert "lastFireStartMs = now;" in source
    assert "lastFireStartMs = 0;" not in stop_block
    assert "stopFireSequence(\"mqtt\")" in source
    assert 'doc["enabled"].is<bool>()' in source
    assert 'const bool enabled = doc["enabled"].as<bool>();' in source
    assert 'doc["enabled"] | true' not in source
    assert "#define NIXO_RELAY1_PIN 23" in build_config
    assert "#define NIXO_RELAY2_PIN -1" in build_config
    assert "#define NIXO_RELAY_ON_LEVEL HIGH" in build_config
