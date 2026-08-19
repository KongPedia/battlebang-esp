#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "go2_nixo_framed_packet_uart/uart/protocol.h"
#include "go2_nixo_framed_packet_uart/uart/runtime.h"

using namespace battlebang::go2_nixo::uart;

namespace {

uint16_t readBe16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t readBe32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

Frame makeFrame(MessageType type, FrameFlags flags, uint16_t sequence, const std::vector<uint8_t>& payload) {
  Frame frame{};
  frame.type = type;
  frame.flags = flags;
  frame.sequence = sequence;
  frame.sender_epoch = 0x01020304;
  frame.payload_length = static_cast<uint8_t>(payload.size());
  if (!payload.empty()) std::memcpy(frame.payload, payload.data(), payload.size());
  return frame;
}


std::vector<uint8_t> parseHexBytes(const std::string& hex) {
  std::vector<uint8_t> bytes;
  std::istringstream input(hex);
  std::string token;
  while (input >> token) bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
  return bytes;
}

std::string fieldValue(const std::string& object, const char* field) {
  const std::string key = std::string("\"") + field + "\": \"";
  const size_t start = object.find(key);
  if (start == std::string::npos) return "";
  const size_t value_start = start + key.size();
  const size_t end = object.find('"', value_start);
  return end == std::string::npos ? "" : object.substr(value_start, end - value_start);
}

struct FixtureVector {
  std::string name;
  std::string expected;
  std::vector<uint8_t> wire;
};

std::vector<FixtureVector> readFixtureVectors() {
  std::ifstream file("tests/fixtures/go2_nixo_uart/golden_vectors.json");
  std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::vector<FixtureVector> vectors;
  size_t pos = 0;
  while ((pos = json.find("\"hex\": \"", pos)) != std::string::npos) {
    const size_t object_start = json.rfind('{', pos);
    const size_t object_end = json.find('}', pos);
    const std::string object = json.substr(object_start, object_end - object_start);
    const std::string name = fieldValue(object, "name");
    std::string expected = fieldValue(object, "expected");
    if (expected.empty()) expected = "valid";
    const std::string hex = fieldValue(object, "hex");
    vectors.push_back({name, expected, parseHexBytes(hex)});
    pos = object_end;
  }
  return vectors;
}

Frame decodeExpected(const std::vector<uint8_t>& wire) {
  Frame frame{};
  EXPECT_EQ(decodeFrame(wire.data(), wire.size(), frame), FrameError::None);
  return frame;
}

}  // namespace


TEST(Go2NixoUartProtocol, SharedFixtureVectorsDecodeAndRoundTrip) {
  const auto vectors = readFixtureVectors();
  ASSERT_EQ(vectors.size(), 20u);
  for (const auto& vector : vectors) {
    Frame frame{};
    const FrameError error = decodeFrame(vector.wire.data(), vector.wire.size(), frame);
    if (vector.expected == "crc_error") {
      EXPECT_EQ(error, FrameError::Crc) << vector.name;
      continue;
    }
    if (vector.expected == "invalid_flags") {
      EXPECT_EQ(error, FrameError::Flags) << vector.name;
      continue;
    }
    ASSERT_EQ(error, FrameError::None) << vector.name;
    if (vector.expected == "invalid_payload") EXPECT_FALSE(isPayloadValid(frame)) << vector.name;
    if (vector.expected == "valid") {
      EXPECT_TRUE(hasValidFlagsForType(frame)) << vector.name;
      EXPECT_TRUE(isPayloadValid(frame)) << vector.name;
    }
    std::array<uint8_t, kMaxFrameBytes> encoded{};
    size_t length = 0;
    ASSERT_EQ(encodeFrame(frame, encoded.data(), encoded.size(), length), FrameError::None) << vector.name;
    EXPECT_TRUE(std::equal(vector.wire.begin(), vector.wire.end(), encoded.begin())) << vector.name;
  }
}

