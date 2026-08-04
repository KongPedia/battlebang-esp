from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/go2_nixo_serial/golden_vectors.json"
FIXTURE_SHA256 = "bbdf0b9cd1f30be9633b24b210dfe5c4c695b0b6a7c5866c13c581141645fd84"


def fixture() -> dict[str, object]:
    return json.loads(FIXTURE.read_text(encoding="utf-8"))


def test_canonical_fixture_hash_and_vector_count() -> None:
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == FIXTURE_SHA256
    assert len(fixture()["golden_vectors"]) == 8


def test_diagnostic_build_is_echo_only_and_production_has_no_legacy_fallback() -> None:
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text(encoding="utf-8")
    protocol = (ROOT / "firmware/go2_nixo/serial/protocol.cpp").read_text(encoding="utf-8")
    assert "[env:esp32dev_go2_nixo_serial_diag]" in platformio
    assert platformio.count("BATTLEBANG_UART_DIAGNOSTIC=1") == 1
    diagnostic_env = platformio.split("[env:esp32dev_go2_nixo_serial_diag]", 1)[1].split("[env:", 1)[0]
    assert "+<../firmware/go2_nixo/main.cpp>" in diagnostic_env
    assert "+<../firmware/go2_nixo/serial/**>" in diagnostic_env
    assert "lib_ldf_mode = off" in diagnostic_env
    diagnostic = protocol.split("IncrementalParser diagnosticParser;", 1)[1].split("#endif", 1)[0]
    assert "FrameTxQueue diagnosticTx;" in diagnostic
    assert "Serial.begin(UART_BAUD);" in diagnostic
    assert "Serial.available()" in diagnostic
    assert "Serial.availableForWrite()" in diagnostic
    assert "Serial.write(bytes, requested)" in diagnostic
    assert "composeDiagEchoReply" in diagnostic
    assert diagnostic.count("forceRelaysOff();") == 2
    assert "UART_NUM_2" not in diagnostic
    assert "uart_driver_install" not in diagnostic
    assert "Serial.print" not in diagnostic
    assert "Serial.printf" not in diagnostic
    assert "Serial.println" not in diagnostic
    assert "monitor_port" not in diagnostic_env
    assert "serial::diagnosticSetup();" in main and "serial::diagnosticLoop();" in main
    assert "JetsonSerial" not in main
    assert "jetsonCommandLine" not in main
    assert 'pollCommandStream(Serial, usbCommandLine, "usb");' in main
    assert 'pollCommandStream(SerialBT, btCommandLine, "bt");' in main
    assert 'lower == "fire"' not in main
    assert 'lower == "reset"' not in main
    assert 'lower == "stop-fire"' not in main


def test_production_runtime_bridges_binary_uart_fire_and_hp() -> None:
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text(encoding="utf-8")
    protocol = (ROOT / "firmware/go2_nixo/serial/protocol.cpp").read_text(encoding="utf-8")
    runtime_header = (ROOT / "firmware/go2_nixo/serial/runtime.h").read_text(encoding="utf-8")
    runtime = (ROOT / "firmware/go2_nixo/serial/runtime.cpp").read_text(encoding="utf-8")
    nixo_header = (ROOT / "firmware/go2_nixo/nixo/nixo_fire_client.h").read_text(encoding="utf-8")

    assert "serial::ProductionSession jetsonSession;" in main
    assert "jetsonParser.feed" in main
    assert "uart_driver_install(UART_NUM_2" in main
    assert "uart_tx_chars(UART_NUM_2" in main
    assert "pollJetsonBinaryUart(now);" in main
    assert main.index("pollJetsonBinaryUart(now);") < main.index("hitMqtt.tick(now")
    assert "nixoFire.startFire(duration, sourceName, true)" in main
    assert "MAX_CONTINUOUS_FIRE_MS = 10000" in main
    assert "JETSON_FIRE_HOLD_TIMEOUT_MS = 300" in main
    assert "serial::isFireHoldExpired" in main
    assert "stopSerialFire(\"jetson-hold-timeout\"" in main
    assert "localHitState.hpRevision" in main
    assert "jetsonSession.notifyHit(serialHit, eventTsMs);" in main
    assert "resetAll(\"jetson_uart\");" in main
    assert "fireRemainingMs" in nixo_header
    assert "kStatusPeriodMs = 500" in (ROOT / "firmware/go2_nixo/serial/protocol.h").read_text()
    assert "kLinkStaleTimeoutMs = 1500" in (ROOT / "firmware/go2_nixo/serial/protocol.h").read_text()
    assert "class ProductionSession" in runtime_header
    assert "rejectDuplicateOrOutOfOrder" in runtime
    assert "AckResult::Duplicate" in runtime
    assert "NackError::SequenceConflict" in runtime
    assert "NackError::OutOfOrder" in runtime
    assert "NackError::SessionMismatch" in runtime
    assert "MessageType::HitEvent" in runtime and "pumpReliable" in runtime
    assert "MessageType::HpStatus" in runtime and "MessageType::LinkStatus" in runtime
    assert "hasValidFlagsForType" in protocol


def test_portable_cpp_codec_parser_and_diagnostic_composition(tmp_path: Path) -> None:
    compiler = shutil.which("c++")
    assert compiler is not None, "host C++ compiler is required"
    executable = tmp_path / "go2_nixo_serial_test"
    subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            f"-I{ROOT / 'firmware'}",
            str(ROOT / "firmware/go2_nixo/serial/protocol.cpp"),
            str(ROOT / "firmware/go2_nixo/serial/runtime.cpp"),
            str(ROOT / "tests/test_go2_nixo_serial.cpp"),
            "-o",
            str(executable),
        ],
        check=True,
        cwd=ROOT,
    )
    payload = fixture()
    vectors = [*payload["golden_vectors"], *payload["edge_vectors"]]
    result = subprocess.run(
        [str(executable), *(vector["hex"].replace(" ", "") for vector in vectors)],
        check=True,
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert result.stdout == "go2_nixo_serial host checks passed\n"
