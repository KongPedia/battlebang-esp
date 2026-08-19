#include "go2_nixo_framed_packet_uart/uart/protocol.h"

#include <string.h>

namespace battlebang {
namespace go2_nixo {
namespace uart {
namespace {

constexpr uint8_t kReservedFlagsMask = 0xFC;
constexpr size_t kPayloadOffset = 13;
constexpr size_t kProtectedHeaderBytes = 11;

uint16_t readBe16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t readBe32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
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

bool isKnownType(uint8_t value) {
  switch (static_cast<MessageType>(value)) {
    case MessageType::CapabilitiesRequest:
    case MessageType::DeviceStatus:
    case MessageType::LinkMetrics:
    case MessageType::FireHold:
    case MessageType::FireStop:
    case MessageType::FireStatus:
    case MessageType::HpReset:
    case MessageType::HpDamage:
    case MessageType::HpSnapshot:
    case MessageType::HitEvent:
    case MessageType::Ack:
    case MessageType::Nack:
    case MessageType::DiagEcho:
    case MessageType::DiagEchoReply:
      return true;
  }
  return false;
}

bool inRange(uint8_t value, uint8_t maximum) {
  return value <= maximum;
}

bool isIdPayloadValid(const Frame& frame, size_t tail_length) {
  if (frame.payload_length < 1 || frame.payload[0] < 1 || frame.payload[0] > 32) return false;
  const size_t id_length = frame.payload[0];
  if (frame.payload_length != 1 + id_length + tail_length) return false;
  for (size_t i = 1; i <= id_length; ++i) {
    const uint8_t c = frame.payload[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

}  // namespace

uint16_t crc16CcittFalse(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

FrameError encodeFrame(const Frame& frame, uint8_t* output, size_t capacity, size_t& output_length) {
  output_length = 0;
  if (output == nullptr || frame.payload_length > kMaxPayloadBytes) return FrameError::Length;
  const size_t frame_length = kMinFrameBytes + frame.payload_length;
  if (capacity < frame_length) return FrameError::Length;
  if (frame.version != kFrameVersion) return FrameError::Version;
  if (!isKnownType(static_cast<uint8_t>(frame.type))) return FrameError::Type;
  if (!hasValidFlagsForType(frame)) return FrameError::Flags;

  output[0] = kMagic0;
  output[1] = kMagic1;
  output[2] = frame.version;
  output[3] = static_cast<uint8_t>(frame.type);
  output[4] = static_cast<uint8_t>(frame.flags);
  writeBe16(output + 5, frame.sequence);
  writeBe32(output + 7, frame.sender_epoch);
  writeBe16(output + 11, frame.payload_length);
  memcpy(output + kPayloadOffset, frame.payload, frame.payload_length);
  writeBe16(output + kPayloadOffset + frame.payload_length,
            crc16CcittFalse(output + 2, kProtectedHeaderBytes + frame.payload_length));
  output_length = frame_length;
  return FrameError::None;
}

FrameError decodeFrame(const uint8_t* wire, size_t length, Frame& frame) {
  if (wire == nullptr || length < kMinFrameBytes || length > kMaxFrameBytes) return FrameError::Length;
  if (wire[0] != kMagic0 || wire[1] != kMagic1) return FrameError::Magic;
  const uint16_t payload_length = readBe16(wire + 11);
  if (payload_length > kMaxPayloadBytes || length != kMinFrameBytes + payload_length) return FrameError::Length;
  if (readBe16(wire + length - 2) != crc16CcittFalse(wire + 2, length - 4)) return FrameError::Crc;
  if (wire[2] != kFrameVersion) return FrameError::Version;
  if ((wire[4] & kReservedFlagsMask) != 0) return FrameError::Flags;
  if (!isKnownType(wire[3])) return FrameError::Type;

  frame.version = wire[2];
  frame.type = static_cast<MessageType>(wire[3]);
  frame.flags = static_cast<FrameFlags>(wire[4]);
  frame.sequence = readBe16(wire + 5);
  frame.sender_epoch = readBe32(wire + 7);
  frame.payload_length = static_cast<uint8_t>(payload_length);
  memcpy(frame.payload, wire + kPayloadOffset, payload_length);
  if (!hasValidFlagsForType(frame)) return FrameError::Flags;
  return FrameError::None;
}

bool isPayloadValid(const Frame& frame) {
  switch (frame.type) {
    case MessageType::CapabilitiesRequest:
      return isIdPayloadValid(frame, 4);
    case MessageType::DeviceStatus:
      return isIdPayloadValid(frame, 9);
    case MessageType::LinkMetrics:
      return frame.payload_length == 11;
    case MessageType::FireHold:
      return frame.payload_length == 3 && frame.payload[0] >= 1 && inRange(frame.payload[0], 6) &&
             readBe16(frame.payload + 1) > 0;
    case MessageType::FireStop:
      return frame.payload_length == 1 && inRange(frame.payload[0], 12);
    case MessageType::FireStatus:
      return frame.payload_length == 6 && inRange(frame.payload[0], 8) && inRange(frame.payload[1], 1) &&
             inRange(frame.payload[2], 6) && inRange(frame.payload[5], 12);
    case MessageType::HpReset:
      return frame.payload_length == 1 && frame.payload[0] >= 1 && frame.payload[0] <= 4;
    case MessageType::HpDamage:
      return frame.payload_length == 3 && readBe16(frame.payload) > 0 && frame.payload[2] >= 1 &&
             inRange(frame.payload[2], 6);
    case MessageType::HpSnapshot:
      return frame.payload_length == 15 && readBe16(frame.payload + 6) > 0 &&
             readBe16(frame.payload + 4) <= readBe16(frame.payload + 6) && inRange(frame.payload[10], 1);
    case MessageType::HitEvent:
      return frame.payload_length == 18 && inRange(frame.payload[8], 3) && inRange(frame.payload[17], 1);
    case MessageType::Ack:
      return frame.payload_length == 4 && inRange(frame.payload[3], 2);
    case MessageType::Nack:
      return frame.payload_length == 4 && frame.payload[3] >= 1 && frame.payload[3] <= 15;
    case MessageType::DiagEcho:
    case MessageType::DiagEchoReply:
      return frame.payload_length <= kMaxPayloadBytes;
  }
  return false;
}

bool hasValidFlagsForType(const Frame& frame) {
  if ((static_cast<uint8_t>(frame.flags) & kReservedFlagsMask) != 0) return false;
  switch (frame.type) {
    case MessageType::FireStop:
    case MessageType::HpReset:
    case MessageType::HpDamage:
    case MessageType::HitEvent:
    case MessageType::DiagEcho:
      return frame.flags == FrameFlags::AckRequired;
    case MessageType::Ack:
    case MessageType::Nack:
    case MessageType::DiagEchoReply:
      return frame.flags == FrameFlags::Response;
    case MessageType::CapabilitiesRequest:
    case MessageType::DeviceStatus:
    case MessageType::LinkMetrics:
    case MessageType::FireHold:
    case MessageType::FireStatus:
    case MessageType::HpSnapshot:
      return frame.flags == FrameFlags::None;
  }
  return false;
}

bool composeDiagEchoReply(const Frame& request, Frame& reply) {
  if (request.type != MessageType::DiagEcho || !isPayloadValid(request)) return false;
  reply.type = MessageType::DiagEchoReply;
  reply.flags = FrameFlags::Response;
  reply.sequence = request.sequence;
  reply.sender_epoch = request.sender_epoch;
  reply.payload_length = request.payload_length;
  reply.version = kFrameVersion;
  memcpy(reply.payload, request.payload, request.payload_length);
  return true;
}

uint16_t nextSequence(uint16_t sequence) {
  return static_cast<uint16_t>(sequence + 1);
}

bool isNewerSequence(uint16_t candidate, uint16_t previous) {
  const uint16_t delta = static_cast<uint16_t>(candidate - previous);
  return delta != 0 && delta < 0x8000;
}

void IncrementalParser::append(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0) return;
  size_t dropped = 0;
  if (length >= kRxBufferBytes) {
    dropped = size_ + length - kRxBufferBytes;
    memcpy(buffer_, data + length - kRxBufferBytes, kRxBufferBytes);
    size_ = kRxBufferBytes;
  } else if (size_ > kRxBufferBytes - length) {
    dropped = size_ - (kRxBufferBytes - length);
    memmove(buffer_, buffer_ + dropped, size_ - dropped);
    size_ -= dropped;
    memcpy(buffer_ + size_, data, length);
    size_ += length;
  } else {
    memcpy(buffer_ + size_, data, length);
    size_ += length;
  }
  if (dropped != 0) {
    counters_.discarded_bytes += static_cast<uint32_t>(dropped);
    ++counters_.overflow_errors;
    candidate_active_ = false;
  }
}

size_t IncrementalParser::findMagic() const {
  for (size_t i = 0; i + 1 < size_; ++i) {
    if (buffer_[i] == kMagic0 && buffer_[i + 1] == kMagic1) return i;
  }
  return size_;
}

bool IncrementalParser::partialExpired(uint32_t now_ms) const {
  return candidate_active_ && static_cast<uint32_t>(now_ms - candidate_started_ms_) > kPartialFrameTimeoutMs;
}

void IncrementalParser::discardPrefix(size_t length) {
  if (length >= size_) {
    counters_.discarded_bytes += static_cast<uint32_t>(size_);
    size_ = 0;
    candidate_active_ = false;
    return;
  }
  memmove(buffer_, buffer_ + length, size_ - length);
  size_ -= length;
  counters_.discarded_bytes += static_cast<uint32_t>(length);
  candidate_active_ = false;
}

void IncrementalParser::consumePrefix(size_t length) {
  if (length >= size_) {
    size_ = 0;
  } else {
    memmove(buffer_, buffer_ + length, size_ - length);
    size_ -= length;
  }
  candidate_active_ = false;
}

void IncrementalParser::feed(const uint8_t* data,
                             size_t length,
                             uint32_t now_ms,
                             FrameHandler handler,
                             void* context) {
  append(data, length);
  while (size_ > 0) {
    const size_t magic = findMagic();
    if (magic == size_) {
      const bool keep_tail_magic = size_ > 0 && buffer_[size_ - 1] == kMagic0;
      discardPrefix(keep_tail_magic ? size_ - 1 : size_);
      return;
    }
    if (magic > 0) {
      discardPrefix(magic);
      continue;
    }
    if (size_ < 2) return;
    if (!candidate_active_) {
      candidate_active_ = true;
      candidate_started_ms_ = now_ms;
    } else if (partialExpired(now_ms)) {
      ++counters_.timeout_errors;
      discardPrefix(1);
      continue;
    }
    if (size_ < kMinFrameBytes) return;
    const uint16_t payload_length = readBe16(buffer_ + 11);
    if (payload_length > kMaxPayloadBytes) {
      ++counters_.length_errors;
      discardPrefix(1);
      continue;
    }
    const size_t frame_length = kMinFrameBytes + payload_length;
    if (size_ < frame_length) return;
    Frame frame;
    const FrameError error = decodeFrame(buffer_, frame_length, frame);
    if (error == FrameError::None) {
      ++counters_.frames;
      if (handler != nullptr) handler(frame, context);
      consumePrefix(frame_length);
      continue;
    }
    if (error == FrameError::Crc) ++counters_.crc_errors;
    if (error == FrameError::Version) ++counters_.version_errors;
    if (error == FrameError::Type) ++counters_.type_errors;
    if (error == FrameError::Flags) ++counters_.flag_errors;
    if (error == FrameError::Length) ++counters_.length_errors;
    discardPrefix(1);
  }
}

}  // namespace uart
}  // namespace go2_nixo
}  // namespace battlebang
