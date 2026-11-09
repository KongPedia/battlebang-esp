from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "firmware/go2_full/main.cpp").read_text()
PIEZO_HEADER = (ROOT / "firmware/go2_full/piezo/piezo_sensor_core.h").read_text()
PIEZO_SOURCE = (ROOT / "firmware/go2_full/piezo/piezo_sensor_core.cpp").read_text()
BUILD_CONFIG = (ROOT / "firmware/go2_full/build_config.h").read_text()
RUNTIME_CONFIG = (ROOT / "firmware/go2_full/config/runtime_config.cpp").read_text()
BUILD_SCRIPT = (ROOT / "scripts/go2_full_config.py").read_text()


def test_front_channel_is_retained_but_disabled_by_default() -> None:
    assert "#define BATTLEBANG_FRONT_PIEZO_AO_PIN 32" in BUILD_CONFIG
    assert "#define BATTLEBANG_PIEZO_CHANNEL_ENABLE_MASK 0x03" in BUILD_CONFIG
    assert "PIEZO_CHANNEL_FRONT = 0x04" in PIEZO_HEADER


def test_channels_must_qualify_independently_before_peak_selection() -> None:
    assert "struct ChannelState" in PIEZO_HEADER
    assert "state.qualified = true;" in PIEZO_SOURCE
    assert "left_.qualified && sample.left >= 0" in PIEZO_SOURCE
    assert "right_.qualified && sample.right > sample.raw" in PIEZO_SOURCE
    assert "front_.qualified && sample.front > sample.raw" in PIEZO_SOURCE
    assert "channelEnabled(PIEZO_CHANNEL_LEFT)" in PIEZO_SOURCE
    assert "channelEnabled(PIEZO_CHANNEL_FRONT)" in PIEZO_SOURCE
    assert PIEZO_SOURCE.index("qualifyChannel(left_") < PIEZO_SOURCE.index(
        "selectQualifiedPeak(sample)"
    )


def test_runtime_mask_is_nvs_backward_compatible_and_visible() -> None:
    assert 'getUChar("piezo_mask", hit.piezoChannelEnableMask)' in RUNTIME_CONFIG
    assert 'putUChar("piezo_mask", hit.piezoChannelEnableMask)' in RUNTIME_CONFIG
    assert '"piezo_channel_enable_mask"' in RUNTIME_CONFIG
    assert 'createNestedObject("piezo_channels")' in MAIN
    assert 'left["qualified"]' in MAIN
    assert 'right["qualified"]' in MAIN
    assert 'front["qualified"]' in MAIN


def test_hardware_profile_mask_reaches_build_define() -> None:
    assert '"piezo_channel_enable_mask": "BATTLEBANG_BUILD_PIEZO_CHANNEL_ENABLE_MASK"' in BUILD_SCRIPT
    assert '"BATTLEBANG_PIEZO_CHANNEL_ENABLE_MASK"' in BUILD_SCRIPT


def test_hardware_mask_and_bar_count_survive_clear_config() -> None:
    assert 'kHardwareNamespace = "go2_full_hw"' in RUNTIME_CONFIG
    assert 'kPreviousHardwareNamespace = "go2_nixo_hw"' in RUNTIME_CONFIG
    assert 'hardware.preferences().putUChar("piezo_mask", current.hit.piezoChannelEnableMask)' in RUNTIME_CONFIG
    assert 'hardware.preferences().putUChar("bar_groups", current.hit.hpBarGroupCount)' in RUNTIME_CONFIG
    assert RUNTIME_CONFIG.index('hardware.end();\n  return battlebang::esp::nvs::clearNamespace') > RUNTIME_CONFIG.index('const RuntimeConfig current = runtimeConfigFromNvsOrBuild();')
    framed_clear = MAIN.split('if (lower == "clear-config") {', 1)[1].split('if (lower.startsWith("provision "))', 1)[0]
    assert 'if (cleared) {' in framed_clear
    assert 'runtimeConfig = runtimeConfigFromNvsOrBuild();' in framed_clear


def test_only_go2_full_packet_main_remains() -> None:
    assert not (ROOT / "firmware/go2_nixo").exists()
    assert not (ROOT / "firmware/go2_nixo_framed_packet_uart").exists()
    assert (ROOT / "firmware/go2_full/main.cpp").is_file()
    assert "pollJetsonUart();" in MAIN
    assert "pollCommandStream(JetsonSerial" not in MAIN
