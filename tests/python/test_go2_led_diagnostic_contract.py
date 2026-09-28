"""Keep the temporary HP-bar wiring diagnostic isolated from HP ownership."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "firmware/go2_nixo/display/bar_display.h").read_text()
SOURCE = (ROOT / "firmware/go2_nixo/display/bar_display.cpp").read_text()
FRAMED = (ROOT / "firmware/go2_nixo_framed_packet_uart/main.cpp").read_text()


def test_hp_bar_diagnostic_selects_one_group_or_raw_pixel_with_timeout() -> None:
    assert "bool setDiagnosticGroup(int group1Based, uint32_t now);" in HEADER
    assert "bool setDiagnosticPixel(int pixel1Based, uint32_t now);" in HEADER
    assert "void clearDiagnostic();" in HEADER
    assert "constexpr uint32_t DIAGNOSTIC_TTL_MS = 10000;" in SOURCE
    assert "setHpBarGroup(diagnosticGroup_, CRGB::White)" in SOURCE
    assert "leds_[diagnosticPixel_ - 1] = CRGB::White" in SOURCE
    assert "diagnosticExpiresMs_ = now + DIAGNOSTIC_TTL_MS" in SOURCE
    assert "clearDiagnostic();" in SOURCE


def test_framed_serial_led_diagnostic_never_mutates_hp() -> None:
    command = FRAMED.split("static void handleCommandLine", 1)[1].split("static bool isImmediateCommandChar", 1)[0]
    assert 'lower.startsWith("led-test ")' in command
    assert "barDisplay.setDiagnosticGroup(" in command
    assert "barDisplay.setDiagnosticPixel(" in command
    assert "barDisplay.clearDiagnostic()" in command
    assert "applyLocalHit(" not in command


def test_hp_down_preempts_bench_diagnostic() -> None:
    assert 'if (localDown_ || (remoteActive_ && (remoteDown_ || remoteMode_ == "down"))) return false;' in SOURCE
    assert 'if (localDown_ && (diagnosticGroup_ > 0 || diagnosticPixel_ > 0)) clearDiagnostic();' in SOURCE
    assert 'if ((remoteDown_ || mode == "down") && (diagnosticGroup_ > 0 || diagnosticPixel_ > 0)) clearDiagnostic();' in SOURCE
    assert SOURCE.index('A bench wiring check must never hide') < SOURCE.index('if (diagnosticGroup_ > 0 || diagnosticPixel_ > 0) {')
    assert 'if (localDown_) {\n      renderLocal(now);\n    } else if (remoteActive_)' in SOURCE
