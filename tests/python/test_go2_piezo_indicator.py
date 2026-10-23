"""Lock the local HP-bar indication used while piezo detection is disabled."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "firmware/go2_nixo/display/bar_display.h").read_text()
SOURCE = (ROOT / "firmware/go2_nixo/display/bar_display.cpp").read_text()


def test_piezo_detection_defaults_enabled_and_exposes_a_runtime_switch() -> None:
    assert "void setPiezoDetectionEnabled(bool enabled);" in HEADER
    assert "bool piezoDetectionEnabled_ = true;" in HEADER
    assert "void BarDisplay::setPiezoDetectionEnabled(bool enabled)" in SOURCE
    assert "piezoDetectionEnabled_ = enabled;" in SOURCE
    assert (
        "dirty_ = true;"
        in SOURCE.split("void BarDisplay::setPiezoDetectionEnabled", 1)[1].split(
            "}", 1
        )[0]
    )


def test_disabled_indicator_is_a_nonblocking_neon_comet() -> None:
    assert "constexpr uint32_t PIEZO_DISABLED_FRAME_MS = 90;" in SOURCE
    renderer = SOURCE.split("void BarDisplay::renderPiezoDisabled", 1)[1].split(
        "void BarDisplay::renderLocal", 1
    )[0]
    assert "frame != lastPiezoDisabledFrame_" in renderer
    assert "CRGB color(18, 0, 36);" in renderer
    assert "CRGB(0, 255, 190)" in renderer
    assert "setHpBarGroup(group + 1, color)" in renderer
    assert "frame % groupCount_" in renderer
    assert "delay(" not in renderer
    assert "FastLED.show" not in renderer
    assert "renderBlank();" in renderer


def test_safety_and_operator_indications_precede_disabled_indicator() -> None:
    tick = SOURCE.split("void BarDisplay::tick", 1)[1].split(
        "bool BarDisplay::startupReady", 1
    )[0]
    assert tick.index("if (diagnosticGroup_ > 0 || diagnosticPixel_ > 0)") < tick.index(
        "startupLoading_ && !startupReady(now)"
    )
    assert tick.index("startupLoading_ && !startupReady(now)") < tick.index(
        "!piezoDetectionEnabled_"
    )
    assert tick.index("if (localDown_)") < tick.index("!piezoDetectionEnabled_")
    assert 'if (remoteDown_ || remoteMode_ == "down") renderRemote(now);' in tick
