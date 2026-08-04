#include "go2_nixo/serial/protocol.h"
#include "go2_nixo/serial/runtime.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using go2::serial::Frame;
using go2::serial::FrameError;
using go2::serial::FrameFlags;
using go2::serial::IncrementalParser;
using go2::serial::MessageType;
using go2::serial::ProductionSession;

#define CHECK(condition)                                                                          \
  do {                                                                                            \
    if (!(condition)) {                                                                           \
      std::cerr << "check failed at line " << __LINE__ << ": " #condition << '\n';             \
      return 1;                                                                                   \
    }                                                                                             \
  } while (false)

namespace {

std::vector<uint8_t> hexBytes(const char* text) {
  const std::string hex(text);
  if (hex.size() % 2 != 0) return {};
  std::vector<uint8_t> bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(hex.substr(i, 2).c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || value > 0xFF) return {};
    bytes.push_back(static_cast<uint8_t>(value));
  }
  return bytes;
}

Frame decode(const std::vector<uint8_t>& wire) {
  Frame frame;
  if (go2::serial::decodeFrame(wire.data(), wire.size(), frame) != FrameError::None) std::abort();
  return frame;
}

void repairCrc(std::vector<uint8_t>& wire) {
  const uint16_t crc = go2::serial::crc16CcittFalse(wire.data() + 2, wire.size() - 4);
  wire[wire.size() - 2] = static_cast<uint8_t>(crc >> 8);
  wire[wire.size() - 1] = static_cast<uint8_t>(crc);
}

struct Collector {
  std::vector<Frame> frames;

  static void receive(const Frame& frame, void* context) {
    static_cast<Collector*>(context)->frames.push_back(frame);
  }
};

struct DiagnosticCollector {
  size_t replies = 0;
  std::vector<uint8_t> last_reply;

  static void receive(const Frame& request, void* context) {
    DiagnosticCollector& collector = *static_cast<DiagnosticCollector*>(context);
    Frame reply;
    if (!go2::serial::composeDiagEchoReply(request, reply)) return;
    uint8_t wire[go2::serial::kMaxFrameBytes] = {};
    size_t length = 0;
    if (go2::serial::encodeFrame(reply, wire, sizeof(wire), length) != FrameError::None) return;
    ++collector.replies;
    collector.last_reply.assign(wire, wire + length);
  }
};

void append(std::vector<uint8_t>& output, const std::vector<uint8_t>& input) {
  output.insert(output.end(), input.begin(), input.end());
}

Frame makeFrame(MessageType type,
                go2::serial::FrameFlags flags,
                uint16_t sequence,
                uint32_t session_id,
                const std::vector<uint8_t>& payload) {
  Frame frame;
  frame.type = type;
  frame.flags = flags;
  frame.sequence = sequence;
  frame.session_id = session_id;
  frame.payload_length = static_cast<uint8_t>(payload.size());
  if (!payload.empty()) std::memcpy(frame.payload, payload.data(), payload.size());
  return frame;
}

std::vector<Frame> drainTx(go2::serial::FrameTxQueue& tx) {
  std::vector<uint8_t> wire;
  while (tx.pendingBytes() != 0) {
    size_t length = 0;
    const uint8_t* bytes = tx.peek(length);
    if (bytes == nullptr || length == 0) std::abort();
    wire.insert(wire.end(), bytes, bytes + length);
    tx.consume(length);
  }
  std::vector<Frame> frames;
  size_t offset = 0;
  while (offset < wire.size()) {
    if (wire.size() - offset < go2::serial::kMinFrameBytes) std::abort();
    const size_t payload_length = (static_cast<size_t>(wire[offset + 11]) << 8) |
                                  wire[offset + 12];
    const size_t frame_length = go2::serial::kMinFrameBytes + payload_length;
    if (offset + frame_length > wire.size()) std::abort();
    Frame frame;
    if (go2::serial::decodeFrame(wire.data() + offset, frame_length, frame) != FrameError::None) {
      std::abort();
    }
    frames.push_back(frame);
    offset += frame_length;
  }
  return frames;
}

const Frame* findType(const std::vector<Frame>& frames, MessageType type) {
  for (size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].type == type) return &frames[i];
  }
  return nullptr;
}

struct FakeRuntime {
  int fire_holds = 0;
  int fire_stops = 0;
  int hp_resets = 0;
  int link_losses = 0;
  bool firing = false;
  bool inhibited = false;
  go2::serial::FireReason last_link_reason = go2::serial::FireReason::None;
  go2::serial::HpSnapshot hp;