TEST(Go2NixoUartProtocol, CrcMatchesCcittFalseCheckVector) {
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(crc16CcittFalse(data, sizeof(data)), 0x29B1);
}

TEST(Go2NixoUartProtocol, SequenceOrderingHandlesWrapAndRejectsStaleValues) {
  EXPECT_TRUE(isNewerSequence(0x0000, 0xFFFF));
  EXPECT_TRUE(isNewerSequence(0x0001, 0xFFFF));
  EXPECT_FALSE(isNewerSequence(0xFFFF, 0x0000));
  EXPECT_FALSE(isNewerSequence(0x1234, 0x1234));
}

TEST(Go2NixoUartProtocol, EncodesAndDecodesFireStopGoldenVector) {
  const std::array<uint8_t, 16> expected = {
      0xAA, 0x55, 0x02, 0x11, 0x01, 0x00, 0x03, 0x01,
      0x02, 0x03, 0x04, 0x00, 0x01, 0x01, 0x9C, 0x45,
  };
  Frame frame = makeFrame(MessageType::FireStop, FrameFlags::AckRequired, 3, {1});

  std::array<uint8_t, kMaxFrameBytes> wire{};
  size_t length = 0;
  ASSERT_EQ(encodeFrame(frame, wire.data(), wire.size(), length), FrameError::None);
  ASSERT_EQ(length, expected.size());
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), wire.begin()));

  Frame decoded{};
  ASSERT_EQ(decodeFrame(wire.data(), length, decoded), FrameError::None);
  EXPECT_EQ(decoded.sender_epoch, frame.sender_epoch);
  EXPECT_EQ(decoded.type, MessageType::FireStop);
}

TEST(Go2NixoUartProtocol, DeviceStatusMatchesSharedGoldenVector) {
  const std::array<uint8_t, 36> expected = {
      0xAA, 0x55, 0x02, 0x02, 0x00, 0x00, 0x01, 0xA1, 0xB2, 0xC3, 0xD4, 0x00,
      0x15, 0x0B, 0x6E, 0x69, 0x78, 0x6F, 0x5F, 0x67, 0x6F, 0x32, 0x5F, 0x30,
      0x33, 0xA1, 0xB2, 0xC3, 0xD4, 0x00, 0x00, 0x00, 0x6F, 0x02, 0x5A, 0xE9,
  };
  Frame frame{};
  composeDeviceStatus("nixo_go2_03", 0x6F, 0xA1B2C3D4, 1, frame);
  std::array<uint8_t, kMaxFrameBytes> wire{};
  size_t length = 0;
  ASSERT_EQ(encodeFrame(frame, wire.data(), wire.size(), length), FrameError::None);
  ASSERT_EQ(length, expected.size());
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(), wire.begin()));
}

TEST(Go2NixoUartProtocol, LinkMetricsUsesTypedBoundedPayload) {
  Frame frame{};
  composeLinkMetrics(1234, 1, 2, 3, LinkState::Healthy, 0xA1B2C3D4, 9, frame);
  ASSERT_EQ(frame.type, MessageType::LinkMetrics);
  ASSERT_EQ(frame.payload_length, 11);
  EXPECT_EQ(readBe32(frame.payload), 1234u);
  EXPECT_EQ(readBe16(frame.payload + 4), 1u);
  EXPECT_EQ(readBe16(frame.payload + 6), 2u);
  EXPECT_EQ(readBe16(frame.payload + 8), 3u);
  EXPECT_EQ(frame.payload[10], static_cast<uint8_t>(LinkState::Healthy));
}

