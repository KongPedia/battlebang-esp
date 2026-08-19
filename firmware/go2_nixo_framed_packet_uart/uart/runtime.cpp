#include "go2_nixo_framed_packet_uart/uart/runtime.h"

#include <string.h>

namespace battlebang {
namespace go2_nixo {
namespace uart {
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

bool FrameTxQueue::enqueue(const Frame& frame) {
  uint8_t wire[kMaxFrameBytes] = {};
  size_t wire_length = 0;
  if (encodeFrame(frame, wire, sizeof(wire), wire_length) != FrameError::None || wire_length > kTxBufferBytes - size_) {
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

bool sameRequest(const Frame& left, const Frame& right) {
  return left.type == right.type && left.flags == right.flags && left.sequence == right.sequence &&
         left.sender_epoch == right.sender_epoch && left.payload_length == right.payload_length &&
         memcmp(left.payload, right.payload, left.payload_length) == 0;
}

bool isReliableOutbound(const Frame& frame) {
  return frame.flags == FrameFlags::AckRequired;
}

}  // namespace

DedupeResult InboundDedupe::check(const Frame& request, Frame& cached_response, uint32_t now_ms) const {
  for (size_t i = 0; i < kDedupeEntryCount; ++i) {
    const Entry& entry = entries_[i];
    if (!entry.active || static_cast<uint32_t>(now_ms - entry.stored_ms) > 2000U ||
        entry.request.sequence != request.sequence || entry.request.sender_epoch != request.sender_epoch) {
      continue;
    }
    if (!sameRequest(entry.request, request)) return DedupeResult::Conflict;
    cached_response = entry.response;
    return DedupeResult::Duplicate;
  }
  return DedupeResult::NewRequest;
}

void InboundDedupe::remember(const Frame& request, const Frame& response, uint32_t now_ms) {
  entries_[next_].active = true;
  entries_[next_].stored_ms = now_ms;
  entries_[next_].request = request;
  entries_[next_].response = response;
  next_ = static_cast<uint8_t>((next_ + 1U) % kDedupeEntryCount);
}

bool ReliableFrameTracker::track(const Frame& frame, uint32_t now_ms) {
  if (!isReliableOutbound(frame)) return true;
  for (size_t i = 0; i < kReliableEntryCount; ++i) {
    if (!entries_[i].active) {
      entries_[i].active = true;
      entries_[i].frame = frame;
      entries_[i].attempts = 1;
      entries_[i].last_sent_ms = now_ms;
      return true;
    }
  }
  return false;
}

bool ReliableFrameTracker::handleAckFrame(const Frame& frame) {
  if ((frame.type != MessageType::Ack && frame.type != MessageType::Nack) || frame.payload_length != 4) return false;
  const MessageType acked_type = static_cast<MessageType>(frame.payload[0]);
  const uint16_t acked_seq = readBe16(frame.payload + 1);
  for (size_t i = 0; i < kReliableEntryCount; ++i) {
    if (entries_[i].active && entries_[i].frame.type == acked_type && entries_[i].frame.sequence == acked_seq) {
      entries_[i].active = false;
      return true;
    }
  }
  return false;
}

void ReliableFrameTracker::retryDue(uint32_t now_ms, FrameTxQueue& tx) {
  for (size_t i = 0; i < kReliableEntryCount; ++i) {
    Entry& entry = entries_[i];
    if (!entry.active || static_cast<uint32_t>(now_ms - entry.last_sent_ms) < kReliableRetryMs) continue;
    if (entry.attempts >= kMaxTransmitAttempts) {
      entry.active = false;
      continue;
    }
    if (tx.enqueue(entry.frame)) {
      ++entry.attempts;
      entry.last_sent_ms = now_ms;
      ++retry_count_;
    }
  }
}

size_t ReliableFrameTracker::activeCount() const {
  size_t count = 0;
  for (size_t i = 0; i < kReliableEntryCount; ++i) {
    if (entries_[i].active) ++count;
  }
  return count;
}

AckResult applyFireStop(const Frame& frame, uint32_t now_ms, const PacketCallbacks& callbacks) {
  if (frame.type != MessageType::FireStop) return AckResult::NoopAlreadySafe;
  if (callbacks.fire_stop == nullptr) return AckResult::NoopAlreadySafe;
  const uint8_t reason = frame.payload_length > 0 ? frame.payload[0] : 0;
  return callbacks.fire_stop(reason, now_ms, callbacks.context);
}

void composeAck(const Frame& request, AckResult result, Frame& response, uint32_t sender_epoch) {
  response = Frame();
  response.type = MessageType::Ack;
  response.flags = FrameFlags::Response;
  response.sequence = request.sequence;
  response.sender_epoch = sender_epoch;
  response.payload_length = 4;
  response.payload[0] = static_cast<uint8_t>(request.type);
  writeBe16(response.payload + 1, request.sequence);
  response.payload[3] = static_cast<uint8_t>(result);
}

void composeNack(const Frame& request, NackError error, Frame& response, uint32_t sender_epoch) {
  response = Frame();
  response.type = MessageType::Nack;
  response.flags = FrameFlags::Response;
  response.sequence = request.sequence;
  response.sender_epoch = sender_epoch;
  response.payload_length = 4;
  response.payload[0] = static_cast<uint8_t>(request.type);
  writeBe16(response.payload + 1, request.sequence);
  response.payload[3] = static_cast<uint8_t>(error);
}

void composeDeviceStatus(const char* device_id, uint32_t capabilities, uint32_t sender_epoch, uint16_t sequence, Frame& frame) {
  frame = Frame();
  frame.type = MessageType::DeviceStatus;
  frame.flags = FrameFlags::None;
  frame.sequence = sequence;
  frame.sender_epoch = sender_epoch;
  const char* id = device_id != nullptr && device_id[0] != '\0' ? device_id : "go2_nixo";
  size_t id_length = strlen(id);
  if (id_length > 32) id_length = 32;
  frame.payload[0] = static_cast<uint8_t>(id_length);
  memcpy(frame.payload + 1, id, id_length);
  writeBe32(frame.payload + 1 + id_length, sender_epoch);
  writeBe32(frame.payload + 5 + id_length, capabilities);
  frame.payload[9 + id_length] = kFrameVersion;
  frame.payload_length = static_cast<uint8_t>(10 + id_length);
}

void composeHpSnapshot(const HpSnapshot& hp, uint32_t sender_epoch, uint16_t sequence, Frame& frame) {
  frame = Frame();
  frame.type = MessageType::HpSnapshot;
  frame.flags = FrameFlags::None;
  frame.sequence = sequence;
  frame.sender_epoch = sender_epoch;
  frame.payload_length = 15;
  writeBe32(frame.payload + 0, hp.revision);
  writeBe16(frame.payload + 4, hp.remaining);
  writeBe16(frame.payload + 6, hp.maximum);
  writeBe16(frame.payload + 8, hp.accepted_hits);
  frame.payload[10] = hp.down ? 1 : 0;
  writeBe32(frame.payload + 11, hp.last_hit_sequence);
}

void composeFireStatus(const FireSnapshot& fire, uint32_t sender_epoch, uint16_t sequence, Frame& frame) {
  frame = Frame();
  frame.type = MessageType::FireStatus;
  frame.flags = FrameFlags::None;
  frame.sequence = sequence;
  frame.sender_epoch = sender_epoch;
  frame.payload_length = 6;
  frame.payload[0] = static_cast<uint8_t>(fire.state);
  frame.payload[1] = fire.inhibited ? 1 : 0;
  frame.payload[2] = static_cast<uint8_t>(fire.source);
  writeBe16(frame.payload + 3, fire.remaining_ms);
  frame.payload[5] = static_cast<uint8_t>(fire.reason);
}

void composeLinkMetrics(uint32_t uptime_ms,
                        uint16_t crc_errors,
                        uint16_t overflow_errors,
                        uint16_t retry_count,
                        LinkState state,
                        uint32_t sender_epoch,
                        uint16_t sequence,
                        Frame& frame) {
  frame = Frame();
  frame.type = MessageType::LinkMetrics;
  frame.flags = FrameFlags::None;
  frame.sequence = sequence;
  frame.sender_epoch = sender_epoch;
  frame.payload_length = 11;
  writeBe32(frame.payload + 0, uptime_ms);
  writeBe16(frame.payload + 4, crc_errors);
  writeBe16(frame.payload + 6, overflow_errors);
  writeBe16(frame.payload + 8, retry_count);
  frame.payload[10] = static_cast<uint8_t>(state);
}

}  // namespace uart
}  // namespace go2_nixo
}  // namespace battlebang