  static uint8_t fireHold(go2::serial::CommandSource, uint32_t, void* context) {
    FakeRuntime& fake = *static_cast<FakeRuntime*>(context);
    if (fake.inhibited) return static_cast<uint8_t>(go2::serial::NackError::Inhibited);
    ++fake.fire_holds;
    fake.firing = true;
    return 0;
  }

  static go2::serial::AckResult fireStop(uint8_t, uint32_t, void* context) {
    FakeRuntime& fake = *static_cast<FakeRuntime*>(context);
    ++fake.fire_stops;
    const bool was_firing = fake.firing;
    fake.firing = false;
    return was_firing ? go2::serial::AckResult::Applied
                      : go2::serial::AckResult::NoopAlreadySafe;
  }

  static uint8_t hpReset(uint8_t, uint32_t, void* context) {
    FakeRuntime& fake = *static_cast<FakeRuntime*>(context);
    ++fake.hp_resets;
    ++fake.hp.revision;
    fake.hp.remaining = fake.hp.maximum;
    fake.hp.accepted_hits = 0;
    fake.hp.down = false;
    return 0;
  }

  static void linkLost(go2::serial::FireReason reason, uint32_t, void* context) {
    FakeRuntime& fake = *static_cast<FakeRuntime*>(context);
    ++fake.link_losses;
    fake.last_link_reason = reason;
    fake.firing = false;
  }

  static go2::serial::HpSnapshot hpSnapshot(void* context) {
    return static_cast<FakeRuntime*>(context)->hp;
  }

  static go2::serial::FireSnapshot fireSnapshot(uint32_t, void* context) {
    FakeRuntime& fake = *static_cast<FakeRuntime*>(context);
    go2::serial::FireSnapshot fire;
    fire.state = fake.firing ? go2::serial::FireState::Firing : go2::serial::FireState::Idle;
    fire.inhibited = fake.inhibited;
    fire.source = fake.firing ? go2::serial::CommandSource::Gamepad
                              : go2::serial::CommandSource::Unknown;
    fire.remaining_ms = fake.firing ? 1000 : 0;
    return fire;
  }

  go2::serial::SessionCallbacks callbacks() {
    go2::serial::SessionCallbacks value;
    value.context = this;
    value.fire_hold = fireHold;
    value.fire_stop = fireStop;
    value.hp_reset = hpReset;
    value.link_lost = linkLost;
    value.hp_snapshot = hpSnapshot;
    value.fire_snapshot = fireSnapshot;
    return value;
  }
};

struct SessionFeedContext {
  ProductionSession* session = nullptr;
  uint32_t now = 0;

