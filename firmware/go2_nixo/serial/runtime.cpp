#include "go2_nixo/serial/runtime.h"

#include <string.h>

namespace go2 {
namespace serial {
namespace {

uint16_t readBe16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

void writeBe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value >> 8);
  data[1] = static_cast<uint8_t>(value);
}

void writeBe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value >> 24);
  data[1] = static_cast<uint8_t>(value >> 16);
  data[2] = static_cast<uint8_t>(value >> 8);
  data[3] = static_cast<uint8_t>(value);
}

}  // namespace

bool isFireHoldExpired(bool hold_active,
                       bool release_required,
                       uint32_t deadline_ms,
                       uint32_t now_ms) {
  return (hold_active || release_required) &&
         static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

bool FrameTxQueue::enqueue(const Frame& frame) {
  uint8_t wire[kMaxFrameBytes] = {};
  size_t wire_length = 0;
  if (encodeFrame(frame, wire, sizeof(wire), wire_length) != FrameError::None ||
      wire_length > kTxBufferBytes - size_) {
    ++overflow_errors_;
    return false;
  }
  const size_t tail = (head_ + size_) % kTxBufferBytes;
  const size_t first = wire_length < kTxBufferBytes - tail ? wire_length : kTxBufferBytes - tail;
  memcpy(buffer_ + tail, wire, first);
  memcpy(buffer_, wire + first, wire_length - first);
  size_ += wire_length;
  return true;
}

const uint8_t* FrameTxQueue::peek(size_t& length) const {
  length = size_ < kTxBufferBytes - head_ ? size_ : kTxBufferBytes - head_;
  return length == 0 ? nullptr : buffer_ + head_;
}

void FrameTxQueue::consume(size_t length) {
  if (length >= size_) {
    head_ = 0;
    size_ = 0;
    return;
  }
  head_ = (head_ + length) % kTxBufferBytes;
  size_ -= length;
}

void FrameTxQueue::clear() {
  head_ = 0;
  size_ = 0;
}

namespace {

bool sameFrame(const Frame& left, const Frame& right) {
  return left.type == right.type && left.flags == right.flags &&
         left.sequence == right.sequence && left.session_id == right.session_id &&
         left.payload_length == right.payload_length &&
         memcmp(left.payload, right.payload, left.payload_length) == 0;
}

void copyProtocolId(const char* source, char* destination, size_t capacity, const char* fallback) {
  const char* value = source != nullptr && source[0] != '\0' ? source : fallback;
  size_t length = strlen(value);
  if (length >= capacity) length = capacity - 1;
  memcpy(destination, value, length);
  destination[length] = '\0';
}

uint16_t saturate16(uint32_t value) {
  return value > 0xFFFFU ? 0xFFFFU : static_cast<uint16_t>(value);
}

}  // namespace

void ProductionSession::begin(const char* device_id,
                              const char* expected_robot_id,
                              uint32_t esp_boot_id,
                              uint32_t capabilities,
                              const SessionCallbacks& callbacks,
                              uint32_t now_ms) {
  copyProtocolId(device_id, device_id_, sizeof(device_id_), "go2_nixo");
  copyProtocolId(expected_robot_id, expected_robot_id_, sizeof(expected_robot_id_), "");
  esp_boot_id_ = esp_boot_id == 0 ? 1 : esp_boot_id;
  capabilities_ = capabilities;
  callbacks_ = callbacks;
  counters_ = SessionCounters();
  state_ = LinkState::Disconnected;
  connected_ = false;
  session_id_ = 0;
  last_rx_ms_ = now_ms;
  last_link_status_ms_ = now_ms;
  last_hp_status_ms_ = now_ms;
  last_fire_status_ms_ = now_ms;
  transport_crc_errors_ = 0;
  transport_overflow_errors_ = 0;
  hp_dirty_ = true;
  fire_dirty_ = true;
  next_tx_sequence_ = 0;
  clearSessionData();
}

bool ProductionSession::queueFrame(const Frame& frame) {
  return tx_.enqueue(frame);
}

uint16_t ProductionSession::takeSequence() {
  const uint16_t sequence = next_tx_sequence_;
  next_tx_sequence_ = nextSequence(next_tx_sequence_);
  return sequence;
}

void ProductionSession::clearSessionData() {
  tx_.clear();
  has_last_sequence_ = false;
  last_sequence_ = 0;
  next_dedupe_entry_ = 0;
  for (size_t i = 0; i < kDedupeEntryCount; ++i) dedupe_[i].active = false;
  reliable_head_ = 0;
  reliable_count_ = 0;
  for (size_t i = 0; i < kReliableEntryCount; ++i) reliable_[i].active = false;
}

void ProductionSession::loseLink(LinkState state, FireReason reason, uint32_t now_ms) {
  const bool notify = connected_ || state_ != state;
  connected_ = false;
  state_ = state;
  clearSessionData();
  if (notify && callbacks_.link_lost != nullptr) {
    callbacks_.link_lost(reason, now_ms, callbacks_.context);
  }
}

void ProductionSession::queueConnected(const Frame& connect, Frame& response) {
  response = Frame();
  response.type = MessageType::Connected;
  response.flags = FrameFlags::Response;
  response.sequence = connect.sequence;
  response.session_id = connect.session_id;
  const size_t id_length = strlen(device_id_);
  response.payload[0] = static_cast<uint8_t>(id_length);
  memcpy(response.payload + 1, device_id_, id_length);
  writeBe32(response.payload + 1 + id_length, esp_boot_id_);
  writeBe32(response.payload + 5 + id_length, capabilities_);
  response.payload[9 + id_length] = kFrameVersion;
  response.payload_length = static_cast<uint8_t>(10 + id_length);
  queueFrame(response);
}

void ProductionSession::handleConnect(const Frame& frame, uint32_t now_ms) {
  if (connected_ && frame.session_id == session_id_ &&
      rejectDuplicateOrOutOfOrder(frame, now_ms)) {
    return;
  }

  const size_t id_length = frame.payload[0];
  if (expected_robot_id_[0] != '\0' &&
      (strlen(expected_robot_id_) != id_length ||
       memcmp(frame.payload + 1, expected_robot_id_, id_length) != 0)) {
    sendNack(frame, NackError::IdentityMismatch);
    return;
  }

  if (connected_) {
    loseLink(LinkState::Stale, FireReason::SessionChanged, now_ms);
  } else {
    clearSessionData();
  }
  session_id_ = frame.session_id;
  state_ = LinkState::Connected;
  connected_ = true;
  last_rx_ms_ = now_ms;
  last_link_status_ms_ = now_ms;
  last_hp_status_ms_ = now_ms;
  last_fire_status_ms_ = now_ms;
  hp_dirty_ = true;
  fire_dirty_ = true;

  Frame response;
  queueConnected(frame, response);
  rememberRequest(frame, &response, now_ms);
  queueHpStatus(now_ms);
  queueFireStatus(now_ms);
}

void ProductionSession::sendAck(const Frame& request, AckResult result, Frame* cached) {
  Frame response;
  response.type = MessageType::Ack;
  response.flags = FrameFlags::Response;
  response.sequence = request.sequence;
  response.session_id = request.session_id;
  response.payload_length = 4;
  response.payload[0] = static_cast<uint8_t>(request.type);
  writeBe16(response.payload + 1, request.sequence);
  response.payload[3] = static_cast<uint8_t>(result);
  queueFrame(response);
  if (cached != nullptr) *cached = response;
}

void ProductionSession::sendNack(const Frame& request, NackError error) {
  Frame response;
  response.type = MessageType::Nack;
  response.flags = FrameFlags::Response;
  response.sequence = request.sequence;
  response.session_id = request.session_id;
  response.payload_length = 4;
  response.payload[0] = static_cast<uint8_t>(request.type);
  writeBe16(response.payload + 1, request.sequence);
  response.payload[3] = static_cast<uint8_t>(error);
  queueFrame(response);
}

bool ProductionSession::rejectDuplicateOrOutOfOrder(const Frame& frame, uint32_t now_ms) {
  for (size_t i = 0; i < kDedupeEntryCount; ++i) {
    DedupeEntry& entry = dedupe_[i];
    if (!entry.active || static_cast<uint32_t>(now_ms - entry.stored_ms) >= kDedupeExpiryMs ||
        entry.request.sequence != frame.sequence) {
      continue;
    }
    if (!sameFrame(entry.request, frame)) {
      ++counters_.sequence_conflicts;
      sendNack(frame, NackError::SequenceConflict);
      return true;
    }
    if (entry.has_response) {
      if (entry.response.type == MessageType::Ack) {
        sendAck(frame, AckResult::Duplicate);
      } else {
        queueFrame(entry.response);
      }
    }
    last_rx_ms_ = now_ms;
    return true;
  }
  if (has_last_sequence_ && !isNewerSequence(frame.sequence, last_sequence_)) {
    ++counters_.out_of_order;
    sendNack(frame, NackError::OutOfOrder);
    return true;
  }
  return false;
}

void ProductionSession::rememberRequest(const Frame& request,
                                        const Frame* response,
                                        uint32_t now_ms) {
  DedupeEntry& entry = dedupe_[next_dedupe_entry_];
  entry.active = true;
  entry.stored_ms = now_ms;
  entry.request = request;
  entry.has_response = response != nullptr;
  if (response != nullptr) entry.response = *response;
  next_dedupe_entry_ = (next_dedupe_entry_ + 1) % kDedupeEntryCount;
  has_last_sequence_ = true;
  last_sequence_ = request.sequence;
  last_rx_ms_ = now_ms;
}

void ProductionSession::handleResponse(const Frame& frame, uint32_t now_ms) {
  if (reliable_count_ == 0) return;
  ReliableEntry& pending = reliable_[reliable_head_];
  if (!pending.active || frame.payload[0] != static_cast<uint8_t>(pending.frame.type) ||
      readBe16(frame.payload + 1) != pending.frame.sequence) {
    return;
  }
  last_rx_ms_ = now_ms;
  popReliable();
  pumpReliable(now_ms);
}

void ProductionSession::handleFrame(const Frame& frame, uint32_t now_ms) {
  if (!hasValidFlagsForType(frame)) {
    sendNack(frame, NackError::InvalidFlags);
    return;
  }
  if (!isPayloadValid(frame)) {
    sendNack(frame, NackError::InvalidPayload);
    return;
  }
  if (frame.type == MessageType::Connect) {
    handleConnect(frame, now_ms);
    return;
  }
  if (!connected_) {
    sendNack(frame, NackError::SessionRequired);
    return;
  }
  if (frame.session_id != session_id_) {
    ++counters_.session_mismatches;
    sendNack(frame, NackError::SessionMismatch);
    return;
  }
  if (frame.type == MessageType::Ack || frame.type == MessageType::Nack) {
    handleResponse(frame, now_ms);
    return;
  }
  if (rejectDuplicateOrOutOfOrder(frame, now_ms)) return;

  switch (frame.type) {
    case MessageType::LinkStatus:
      rememberRequest(frame, nullptr, now_ms);
      return;
    case MessageType::FireHold: {
      const uint8_t error = callbacks_.fire_hold == nullptr
                                ? static_cast<uint8_t>(NackError::InternalError)
                                : callbacks_.fire_hold(static_cast<CommandSource>(frame.payload[0]),
                                                       now_ms,
                                                       callbacks_.context);
      if (error != 0) {
        sendNack(frame, static_cast<NackError>(error));
        return;
      }
      rememberRequest(frame, nullptr, now_ms);
      fire_dirty_ = true;
      queueFireStatus(now_ms);
      return;
    }
    case MessageType::FireStop: {
      const AckResult result = callbacks_.fire_stop == nullptr
                                   ? AckResult::NoopAlreadySafe
                                   : callbacks_.fire_stop(frame.payload[0], now_ms, callbacks_.context);
      Frame response;
      sendAck(frame, result, &response);
      rememberRequest(frame, &response, now_ms);
      fire_dirty_ = true;
      queueFireStatus(now_ms);
      return;
    }
    case MessageType::HpReset: {
      const uint8_t error = callbacks_.hp_reset == nullptr
                                ? static_cast<uint8_t>(NackError::InternalError)
                                : callbacks_.hp_reset(frame.payload[0], now_ms, callbacks_.context);
      if (error != 0) {
        sendNack(frame, static_cast<NackError>(error));
        return;
      }
      Frame response;
      sendAck(frame, AckResult::Applied, &response);
      rememberRequest(frame, &response, now_ms);
      hp_dirty_ = true;
      queueHpStatus(now_ms);
      return;
    }
    default:
      sendNack(frame, NackError::UnsupportedType);
      return;
  }
}

void ProductionSession::queueLinkStatus(uint32_t now_ms) {
  Frame frame;
  frame.type = MessageType::LinkStatus;
  frame.flags = FrameFlags::None;
  frame.sequence = takeSequence();
  frame.session_id = session_id_;
  frame.payload_length = 11;
  writeBe32(frame.payload, now_ms);
  writeBe16(frame.payload + 4, saturate16(transport_crc_errors_));
  writeBe16(frame.payload + 6,
            saturate16(transport_overflow_errors_ + tx_.overflowErrors()));
  writeBe16(frame.payload + 8, saturate16(counters_.reliable_retries));
  frame.payload[10] = static_cast<uint8_t>(state_);
  queueFrame(frame);
  last_link_status_ms_ = now_ms;
}

void ProductionSession::queueHpStatus(uint32_t now_ms) {
  if (callbacks_.hp_snapshot == nullptr) return;
  const HpSnapshot hp = callbacks_.hp_snapshot(callbacks_.context);
  Frame frame;
  frame.type = MessageType::HpStatus;
  frame.flags = FrameFlags::None;
  frame.sequence = takeSequence();
  frame.session_id = session_id_;
  frame.payload_length = 15;
  writeBe32(frame.payload, hp.revision);
  writeBe16(frame.payload + 4, hp.remaining);
  writeBe16(frame.payload + 6, hp.maximum);
  writeBe16(frame.payload + 8, hp.accepted_hits);
  frame.payload[10] = hp.down ? 1 : 0;
  writeBe32(frame.payload + 11, hp.last_hit_sequence);
  if (queueFrame(frame)) hp_dirty_ = false;
  last_hp_status_ms_ = now_ms;
}

void ProductionSession::queueFireStatus(uint32_t now_ms) {
  if (callbacks_.fire_snapshot == nullptr) return;
  const FireSnapshot fire = callbacks_.fire_snapshot(now_ms, callbacks_.context);
  Frame frame;
  frame.type = MessageType::FireStatus;
  frame.flags = FrameFlags::None;
  frame.sequence = takeSequence();
  frame.session_id = session_id_;
  frame.payload_length = 6;
  frame.payload[0] = static_cast<uint8_t>(fire.state);
  frame.payload[1] = fire.inhibited ? 1 : 0;
  frame.payload[2] = static_cast<uint8_t>(fire.source);
  writeBe16(frame.payload + 3, fire.remaining_ms);
  frame.payload[5] = static_cast<uint8_t>(fire.reason);
  if (queueFrame(frame)) fire_dirty_ = false;
  last_fire_status_ms_ = now_ms;
}

bool ProductionSession::enqueueReliable(const Frame& frame, uint32_t now_ms) {
  if (reliable_count_ >= kReliableEntryCount) {
    ++counters_.reliable_overflows;
    return false;
  }
  const size_t index = (reliable_head_ + reliable_count_) % kReliableEntryCount;
  ReliableEntry& entry = reliable_[index];
  entry.active = true;
  entry.frame = frame;
  entry.attempts = 0;
  entry.last_attempt_ms = now_ms;
  ++reliable_count_;
  pumpReliable(now_ms);
  return true;
}

void ProductionSession::pumpReliable(uint32_t now_ms) {
  if (!connected_ || reliable_count_ == 0) return;
  ReliableEntry& entry = reliable_[reliable_head_];
  if (!entry.active) return;
  if (entry.attempts != 0 &&
      static_cast<uint32_t>(now_ms - entry.last_attempt_ms) < kResponseTimeoutMs) {
    return;
  }
  if (entry.attempts >= kMaxTransmitAttempts) {
    ++counters_.reliable_drops;
    popReliable();
    pumpReliable(now_ms);
    return;
  }
  if (!queueFrame(entry.frame)) return;
  if (entry.attempts != 0) ++counters_.reliable_retries;
  ++entry.attempts;
  entry.last_attempt_ms = now_ms;
}

void ProductionSession::popReliable() {
  if (reliable_count_ == 0) return;
  reliable_[reliable_head_].active = false;
  reliable_head_ = (reliable_head_ + 1) % kReliableEntryCount;
  --reliable_count_;
}

void ProductionSession::tick(uint32_t now_ms) {
  if (!connected_) return;
  if (static_cast<int32_t>(now_ms - last_rx_ms_) >=
      static_cast<int32_t>(kLinkStaleTimeoutMs)) {
    loseLink(LinkState::Stale, FireReason::LinkStale, now_ms);
    return;
  }
  pumpReliable(now_ms);
  if (static_cast<uint32_t>(now_ms - last_link_status_ms_) >= kStatusPeriodMs) {
    queueLinkStatus(now_ms);
  }
  if (hp_dirty_ || static_cast<uint32_t>(now_ms - last_hp_status_ms_) >= kStatusPeriodMs) {
    queueHpStatus(now_ms);
  }
  if (fire_dirty_ || static_cast<uint32_t>(now_ms - last_fire_status_ms_) >= kStatusPeriodMs) {
    queueFireStatus(now_ms);
  }
}

void ProductionSession::parserFault(uint32_t now_ms) {
  ++counters_.parser_faults;
  loseLink(LinkState::Fault, FireReason::InternalFault, now_ms);
}

void ProductionSession::setTransportCounters(uint32_t crc_errors, uint32_t overflow_errors) {
  transport_crc_errors_ = crc_errors;
  transport_overflow_errors_ = overflow_errors;
}

void ProductionSession::notifyHpChanged(uint32_t now_ms) {
  hp_dirty_ = true;
  if (connected_) queueHpStatus(now_ms);
}

bool ProductionSession::notifyHit(const HitSnapshot& hit, uint32_t now_ms) {
  if (!connected_ || hit.sensor_id > 3) return false;
  Frame frame;
  frame.type = MessageType::HitEvent;
  frame.flags = FrameFlags::AckRequired;
  frame.sequence = takeSequence();
  frame.session_id = session_id_;
  frame.payload_length = 18;
  writeBe32(frame.payload, hit.hit_sequence);
  writeBe32(frame.payload + 4, hit.hp_revision);
  frame.payload[8] = hit.sensor_id;
  writeBe16(frame.payload + 9, hit.strength);
  writeBe32(frame.payload + 11, hit.timestamp_ms);
  writeBe16(frame.payload + 15, hit.hp_remaining);
  frame.payload[17] = hit.down ? 1 : 0;
  return enqueueReliable(frame, now_ms);
}

void ProductionSession::notifyFireStatus(uint32_t now_ms) {
  fire_dirty_ = true;
  if (connected_) queueFireStatus(now_ms);
}

}  // namespace serial
}  // namespace go2