TEST(Go2NixoUartProtocol, FireStatusPreservesActualStateSourceReasonAndLease) {
  FireSnapshot fire{};
  fire.state = FireState::Spindown;
  fire.inhibited = false;
  fire.source = CommandSource::Autonomy;
  fire.remaining_ms = 175;
  fire.reason = FireReason::HoldTimeout;
  Frame frame{};
  composeFireStatus(fire, 0xA1B2C3D4, 11, frame);

  ASSERT_EQ(frame.type, MessageType::FireStatus);
  ASSERT_EQ(frame.flags, FrameFlags::None);
  ASSERT_EQ(frame.payload_length, 6);
  EXPECT_EQ(frame.payload[0], static_cast<uint8_t>(FireState::Spindown));
  EXPECT_EQ(frame.payload[1], 0u);
  EXPECT_EQ(frame.payload[2], static_cast<uint8_t>(CommandSource::Autonomy));
  EXPECT_EQ(readBe16(frame.payload + 3), 175u);
  EXPECT_EQ(frame.payload[5], static_cast<uint8_t>(FireReason::HoldTimeout));
}

TEST(Go2NixoUartProtocol, ExactFlagsMatchPythonRegistry) {
  EXPECT_TRUE(hasValidFlagsForType(makeFrame(MessageType::FireHold, FrameFlags::None, 1, {1, 0x01, 0x2C})));
  EXPECT_FALSE(hasValidFlagsForType(makeFrame(MessageType::FireHold, FrameFlags::AckRequired, 1, {1, 0x01, 0x2C})));
  EXPECT_TRUE(hasValidFlagsForType(makeFrame(MessageType::HpDamage, FrameFlags::AckRequired, 2, {0, 1, 1})));
  EXPECT_FALSE(hasValidFlagsForType(makeFrame(MessageType::HpDamage, FrameFlags::None, 2, {0, 1, 1})));
  EXPECT_TRUE(hasValidFlagsForType(makeFrame(MessageType::Ack, FrameFlags::Response, 3, {0x11, 0, 3, 0})));
  EXPECT_FALSE(hasValidFlagsForType(makeFrame(MessageType::Ack, FrameFlags::AckRequired, 3, {0x11, 0, 3, 0})));
}

TEST(Go2NixoUartProtocol, IncrementalParserRecoversAfterNoiseAndSplitFrame) {
  Frame frame = makeFrame(MessageType::FireStop, FrameFlags::AckRequired, 3, {1});
  std::array<uint8_t, kMaxFrameBytes> wire{};
  size_t length = 0;
  ASSERT_EQ(encodeFrame(frame, wire.data(), wire.size(), length), FrameError::None);

  IncrementalParser parser;
  const uint8_t noise[] = {'l', 'o', 'g', '\n'};
  size_t count = 0;
  parser.feed(noise, sizeof(noise), 0, [&](const Frame&) { ++count; });
  for (size_t index = 0; index < length; ++index) {
    parser.feed(&wire[index], 1, 1, [&](const Frame& parsed) {
      EXPECT_EQ(parsed.type, MessageType::FireStop);
      ++count;
    });
  }
  EXPECT_EQ(count, 1u);
  EXPECT_LE(parser.bufferedBytes(), kRxBufferBytes);
  EXPECT_EQ(parser.counters().frames, 1u);
}

TEST(Go2NixoUartProtocol, StopIsValidBeforeAnyCapabilityHandshake) {
  Frame stop = makeFrame(MessageType::FireStop, FrameFlags::AckRequired, 7, {1});
  EXPECT_TRUE(isPayloadValid(stop));
  EXPECT_TRUE(hasValidFlagsForType(stop));
}

TEST(Go2NixoUartProtocol, HitEventPayloadMatchesPythonGoldenLayout) {
  Frame hit = makeFrame(MessageType::HitEvent, FrameFlags::AckRequired, 0x1001,
                        {0, 0, 0, 2, 0, 0, 0, 21, 1, 0x01, 0xB6, 0, 0, 0x3B, 0x7E, 0, 12, 0});
  ASSERT_TRUE(isPayloadValid(hit));
  EXPECT_EQ(readBe32(hit.payload + 0), 2u);
  EXPECT_EQ(readBe32(hit.payload + 4), 21u);
  EXPECT_EQ(hit.payload[8], 1u);
  EXPECT_EQ(readBe16(hit.payload + 9), 438u);
  EXPECT_EQ(readBe32(hit.payload + 11), 15230u);
  EXPECT_EQ(readBe16(hit.payload + 15), 12u);
  EXPECT_EQ(hit.payload[17], 0u);
}

