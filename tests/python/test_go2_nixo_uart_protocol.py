import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/go2_nixo_uart/golden_vectors.json"
FIXTURE_SHA256 = "83383d602a195388425e549f58e05fead5c75df93d8b779c3778687b5460a45a"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_uart_fixture_and_cpp_contract_are_present() -> None:
    payload = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == FIXTURE_SHA256
    assert payload["frame_format_version"] == 2
    assert len(payload["golden_vectors"]) == 10

    header = read("firmware/go2_nixo_framed_packet_uart/uart/protocol.h")
    assert "kFrameVersion = 2" in header
    assert "CapabilitiesRequest = 0x01" in header
    assert "DeviceStatus = 0x02" in header
    assert "FireHold = 0x10" in header
    assert "FireStop = 0x11" in header
    assert "HpDamage = 0x21" in header
    assert "HpSnapshot = 0x22" in header
    assert "HitEvent = 0x23" in header
    assert "CapabilityHpDamage = 0x00000040" in header
    assert "sender_epoch" in header
    assert "session_id" not in header


def _env_block(platformio: str, name: str) -> str:
    start = platformio.index(f"[env:{name}]")
    end = platformio.find("\n[", start + 1)
    return platformio[start:] if end < 0 else platformio[start:end]


def test_go2_nixo_line_and_framed_packet_firmware_are_separate() -> None:
    platformio = read("platformio.ini")
    base = _env_block(platformio, "esp32dev_go2_nixo")
    assert "GO2_NIXO_UART_PACKET_V2" not in base
    for name in ("esp32dev_go2_nixo_1ch", "esp32dev_go2_nixo_2ch"):
        assert f"[env:{name}]" in platformio
    for name in (
        "esp32dev_go2_nixo_framed_packet_uart_1ch",
        "esp32dev_go2_nixo_framed_packet_uart_2ch",
    ):
        assert f"[env:{name}]" in platformio

    line_main = read("firmware/go2_nixo/main.cpp")
    assert 'pollCommandStream(JetsonSerial, jetsonCommandLine, "jetson")' in line_main
    assert "pollJetsonUart" not in line_main

    framed_main = read("firmware/go2_nixo_framed_packet_uart/main.cpp")
    assert "GO2_NIXO_UART_PACKET_V2" not in framed_main
    assert "pollJetsonUart" in framed_main
    assert "pollCommandStream(JetsonSerial" not in framed_main
    assert "jetsonCommandLine" not in framed_main
    assert 'doc["jetson_uart_protocol"] = "framed"' in framed_main
    assert "jetsonAuthorizedHostEpoch" in framed_main
    assert "packetRobotIdentityMatches" in framed_main
    assert "packetCommandNeedsAuthority" in framed_main
    assert 'doc["jetson_uart_rx_frames"]' in framed_main
    assert 'doc["jetson_uart_rx_discarded_bytes"]' in framed_main
    assert 'doc["jetson_uart_rx_crc_errors"]' in framed_main
    assert 'doc["jetson_uart_rx_last_hex"]' in framed_main
    assert "queueJetsonFireStatusPacket(nextJetsonPacketSequence())" in framed_main
    assert "jetsonLastFireReason = FireReason::HoldTimeout" in framed_main
    assert "jetsonReliableAdmissionErrors" in framed_main

    config_script = read("scripts/go2_nixo_config.py")
    assert 'project_option("custom_go2_nixo_firmware")' in config_script
    assert '{"single_char_newline", "framed_packet_uart"}' in config_script
    assert 'f"go2-nixo-framed-packet-uart-{slug}"' in config_script

def test_firmware_versions_are_release_generated_not_branch_hardcoded() -> None:
    assert not (ROOT / "firmware/go2_nixo/app/version.h").exists()
    assert not (ROOT / "firmware/go2_nixo_framed_packet_uart/app/version.h").exists()
    assert read("firmware/go2_nixo/app/version_autogen.h") == "#pragma once\n"
    assert read("firmware/go2_nixo_framed_packet_uart/app/version_autogen.h") == "#pragma once\n"
    for path in (
        "firmware/go2_nixo/app/firmware_info.h",
        "firmware/go2_nixo_framed_packet_uart/app/firmware_info.h",
    ):
        info = read(path)
        assert '#include "version.h"' not in info
        assert '#define BB_GO2_NIXO_VERSION "0.1.0-local"' in info
        assert "#define BB_GO2_NIXO_BUILD 1" in info
    assert "commonDefaults.otaAutoCheckEnabled = false;" in read("firmware/go2_nixo/config/runtime_config.cpp")


def test_uart_safety_contract_is_not_session_gated() -> None:
    runtime = read("firmware/go2_nixo_framed_packet_uart/uart/runtime.cpp")
    assert "MessageType::FireStop" in runtime
    assert "applyFireStop" in runtime
    assert "SessionRequired" not in runtime
    assert "SessionMismatch" not in runtime
    assert "parserFault" not in runtime
    assert "DeviceStatus" in runtime


def test_uart_flags_match_python_registry() -> None:
    protocol = read("firmware/go2_nixo_framed_packet_uart/uart/protocol.cpp")
    assert "case MessageType::FireStop:" in protocol
    assert "case MessageType::HpReset:" in protocol
    assert "case MessageType::HpDamage:" in protocol
    assert "case MessageType::HitEvent:" in protocol
    assert "case MessageType::DiagEcho:" in protocol
    assert "return frame.flags == FrameFlags::AckRequired;" in protocol
    assert "return frame.flags == FrameFlags::Response;" in protocol
    runtime = read("firmware/go2_nixo_framed_packet_uart/uart/runtime.cpp")
    assert "void composeFireStatus(" in runtime


def _hex_bytes(value: str) -> bytes:
    return bytes(int(part, 16) for part in value.split())


def _crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _decode_frame(wire: bytes) -> dict[str, int | bytes]:
    assert wire[:3] == b"\xAA\x55\x02"
    payload_length = int.from_bytes(wire[11:13], "big")
    assert len(wire) == 15 + payload_length
    assert _crc16_ccitt_false(wire[2:-2]) == int.from_bytes(wire[-2:], "big")
    return {
        "type": wire[3],
        "flags": wire[4],
        "sequence": int.from_bytes(wire[5:7], "big"),
        "sender_epoch": int.from_bytes(wire[7:11], "big"),
        "payload": wire[13:-2],
    }


def test_uart_fixture_vectors_parse_without_cpp_source_grep() -> None:
    payload = json.loads(FIXTURE.read_text(encoding="utf-8"))
    vectors = payload["golden_vectors"] + payload["edge_vectors"]
    assert len(vectors) == 20
    for vector in vectors:
        wire = _hex_bytes(vector["hex"])
        if vector.get("expected") == "crc_error":
            assert _crc16_ccitt_false(wire[2:-2]) != int.from_bytes(wire[-2:], "big")
            continue
        frame = _decode_frame(wire)
        if vector.get("expected") == "invalid_flags":
            assert frame["flags"] == 0x05
        elif vector.get("expected") == "invalid_payload":
            assert frame["type"] == 0x11 and len(frame["payload"]) != 1
        elif vector.get("expected") == "stale_epoch":
            assert frame["sender_epoch"] != 0x01020304
        else:
            assert frame["sender_epoch"] != 0
