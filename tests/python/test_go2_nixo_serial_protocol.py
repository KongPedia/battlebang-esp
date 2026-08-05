from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/go2_nixo_serial/golden_vectors.json"
FIXTURE_SHA256 = "95ece74f2347ecfd3bd6f58e2732b32182425397b9ee7d2d187413d978b9b32a"


def fixture() -> dict[str, object]:
    return json.loads(FIXTURE.read_text(encoding="utf-8"))


def test_canonical_fixture_hash_and_vector_count() -> None:
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == FIXTURE_SHA256
    assert len(fixture()["golden_vectors"]) == 10


def test_diagnostic_build_is_echo_only_and_production_has_no_legacy_fallback() -> None:
    platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    workflow = (ROOT / ".github/workflows/firmware-ota.yml").read_text(encoding="utf-8")
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text(encoding="utf-8")
    protocol = (ROOT / "firmware/go2_nixo/serial/protocol.cpp").read_text(encoding="utf-8")
    assert "[env:esp32dev_go2_nixo_serial_diag]" in platformio
    assert "esp32dev_go2_nixo_serial_diag" not in workflow
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
    assert "callbacks.hp_damage = onSerialHpDamage;" in main
    assert "callbacks.hp_guard = onSerialHpGuard;" in main
    assert "bool hpGuardEnabled = false;" in main
    assert "if (hpGuardActive(now))" in main
    assert "if (hpGuardActive(now) || localHitState.down" in main
    assert "applyLocalHit(++hitSequence, now, false);" in main
    assert "CapabilityHpDamage" in main
    assert "CapabilityHpGuard" in main
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
    assert "MessageType::HpDamage" in runtime
    assert "MessageType::HpGuard" in runtime
    assert "hasValidFlagsForType" in protocol


def test_uart_fire_faults_latch_release_without_disabling_mqtt_fire() -> None:
    main = (ROOT / "firmware/go2_nixo/main.cpp").read_text(encoding="utf-8")
    runtime = (ROOT / "firmware/go2_nixo/serial/runtime.cpp").read_text(encoding="utf-8")

    defer_block = main.split("static bool shouldDeferNetworkForFire", 1)[1].split(
        "static void refreshNixoFireInhibit", 1
    )[0]
    inhibit_block = main.split("static void refreshNixoFireInhibit", 1)[1].split(
        "static const char* serialFireSourceName", 1
    )[0]
    stop_block = main.split("static void stopSerialFire", 1)[1].split(
        "static uint8_t onSerialFireHold", 1
    )[0]
    link_lost_block = main.split("static void onSerialLinkLost", 1)[1].split(
        "static serial::HpSnapshot", 1
    )[0]

    assert "jetsonFireReleaseRequired" not in defer_block
    assert "jetsonSession.connected()" not in inhibit_block
    assert "localHitState.down || localHitState.hpRemaining == 0" in inhibit_block
    assert "requiresExplicitFireRelease(reason)" in stop_block
    assert "reason == serial::FireReason::OperatorRelease" in stop_block
    assert "const bool uartOwnedFire = jetsonFireHoldActive;" in link_lost_block
    assert "uartOwnedFire" in link_lost_block
    assert "reason == FireReason::HoldTimeout" in runtime
    assert "reason == FireReason::LinkStale" in runtime
    assert "reason == FireReason::SessionChanged" in runtime
    assert "reason == FireReason::InternalFault" in runtime
    assert "JETSON_PARSER_FAULT_THRESHOLD = 8" in main
    assert "noteJetsonParserErrors" in main
    assert "if (jetsonUartQueue == nullptr) return;" in main
    assert "if (frame.type == MessageType::Nack) ++counters_.reliable_drops;" in runtime


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
        [str(executable), *(f"{vector['name']}={vector['hex'].replace(' ', '')}" for vector in vectors)],
        check=True,
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    assert result.stdout == "go2_nixo_serial host checks passed\n"