TEST(Go2NixoUartProtocol, InboundDedupeReplaysDuplicateHpDamageAndRejectsConflict) {
  InboundDedupe dedupe;
  Frame request = makeFrame(MessageType::HpDamage, FrameFlags::AckRequired, 4, {0, 2, 1});
  Frame response{};
  composeAck(request, AckResult::Applied, response, 0xA1B2C3D4);
  dedupe.remember(request, response, 10);

  Frame cached{};
  EXPECT_EQ(dedupe.check(request, cached, 20), DedupeResult::Duplicate);
  EXPECT_EQ(cached.type, MessageType::Ack);
  EXPECT_EQ(cached.payload[3], static_cast<uint8_t>(AckResult::Applied));

  Frame conflict = request;
  conflict.payload[1] = 3;
  EXPECT_EQ(dedupe.check(conflict, cached, 21), DedupeResult::Conflict);
}


TEST(Go2NixoUartProtocol, ReliableHitEventRetriesAfterFullTxQueue) {
  ReliableFrameTracker reliable;
  FrameTxQueue tx;
  Frame filler = makeFrame(MessageType::DeviceStatus, FrameFlags::None, 1, std::vector<uint8_t>(60, 1));
  while (tx.enqueue(filler)) {}

  Frame hit = makeFrame(MessageType::HitEvent, FrameFlags::AckRequired, 22,
                        {0, 0, 0, 9, 0, 0, 0, 4, 1, 0, 123, 0, 0, 1, 200, 0, 2, 0});
  ASSERT_TRUE(reliable.track(hit, 0));
  EXPECT_EQ(reliable.activeCount(), 1u);

  tx.clear();
  reliable.retryDue(kReliableRetryMs, tx);
  EXPECT_GT(tx.pendingBytes(), 0u);
}

TEST(Go2NixoUartProtocol, ReliableHitEventRetriesUntilAck) {
  ReliableFrameTracker reliable;
  FrameTxQueue tx;
  Frame hit = makeFrame(MessageType::HitEvent, FrameFlags::AckRequired, 22,
                        {0, 0, 0, 9, 0, 0, 0, 4, 1, 0, 123, 0, 0, 1, 200, 0, 2, 0});
  ASSERT_TRUE(reliable.track(hit, 0));
  EXPECT_EQ(reliable.activeCount(), 1u);

  reliable.retryDue(kReliableRetryMs, tx);
  EXPECT_GT(tx.pendingBytes(), 0u);

  Frame ack{};
  composeAck(hit, AckResult::Applied, ack, 0xA1B2C3D4);
  EXPECT_TRUE(reliable.handleAckFrame(ack));
  EXPECT_EQ(reliable.activeCount(), 0u);
  tx.clear();
  reliable.retryDue(kReliableRetryMs * 2, tx);
  EXPECT_EQ(tx.pendingBytes(), 0u);
}

TEST(Go2NixoUartProtocol, ReliableTrackerRejectsAdmissionWhenCapacityIsFull) {
  ReliableFrameTracker reliable;
  for (size_t index = 0; index < kReliableEntryCount; ++index) {
    Frame hit = makeFrame(MessageType::HitEvent, FrameFlags::AckRequired, static_cast<uint16_t>(index + 1),
                          {0, 0, 0, 9, 0, 0, 0, 4, 1, 0, 123, 0, 0, 1, 200, 0, 2, 0});
    ASSERT_TRUE(reliable.track(hit, 0));
  }
  Frame overflow = makeFrame(MessageType::HitEvent, FrameFlags::AckRequired, 99,
                             {0, 0, 0, 9, 0, 0, 0, 4, 1, 0, 123, 0, 0, 1, 200, 0, 2, 0});
  EXPECT_FALSE(reliable.track(overflow, 0));
  EXPECT_EQ(reliable.activeCount(), kReliableEntryCount);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
