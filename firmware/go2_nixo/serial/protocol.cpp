#include "go2_nixo/serial/protocol.h"

#include <string.h>

#if defined(BATTLEBANG_UART_DIAGNOSTIC)
#include <Arduino.h>

#include "go2_nixo/build_config.h"
#include "go2_nixo/serial/runtime.h"
#endif

namespace go2 {
namespace serial {
namespace {

constexpr uint8_t kReservedFlagsMask = 0xFC;
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
    case MessageType::Connect:
    case MessageType::Connected:
    case MessageType::LinkStatus:
    case MessageType::FireHold:
    case MessageType::FireStop:
    case MessageType::FireStatus:
    case MessageType::HpReset:
    case MessageType::HpStatus:
    case MessageType::HitEvent:
    case MessageType::Ack:
    case MessageType::Nack:
    case MessageType::DiagEcho:
    case MessageType::DiagEchoReply:
      return true;
  }
  return false;
}

bool isIdPayloadValid(const Frame& frame, size_t tail_length) {
  if (frame.payload_length < 1 || frame.payload[0] < 1 || frame.payload[0] > 32) return false;
  const size_t id_length = frame.payload[0];
  if (frame.payload_length != 1 + id_length + tail_length) return false;
  for (size_t i = 1; i <= id_length; ++i) {
    const uint8_t c = frame.payload[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
      return false;
    }
  }
  return true;
}

bool inRange(uint8_t value, uint8_t maximum) {
  return value <= maximum;
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
  if (frame.payload_length > kMaxPayloadBytes) return FrameError::Length;
  const size_t frame_length = kMinFrameBytes + frame.payload_length;
  if (output == nullptr || capacity < frame_length) return FrameError::Length;
  if (frame.version != kFrameVersion) return FrameError::Version;
  if (!isKnownType(static_cast<uint8_t>(frame.type))) return FrameError::Type;
  if ((static_cast<uint8_t>(frame.flags) & kReservedFlagsMask) != 0) return FrameError::Flags;
  if (frame.session_id == 0) return FrameError::Session;

  output[0] = kMagic0;
  output[1] = kMagic1;
  output[2] = frame.version;
  output[3] = static_cast<uint8_t>(frame.type);
  output[4] = static_cast<uint8_t>(frame.flags);
  writeBe16(output + 5, frame.sequence);
  writeBe32(output + 7, frame.session_id);
  writeBe16(output + 11, frame.payload_length);
  memcpy(output + 13, frame.payload, frame.payload_length);
  writeBe16(output + 13 + frame.payload_length,
            crc16CcittFalse(output + 2, kProtectedHeaderBytes + frame.payload_length));
  output_length = frame_length;
  return FrameError::None;
}

FrameError decodeFrame(const uint8_t* wire, size_t length, Frame& frame) {
  if (wire == nullptr || length < kMinFrameBytes || length > kMaxFrameBytes) return FrameError::Length;
  if (wire[0] != kMagic0 || wire[1] != kMagic1) return FrameError::Magic;
  const uint16_t payload_length = readBe16(wire + 11);
  if (payload_length > kMaxPayloadBytes || length != kMinFrameBytes + payload_length) {
    return FrameError::Length;
  }
  if (readBe16(wire + length - 2) != crc16CcittFalse(wire + 2, length - 4)) {
    return FrameError::Crc;
  }
  if (wire[2] != kFrameVersion) return FrameError::Version;
  if ((wire[4] & kReservedFlagsMask) != 0) return FrameError::Flags;
  if (!isKnownType(wire[3])) return FrameError::Type;
  const uint32_t session_id = readBe32(wire + 7);
  if (session_id == 0) return FrameError::Session;

  frame.version = wire[2];
  frame.type = static_cast<MessageType>(wire[3]);
  frame.flags = static_cast<FrameFlags>(wire[4]);
  frame.sequence = readBe16(wire + 5);
  frame.session_id = session_id;
  frame.payload_length = static_cast<uint8_t>(payload_length);
  memcpy(frame.payload, wire + 13, payload_length);
  return FrameError::None;
}

bool isPayloadValid(const Frame& frame) {
  switch (frame.type) {
    case MessageType::Connect:
      return isIdPayloadValid(frame, 4);
    case MessageType::Connected:
      return isIdPayloadValid(frame, 9);
    case MessageType::LinkStatus:
      return frame.payload_length == 11 && inRange(frame.payload[10], 4);
    case MessageType::FireHold:
      return frame.payload_length == 1 && inRange(frame.payload[0], 6);
    case MessageType::FireStop:
      return frame.payload_length == 1;
    case MessageType::FireStatus:
      return frame.payload_length == 6 && inRange(frame.payload[0], 8) &&
             inRange(frame.payload[1], 1) && inRange(frame.payload[2], 6) &&
             inRange(frame.payload[5], 12);
    case MessageType::HpReset:
      return frame.payload_length == 1 && frame.payload[0] >= 1 && frame.payload[0] <= 4;
    case MessageType::HpStatus:
      return frame.payload_length == 15 && inRange(frame.payload[10], 1);
    case MessageType::HitEvent:
      return frame.payload_length == 18 && inRange(frame.payload[8], 3) &&
             inRange(frame.payload[17], 1);
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
  FrameFlags expected = FrameFlags::None;
  switch (frame.type) {
    case MessageType::Connect:
    case MessageType::FireStop:
    case MessageType::HpReset:
    case MessageType::HitEvent:
    case MessageType::DiagEcho:
      expected = FrameFlags::AckRequired;
      break;
    case MessageType::Connected:
    case MessageType::Ack:
    case MessageType::Nack:
    case MessageType::DiagEchoReply:
      expected = FrameFlags::Response;
      break;
    case MessageType::LinkStatus:
    case MessageType::FireHold:
    case MessageType::FireStatus:
    case MessageType::HpStatus:
      expected = FrameFlags::None;
      break;
  }
  return frame.flags == expected;
}

bool composeDiagEchoReply(const Frame& request, Frame& reply) {
  if (request.type != MessageType::DiagEcho || !isPayloadValid(request)) return false;
  reply.type = MessageType::DiagEchoReply;
  reply.flags = FrameFlags::Response;
  reply.sequence = request.sequence;
  reply.session_id = request.session_id;
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
  if (length == 0) return;
  if (data == nullptr) return;

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

bool IncrementalParser::partialExpired(uint32_t now_ms) {
  if (!candidate_active_) {
    candidate_active_ = true;
    candidate_started_ms_ = now_ms;
    return false;
  }
  return static_cast<uint32_t>(now_ms - candidate_started_ms_) >= kPartialFrameTimeoutMs;
}

void IncrementalParser::discardPrefix(size_t length) {
  if (length == 0) return;
  consumePrefix(length);
  counters_.discarded_bytes += static_cast<uint32_t>(length);
}

void IncrementalParser::consumePrefix(size_t length) {
  if (length >= size_) {
    size_ = 0;
    return;
  }
  memmove(buffer_, buffer_ + length, size_ - length);
  size_ -= length;
}

void IncrementalParser::feed(const uint8_t* data,
                             size_t length,
                             uint32_t now_ms,
                             FrameHandler handler,
                             void* context) {
  append(data, length);
  while (true) {
    const size_t magic_at = findMagic();
    if (magic_at == size_) {
      const size_t keep = size_ != 0 && buffer_[size_ - 1] == kMagic0 ? 1 : 0;
      discardPrefix(size_ - keep);
      candidate_active_ = false;
      return;
    }
    if (magic_at != 0) {
      discardPrefix(magic_at);
      candidate_active_ = false;
    }
    if (size_ < 13) {
      if (!partialExpired(now_ms)) return;
      ++counters_.timeout_errors;
      discardPrefix(1);
      candidate_active_ = false;
      continue;
    }

    const uint16_t payload_length = readBe16(buffer_ + 11);
    if (payload_length > kMaxPayloadBytes) {
      ++counters_.length_errors;
      discardPrefix(1);
      candidate_active_ = false;
      continue;
    }
    const size_t frame_length = kMinFrameBytes + payload_length;
    if (size_ < frame_length) {
      if (!partialExpired(now_ms)) return;
      ++counters_.timeout_errors;
      discardPrefix(1);
      candidate_active_ = false;
      continue;
    }

    Frame frame;
    const FrameError error = decodeFrame(buffer_, frame_length, frame);
    if (error == FrameError::None) {
      consumePrefix(frame_length);
      candidate_active_ = false;
      ++counters_.frames;
      if (handler != nullptr) handler(frame, context);
      continue;
    }
    switch (error) {
      case FrameError::Crc:
        ++counters_.crc_errors;
        break;
      case FrameError::Version:
        ++counters_.version_errors;
        break;
      case FrameError::Type:
        ++counters_.type_errors;
        break;
      case FrameError::Flags:
        ++counters_.flag_errors;
        break;
      case FrameError::Session:
        ++counters_.session_errors;
        break;
      default:
        ++counters_.length_errors;
        break;
    }
    discardPrefix(1);
    candidate_active_ = false;
  }
}


#if defined(BATTLEBANG_UART_DIAGNOSTIC)
namespace {

IncrementalParser diagnosticParser;
FrameTxQueue diagnosticTx;

void forceRelaysOff() {
  pinMode(NIXO_RELAY1_PIN_VALUE, OUTPUT);
  digitalWrite(NIXO_RELAY1_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  if (NIXO_RELAY2_ENABLED_VALUE) {
    pinMode(NIXO_RELAY2_PIN_VALUE, OUTPUT);
    digitalWrite(NIXO_RELAY2_PIN_VALUE, NIXO_RELAY_OFF_LEVEL_VALUE);
  }
}

void sendReply(const Frame& request, void*) {
  Frame reply;
  if (!composeDiagEchoReply(request, reply)) return;
  diagnosticTx.enqueue(reply);
}

void drainUsb(uint32_t now) {
  uint8_t bytes[128];
  while (Serial.available() > 0) {
    size_t count = 0;
    while (count < sizeof(bytes) && Serial.available() > 0) {
      const int value = Serial.read();
      if (value < 0) break;
      bytes[count++] = static_cast<uint8_t>(value);
    }
    if (count == 0) return;
    diagnosticParser.feed(bytes, count, now, sendReply);
  }
}

void flushUsb() {
  while (diagnosticTx.pendingBytes() != 0) {
    const int writable = Serial.availableForWrite();
    if (writable <= 0) return;
    size_t available = 0;
    const uint8_t* bytes = diagnosticTx.peek(available);
    const size_t requested = available < static_cast<size_t>(writable)
                                 ? available
                                 : static_cast<size_t>(writable);
    const size_t written = Serial.write(bytes, requested);
    diagnosticTx.consume(written);
    if (written < requested) return;
  }
}

}  // namespace

void diagnosticSetup() {
  forceRelaysOff();
  Serial.begin(UART_BAUD);
}

void diagnosticLoop() {
  forceRelaysOff();
  const uint32_t now = millis();
  drainUsb(now);
  diagnosticParser.feed(nullptr, 0, now, sendReply);
  flushUsb();
  delay(1);
}
#endif

}  // namespace serial
}  // namespace go2
