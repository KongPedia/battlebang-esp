from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "firmware/go2_nixo_framed_packet_uart/main.cpp").read_text()
BUILD_CONFIG = (ROOT / "firmware/go2_nixo/build_config.h").read_text()
RUNTIME_CONFIG = (ROOT / "firmware/go2_nixo/config/runtime_config.cpp").read_text()
BUILD_SCRIPT = (ROOT / "scripts/go2_nixo_config.py").read_text()


def test_front_channel_is_retained_but_disabled_by_default() -> None:
    assert "#define BATTLEBANG_FRONT_PIEZO_AO_PIN 32" in BUILD_CONFIG
    assert "#define BATTLEBANG_PIEZO_CHANNEL_ENABLE_MASK 0x03" in BUILD_CONFIG
    assert "PIEZO_CHANNEL_FRONT = 0x04" in MAIN


def test_channels_must_qualify_independently_before_peak_selection() -> None:
    assert "struct PiezoChannelState" in MAIN
    assert "state.qualified = true;" in MAIN
    assert "analogPiezo.left.qualified && sample.left >= 0" in MAIN
    assert "analogPiezo.right.qualified && sample.right > sample.raw" in MAIN
    assert "analogPiezo.front.qualified && sample.front > sample.raw" in MAIN
    assert "piezoChannelEnabled(PIEZO_CHANNEL_LEFT) && PIEZO_LEFT_AO_PIN >= 0) ||" in MAIN
    assert "piezoChannelEnabled(PIEZO_CHANNEL_FRONT) && PIEZO_FRONT_AO_PIN >= 0);" in MAIN
    assert MAIN.index("qualifyPiezoChannels(sample, now)") < MAIN.index(
        "const int raw = sample.raw", MAIN.index("static void pollAnalogPiezo")
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