  static void receive(const Frame& frame, void* context) {
    SessionFeedContext& feed = *static_cast<SessionFeedContext*>(context);
    feed.session->handleFrame(frame, feed.now);
  }
};

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 19);
  std::vector<std::vector<uint8_t> > vectors;
  for (int i = 1; i < argc; ++i) vectors.push_back(hexBytes(argv[i]));

  const MessageType expected_types[] = {
      MessageType::Connect,      MessageType::Connected,      MessageType::FireStop,
      MessageType::Ack,        MessageType::HpStatus,      MessageType::HitEvent,
      MessageType::DiagEcho,   MessageType::DiagEchoReply,
  };
  for (size_t i = 0; i < 8; ++i) {
    const Frame frame = decode(vectors[i]);
    CHECK(frame.type == expected_types[i]);
    CHECK(go2::serial::isPayloadValid(frame));
    uint8_t encoded[go2::serial::kMaxFrameBytes] = {};
    size_t encoded_length = 0;
    CHECK(go2::serial::encodeFrame(frame, encoded, sizeof(encoded), encoded_length) == FrameError::None);
    CHECK(encoded_length == vectors[i].size());
    CHECK(std::memcmp(encoded, vectors[i].data(), encoded_length) == 0);
  }

  CHECK(go2::serial::crc16CcittFalse(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0x29B1);
  CHECK(go2::serial::nextSequence(0xFFFF) == 0);
  CHECK(go2::serial::isNewerSequence(0, 0xFFFF));
  CHECK(!go2::serial::isNewerSequence(0xFFFF, 0));
  CHECK(!go2::serial::isNewerSequence(7, 7));
  CHECK(!go2::serial::isNewerSequence(0x8000, 0));

  const std::vector<uint8_t>& connected = vectors[1];
  IncrementalParser one_byte_parser;
  Collector one_byte;
  for (size_t i = 0; i < connected.size(); ++i) {
    one_byte_parser.feed(&connected[i], 1, 0, Collector::receive, &one_byte);
  }
  CHECK(one_byte.frames.size() == 1);
  CHECK(one_byte.frames[0].type == MessageType::Connected);

  IncrementalParser random_parser;
  Collector random_frames;
  std::mt19937 randomizer(815);
  size_t offset = 0;
  while (offset < connected.size()) {
    const size_t chunk = 1 + randomizer() % 7;
    const size_t length = chunk < connected.size() - offset ? chunk : connected.size() - offset;
    random_parser.feed(connected.data() + offset, length, 0, Collector::receive, &random_frames);
    offset += length;
  }
  CHECK(random_frames.frames.size() == 1);

  std::vector<uint8_t> coalesced = hexBytes("626F6F74206C6F670D0A00");
  append(coalesced, vectors[6]);
  append(coalesced, vectors[7]);
  IncrementalParser coalesced_parser;
  Collector coalesced_frames;
  coalesced_parser.feed(coalesced.data(), coalesced.size(), 0, Collector::receive, &coalesced_frames);
  CHECK(coalesced_frames.frames.size() == 2);
  CHECK(coalesced_parser.counters().discarded_bytes == 11);

  std::vector<uint8_t> oversize = hexBytes("AA5501F0010001010203040041");
  append(oversize, vectors[2]);
  IncrementalParser oversize_parser;
  Collector oversize_frames;
  oversize_parser.feed(oversize.data(), oversize.size(), 0, Collector::receive, &oversize_frames);
  CHECK(oversize_frames.frames.size() == 1);
  CHECK(oversize_frames.frames[0].type == MessageType::FireStop);
  CHECK(oversize_parser.counters().length_errors == 1);

  IncrementalParser timeout_parser;
  Collector timeout_frames;
  timeout_parser.feed(vectors[2].data(), 10, 1000, Collector::receive, &timeout_frames);
  timeout_parser.feed(nullptr, 0, 1051, Collector::receive, &timeout_frames);
  timeout_parser.feed(vectors[2].data(), vectors[2].size(), 1052, Collector::receive, &timeout_frames);
  CHECK(timeout_frames.frames.size() == 1);
  CHECK(timeout_parser.counters().timeout_errors == 1);

  std::vector<uint8_t> bad_version = vectors[2];
  bad_version[2] = 2;
  repairCrc(bad_version);
  std::vector<uint8_t> bad_type = vectors[2];
  bad_type[3] = 0x55;
  repairCrc(bad_type);
  std::vector<uint8_t> invalids = vectors[12];
  append(invalids, bad_version);
  append(invalids, vectors[13]);
  append(invalids, bad_type);
  append(invalids, vectors[2]);
  IncrementalParser recovery_parser;
  Collector recovered;
  recovery_parser.feed(invalids.data(), invalids.size(), 0, Collector::receive, &recovered);
  CHECK(recovered.frames.size() == 1);
  CHECK(recovery_parser.counters().crc_errors == 1);
  CHECK(recovery_parser.counters().version_errors == 1);
  CHECK(recovery_parser.counters().flag_errors == 1);
  CHECK(recovery_parser.counters().type_errors == 1);

  std::vector<uint8_t> overflow(600, static_cast<uint8_t>('x'));
  append(overflow, vectors[7]);
  IncrementalParser overflow_parser;
  Collector overflow_frames;
  overflow_parser.feed(overflow.data(), overflow.size(), 0, Collector::receive, &overflow_frames);
  CHECK(overflow_frames.frames.size() == 1);
  CHECK(overflow_parser.bufferedBytes() <= go2::serial::kRxBufferBytes);
  CHECK(overflow_parser.counters().overflow_errors == 1);

  IncrementalParser split_magic_parser;
  Collector split_magic_frames;
  const std::vector<uint8_t> noise_and_magic = hexBytes("6E6F697365AA");
  split_magic_parser.feed(noise_and_magic.data(), noise_and_magic.size(), 0,
                          Collector::receive, &split_magic_frames);
  split_magic_parser.feed(vectors[6].data() + 1, vectors[6].size() - 1, 0,
                          Collector::receive, &split_magic_frames);
  CHECK(split_magic_frames.frames.size() == 1);
  CHECK(split_magic_frames.frames[0].type == MessageType::DiagEcho);

  const Frame empty_payload = decode(vectors[8]);
  const Frame max_payload = decode(vectors[9]);
  CHECK(empty_payload.payload_length == 0);
  CHECK(max_payload.payload_length == 64);
  CHECK(decode(vectors[10]).sequence == 0xFFFF);
  CHECK(decode(vectors[11]).sequence == 0);
  Frame ignored;
  CHECK(go2::serial::decodeFrame(vectors[12].data(), vectors[12].size(), ignored) == FrameError::Crc);
  CHECK(go2::serial::decodeFrame(vectors[13].data(), vectors[13].size(), ignored) == FrameError::Flags);
  CHECK(!go2::serial::isPayloadValid(decode(vectors[14])));
  const Frame same_a = decode(vectors[15]);
  const Frame same_b = decode(vectors[16]);
  CHECK(same_a.sequence == same_b.sequence);
  CHECK(same_a.payload[0] != same_b.payload[0]);
  CHECK(decode(vectors[17]).session_id == 0x01020303);

  std::vector<uint8_t> no_side_effect = vectors[12];
  append(no_side_effect, vectors[13]);
  append(no_side_effect, vectors[14]);
  append(no_side_effect, vectors[2]);
  IncrementalParser diagnostic_parser;
  DiagnosticCollector diagnostic;
  diagnostic_parser.feed(no_side_effect.data(), no_side_effect.size(), 0,
                         DiagnosticCollector::receive, &diagnostic);
  CHECK(diagnostic.replies == 0);
  diagnostic_parser.feed(vectors[6].data(), vectors[6].size(), 1,
                         DiagnosticCollector::receive, &diagnostic);
  CHECK(diagnostic.replies == 1);
  CHECK(diagnostic.last_reply == vectors[7]);

  FakeRuntime fake;
  fake.hp.revision = 21;
  fake.hp.remaining = 12;
  fake.hp.maximum = 14;
  fake.hp.accepted_hits = 2;
  fake.hp.last_hit_sequence = 2;
  ProductionSession session;
  session.begin("nixo_go2_03",
                "go2_03",
                0xA1B2C3D4,
                go2::serial::CapabilityFireControl | go2::serial::CapabilityHpStatus |
                    go2::serial::CapabilityHitEvent | go2::serial::CapabilityLinkStatus,
                fake.callbacks(),
                10);
  const Frame connect = decode(vectors[0]);
  session.handleFrame(connect, 10);
  CHECK(session.connected());
  CHECK(session.sessionId() == 0x01020304);
  std::vector<Frame> outgoing = drainTx(session.tx());
  const Frame* connected_response = findType(outgoing, MessageType::Connected);
  CHECK(connected_response != nullptr);
  CHECK(connected_response->payload_length == 21);
  CHECK(connected_response->payload[0] == 11);
  CHECK(std::memcmp(connected_response->payload + 1, "nixo_go2_03", 11) == 0);
  CHECK(findType(outgoing, MessageType::HpStatus) != nullptr);
  CHECK(findType(outgoing, MessageType::FireStatus) != nullptr);

  session.handleFrame(connect, 11);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1);
  CHECK(outgoing[0].type == MessageType::Connected);
  CHECK(fake.link_losses == 0);

  const Frame fire_hold = makeFrame(MessageType::FireHold,
                                    FrameFlags::None,
                                    2,
                                    session.sessionId(),
                                    std::vector<uint8_t>{1});
  session.handleFrame(fire_hold, 20);
  CHECK(fake.fire_holds == 1);
  CHECK(findType(drainTx(session.tx()), MessageType::FireStatus) != nullptr);
  session.handleFrame(fire_hold, 21);
  CHECK(fake.fire_holds == 1);
  CHECK(drainTx(session.tx()).empty());

  Frame conflict = fire_hold;
  conflict.payload[0] = 2;
  session.handleFrame(conflict, 22);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Nack);
  CHECK(outgoing[0].payload[3] ==
        static_cast<uint8_t>(go2::serial::NackError::SequenceConflict));
  CHECK(fake.fire_holds == 1);

  const Frame out_of_order = makeFrame(MessageType::FireHold,
                                       FrameFlags::None,
                                       0,
                                       session.sessionId(),
                                       std::vector<uint8_t>{1});
  session.handleFrame(out_of_order, 23);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Nack);
  CHECK(outgoing[0].payload[3] == static_cast<uint8_t>(go2::serial::NackError::OutOfOrder));
  CHECK(fake.fire_holds == 1);

  Frame wrong_session = fire_hold;
  wrong_session.sequence = 3;
  wrong_session.session_id = 0x01020303;
  session.handleFrame(wrong_session, 24);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Nack);
  CHECK(outgoing[0].payload[3] ==
        static_cast<uint8_t>(go2::serial::NackError::SessionMismatch));
  CHECK(fake.fire_holds == 1);

  const Frame bad_fire_flags = makeFrame(MessageType::FireStop,
                                         FrameFlags::None,
                                         3,
                                         session.sessionId(),
                                         std::vector<uint8_t>{1});
  session.handleFrame(bad_fire_flags, 25);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Nack);
  CHECK(outgoing[0].payload[3] == static_cast<uint8_t>(go2::serial::NackError::InvalidFlags));
  CHECK(fake.fire_stops == 0);

  const Frame bad_fire_payload = makeFrame(MessageType::FireStop,
                                           FrameFlags::AckRequired,
                                           3,
                                           session.sessionId(),
                                           std::vector<uint8_t>{1, 2});
  session.handleFrame(bad_fire_payload, 26);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Nack);
  CHECK(outgoing[0].payload[3] ==
        static_cast<uint8_t>(go2::serial::NackError::InvalidPayload));
  CHECK(fake.fire_stops == 0);

  const Frame fire_stop = makeFrame(MessageType::FireStop,
                                    FrameFlags::AckRequired,
                                    3,
                                    session.sessionId(),
                                    std::vector<uint8_t>{1});
  session.handleFrame(fire_stop, 30);
  outgoing = drainTx(session.tx());
  CHECK(fake.fire_stops == 1);
  const Frame* stop_ack = findType(outgoing, MessageType::Ack);
  CHECK(stop_ack != nullptr && stop_ack->payload[3] == 0);
  session.handleFrame(fire_stop, 31);
  outgoing = drainTx(session.tx());
  CHECK(fake.fire_stops == 1);
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Ack);
  CHECK(outgoing[0].payload[3] ==
        static_cast<uint8_t>(go2::serial::AckResult::Duplicate));
  session.tick(30);
  CHECK(session.connected());
  CHECK(fake.link_losses == 0);

  const Frame hp_reset = makeFrame(MessageType::HpReset,
                                   FrameFlags::AckRequired,
                                   4,
                                   session.sessionId(),
                                   std::vector<uint8_t>{1});
  session.handleFrame(hp_reset, 40);
  outgoing = drainTx(session.tx());
  CHECK(fake.hp_resets == 1);
  CHECK(findType(outgoing, MessageType::Ack) != nullptr);
  const Frame* reset_hp = findType(outgoing, MessageType::HpStatus);
  CHECK(reset_hp != nullptr);
  CHECK(reset_hp->payload[3] == 22);
  session.handleFrame(hp_reset, 41);
  outgoing = drainTx(session.tx());
  CHECK(fake.hp_resets == 1);
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::Ack);
  CHECK(outgoing[0].payload[3] ==
        static_cast<uint8_t>(go2::serial::AckResult::Duplicate));

  Frame reconnect = connect;
  reconnect.sequence = 0xFFFE;
  reconnect.session_id = 0xAABBCCDD;
  session.handleFrame(reconnect, 100);
  CHECK(session.connected());
  CHECK(session.sessionId() == 0xAABBCCDD);
  CHECK(fake.link_losses == 1);
  CHECK(fake.last_link_reason == go2::serial::FireReason::SessionChanged);
  drainTx(session.tx());

  const Frame peer_link = makeFrame(MessageType::LinkStatus,
                                    FrameFlags::None,
                                    0xFFFF,
                                    session.sessionId(),
                                    std::vector<uint8_t>{0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1});
  session.handleFrame(peer_link, 101);
  CHECK(drainTx(session.tx()).empty());
  Frame wrapped_hold = fire_hold;
  wrapped_hold.sequence = 0;
  wrapped_hold.session_id = session.sessionId();
  session.handleFrame(wrapped_hold, 102);
  CHECK(fake.fire_holds == 2);
  drainTx(session.tx());

  go2::serial::HitSnapshot hit;
  hit.hit_sequence = 9;
  hit.hp_revision = 22;
  hit.sensor_id = 1;
  hit.strength = 2470;
  hit.timestamp_ms = 200;
  hit.hp_remaining = 11;
  CHECK(session.notifyHit(hit, 200));
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1 && outgoing[0].type == MessageType::HitEvent);
  const Frame first_hit_wire = outgoing[0];
  session.tick(299);
  CHECK(drainTx(session.tx()).empty());
  session.tick(300);
  outgoing = drainTx(session.tx());
  CHECK(outgoing.size() == 1);
  CHECK(outgoing[0].type == MessageType::HitEvent);
  CHECK(outgoing[0].sequence == first_hit_wire.sequence);
  CHECK(std::memcmp(outgoing[0].payload, first_hit_wire.payload, first_hit_wire.payload_length) == 0);
  CHECK(session.counters().reliable_retries == 1);

  const Frame hit_ack = makeFrame(MessageType::Ack,
                                  FrameFlags::Response,
                                  first_hit_wire.sequence,
                                  session.sessionId(),
                                  std::vector<uint8_t>{
                                      static_cast<uint8_t>(MessageType::HitEvent),
                                      static_cast<uint8_t>(first_hit_wire.sequence >> 8),
                                      static_cast<uint8_t>(first_hit_wire.sequence),
                                      static_cast<uint8_t>(go2::serial::AckResult::Applied)});
  session.handleFrame(hit_ack, 301);
  session.tick(400);
  CHECK(drainTx(session.tx()).empty());
  session.tick(600);
  outgoing = drainTx(session.tx());
  CHECK(findType(outgoing, MessageType::LinkStatus) != nullptr);
  const Frame* periodic_hp = findType(outgoing, MessageType::HpStatus);
  CHECK(periodic_hp != nullptr);
  CHECK(periodic_hp->payload_length == 15);
  CHECK(periodic_hp->payload[3] == 22);

  session.tick(1801);
  CHECK(!session.connected());
  CHECK(session.state() == go2::serial::LinkState::Stale);
  CHECK(fake.link_losses == 2);
  CHECK(fake.last_link_reason == go2::serial::FireReason::LinkStale);

  reconnect.sequence = 10;
  reconnect.session_id = 0x11223344;
  session.handleFrame(reconnect, 2000);
  CHECK(session.connected());
  drainTx(session.tx());
  session.parserFault(2001);
  CHECK(!session.connected());
  CHECK(session.state() == go2::serial::LinkState::Fault);
  CHECK(fake.link_losses == 3);
  CHECK(fake.last_link_reason == go2::serial::FireReason::InternalFault);

  CHECK(!go2::serial::isFireHoldExpired(false, false, 300, 300));
  CHECK(!go2::serial::isFireHoldExpired(true, false, 300, 299));
  CHECK(go2::serial::isFireHoldExpired(true, false, 300, 300));
  CHECK(go2::serial::isFireHoldExpired(false, true, 300, 301));

  go2::serial::FrameTxQueue bounded_tx;
  Frame largest = max_payload;
  size_t enqueued = 0;
  while (bounded_tx.enqueue(largest)) ++enqueued;
  CHECK(enqueued > 0);
  CHECK(bounded_tx.pendingBytes() <= go2::serial::kTxBufferBytes);
  CHECK(bounded_tx.overflowErrors() == 1);
  const size_t pending_before_partial = bounded_tx.pendingBytes();
  bounded_tx.consume(3);
  CHECK(bounded_tx.pendingBytes() == pending_before_partial - 3);

  FakeRuntime parser_fake;
  parser_fake.hp.maximum = 14;
  ProductionSession parser_session;
  parser_session.begin("nixo_go2_03",
                       "go2_03",
                       7,
                       go2::serial::CapabilityFireControl,
                       parser_fake.callbacks(),
                       0);
  parser_session.handleFrame(connect, 0);
  drainTx(parser_session.tx());
  Frame parser_fire = makeFrame(MessageType::FireHold,
                                FrameFlags::None,
                                2,
                                parser_session.sessionId(),
                                std::vector<uint8_t>{1});
  uint8_t parser_fire_wire[go2::serial::kMaxFrameBytes] = {};
  size_t parser_fire_length = 0;
  CHECK(go2::serial::encodeFrame(parser_fire,
                                 parser_fire_wire,
                                 sizeof(parser_fire_wire),
                                 parser_fire_length) == FrameError::None);
  parser_fire_wire[parser_fire_length - 1] ^= 1;
  IncrementalParser production_parser;
  SessionFeedContext feed;
  feed.session = &parser_session;
  production_parser.feed(parser_fire_wire,
                         parser_fire_length,
                         1,
                         SessionFeedContext::receive,
                         &feed);
  CHECK(parser_fake.fire_holds == 0);
  CHECK(production_parser.counters().crc_errors == 1);

  std::cout << "go2_nixo_serial host checks passed\n";
  return 0;
}
