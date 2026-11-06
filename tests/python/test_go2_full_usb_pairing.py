"""Exercise the production handshake prefix with a non-actuating host C++ harness."""

from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated production function: {signature}")


@pytest.fixture(scope="module")
def handshake_harness(tmp_path_factory):
    compiler = shutil.which("c++") or shutil.which("g++")
    if not compiler:
        pytest.skip("C++ compiler is required for production handshake tests")
    source = (ROOT / "firmware/go2_full/main.cpp").read_text()
    identity_signature = "static bool packetRobotIdentityMatches("
    identity = (
        _function(source, identity_signature) if identity_signature in source else ""
    )
    start = source.index("static void handleJetsonFrame(")
    end = source.index("    case MessageType::FireHold:", start)
    # Keep production admission and CapabilitiesRequest handling verbatim. Other
    # command handlers are outside this handshake test and cannot actuate here.
    handler = source[start:end] + "    default: return;\n  }\n}\n"
    cpp = tmp_path_factory.mktemp("usb_handshake") / "handshake.cpp"
    cpp.write_text(
        PREAMBLE
        + identity
        + _function(source, "static bool packetCommandNeedsAuthority(")
        + handler
        + SCENARIOS
    )
    executable = cpp.with_suffix("")
    result = subprocess.run(
        [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT),
            "-I",
            str(ROOT / "firmware"),
            str(cpp),
            str(ROOT / "firmware/go2_full/uart/protocol.cpp"),
            "-o",
            str(executable),
        ],
        text=True,
        capture_output=True,
        timeout=30,
    )
    assert result.returncode == 0, result.stderr
    return executable


@pytest.mark.parametrize(
    "scenario",
    ["same-id", "other-id", "bad-id", "malformed", "zero-epoch", "flags", "old-host"],
)
def test_production_handshake_uses_session_not_fixed_robot_pairing(
    handshake_harness, scenario
):
    result = subprocess.run(
        [str(handshake_harness), scenario], text=True, capture_output=True, timeout=5
    )
    assert result.returncode == 0, result.stderr


PREAMBLE = r"""
#include "firmware/go2_full/uart/protocol.h"
#include <cstring>
#include <iostream>
#include <string>
using namespace battlebang::go2_full::uart;
using String = std::string;
struct RuntimeConfig { struct Hit { String robotId = "go2_02"; } hit; } runtimeConfig;
uint32_t jetsonAuthorizedHostEpoch = 0;
int nacks = 0, lastNack = 0, resets = 0, statuses = 0, hpSnapshots = 0;
struct Guard { int clears = 0; void clear() { ++clears; } } hpDamageGuard;
struct ReliableTx { void handleAckFrame(const Frame&) {} } jetsonReliableTx;
void nackJetsonPacket(const Frame&, NackError error) { ++nacks; lastNack = static_cast<int>(error); }
bool replayOrRejectDuplicate(const Frame&) { return false; }
bool staleJetsonActiveCommand(const Frame&) { return false; }
void resetJetsonHostCommandSequence() { ++resets; }
void queueJetsonDeviceStatusPacket() { ++statuses; }
void queueJetsonHpSnapshotPacket() { ++hpSnapshots; }
"""

SCENARIOS = r"""
Frame capabilities(const char* robot, uint32_t epoch) {
  Frame frame;
  frame.type = MessageType::CapabilitiesRequest;
  frame.sender_epoch = epoch;
  const size_t length = std::strlen(robot);
  frame.payload[0] = static_cast<uint8_t>(length);
  std::memcpy(frame.payload + 1, robot, length);
  frame.payload_length = static_cast<uint8_t>(length + 5);
  return frame;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string scenario(argv[1]);
  Frame request = capabilities(scenario == "other-id" ? "go2_09" : "go2_02", 101);
  if (scenario == "bad-id") request.payload[1] = '/';
  if (scenario == "malformed") --request.payload_length;
  if (scenario == "zero-epoch") request.sender_epoch = 0;
  if (scenario == "flags") request.flags = FrameFlags::AckRequired;
  handleJetsonFrame(request);
  if (scenario == "bad-id" || scenario == "malformed" || scenario == "zero-epoch" || scenario == "flags") {
    if (jetsonAuthorizedHostEpoch != 0 || nacks != 1 || statuses != 0 || hpSnapshots != 0) return 3;
    const int expected = static_cast<int>(scenario == "flags" ? NackError::InvalidFlags : NackError::InvalidPayload);
    return lastNack == expected ? 0 : 4;
  }
  if (jetsonAuthorizedHostEpoch != 101 || nacks != 0 || statuses != 1 || hpSnapshots != 1) {
    std::cerr << "valid physically connected host rejected: epoch=" << jetsonAuthorizedHostEpoch
              << " nack=" << lastNack << "\n";
    return 5;
  }
  if (scenario == "old-host") {
    handleJetsonFrame(capabilities("go2_02", 102));
    if (jetsonAuthorizedHostEpoch != 102 || resets != 2 || hpDamageGuard.clears != 2) return 6;
    Frame old;
    old.type = MessageType::FireHold;
    old.flags = FrameFlags::None;
    old.sender_epoch = 101;
    old.payload_length = 3;
    old.payload[0] = static_cast<uint8_t>(CommandSource::Gamepad);
    old.payload[2] = 100;
    handleJetsonFrame(old);
    if (nacks != 1 || lastNack != static_cast<int>(NackError::NotReady)) return 7;
  }
  return 0;
}
"""
