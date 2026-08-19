#pragma once

#include <stddef.h>
#include <stdint.h>

namespace battlebang {
namespace go2_nixo {
namespace uart {

constexpr uint8_t kMagic0 = 0xAA;
constexpr uint8_t kMagic1 = 0x55;
constexpr uint8_t kFrameVersion = 2;
constexpr size_t kMaxPayloadBytes = 64;
constexpr size_t kMinFrameBytes = 15;
constexpr size_t kMaxFrameBytes = kMinFrameBytes + kMaxPayloadBytes;
constexpr size_t kRxBufferBytes = 512;
constexpr size_t kTxBufferBytes = 512;
constexpr uint32_t kPartialFrameTimeoutMs = 50;
constexpr uint32_t kStatusPeriodMs = 500;
constexpr uint32_t kReliableRetryMs = 100;
constexpr size_t kDedupeEntryCount = 16;
constexpr size_t kReliableEntryCount = 8;
constexpr uint8_t kMaxTransmitAttempts = 3;

// Framed UART header (wire format version 2): AA 55, version, type, flags, sequence, sender_epoch, payload_length, payload, crc16.
enum class MessageType : uint8_t {
  CapabilitiesRequest = 0x01,
  DeviceStatus = 0x02,
  LinkMetrics = 0x03,
  FireHold = 0x10,
  FireStop = 0x11,
  FireStatus = 0x12,
  HpReset = 0x20,
  HpDamage = 0x21,
  HpSnapshot = 0x22,
  HitEvent = 0x23,
  Ack = 0x7E,
  Nack = 0x7F,
  DiagEcho = 0xF0,
  DiagEchoReply = 0xF1,
};

enum class FrameFlags : uint8_t {
  None = 0,
  AckRequired = 0x01,
  Response = 0x02,
};

enum class FrameError : uint8_t {
  None,
  Length,
  Magic,
  Crc,
  Version,
  Type,
  Flags,
};

enum CapabilityBits : uint32_t {
  CapabilityFireControl = 0x00000001,
  CapabilityHpSnapshot = 0x00000002,
  CapabilityHitEvent = 0x00000004,
  CapabilityLinkMetrics = 0x00000008,
  CapabilityDiagEcho = 0x00000010,
  CapabilityRelay2Ch = 0x00000020,
  CapabilityHpDamage = 0x00000040,
};

enum class LinkState : uint8_t {
  Disconnected = 0,
  Healthy = 1,
  Stale = 2,
  Fault = 3,
  Diagnostic = 4,
};

enum class CommandSource : uint8_t {
  Unknown = 0,
  Gamepad = 1,
  Autonomy = 2,
  CommandCenter = 3,
  EspMqtt = 4,
  Diagnostic = 5,
  InternalSafety = 6,
};

enum class FireState : uint8_t {
  Idle = 0,
  Prefire = 1,
  Spinup = 2,
  Firing = 3,
  Spindown = 4,
  Cooldown = 5,
  Inhibited = 6,
  ReleaseRequired = 7,
  Fault = 8,
};

enum class FireReason : uint8_t {
  None = 0,
  OperatorRelease = 1,
  HoldTimeout = 2,
  LinkStale = 3,
  HpDown = 4,
  Inhibited = 5,
  DurationLimit = 6,
  EpochChanged = 7,
  Reset = 8,
  Emergency = 9,
  InternalFault = 10,
  ReleaseRequired = 11,
  SourceConflict = 12,
};

enum class AckResult : uint8_t {
  Applied = 0,
  Duplicate = 1,
  NoopAlreadySafe = 2,
};

enum class NackError : uint8_t {
  UnsupportedVersion = 1,
  UnsupportedType = 2,
  InvalidFlags = 3,
  InvalidLength = 4,
  InvalidPayload = 5,
  IdentityMismatch = 8,
  NotReady = 9,
  Busy = 10,
  Inhibited = 11,
  OutOfOrder = 12,
  SequenceConflict = 13,
  DiagnosticDisabled = 14,
  InternalError = 15,
};

struct Frame {
  MessageType type = MessageType::DiagEcho;
  FrameFlags flags = FrameFlags::None;
  uint16_t sequence = 0;
  uint32_t sender_epoch = 0;
  uint8_t payload_length = 0;
  uint8_t payload[kMaxPayloadBytes] = {};
  uint8_t version = kFrameVersion;
};

struct ParserCounters {
  uint32_t frames = 0;
  uint32_t discarded_bytes = 0;
  uint32_t overflow_errors = 0;
  uint32_t timeout_errors = 0;
  uint32_t length_errors = 0;
  uint32_t crc_errors = 0;
  uint32_t version_errors = 0;
  uint32_t type_errors = 0;
  uint32_t flag_errors = 0;
};

using FrameHandler = void (*)(const Frame& frame, void* context);

uint16_t crc16CcittFalse(const uint8_t* data, size_t length);
FrameError encodeFrame(const Frame& frame, uint8_t* output, size_t capacity, size_t& output_length);
FrameError decodeFrame(const uint8_t* wire, size_t length, Frame& frame);
bool isPayloadValid(const Frame& frame);
bool hasValidFlagsForType(const Frame& frame);
bool composeDiagEchoReply(const Frame& request, Frame& reply);
uint16_t nextSequence(uint16_t sequence);
bool isNewerSequence(uint16_t candidate, uint16_t previous);

class IncrementalParser {
 public:
  void feed(const uint8_t* data, size_t length, uint32_t now_ms, FrameHandler handler, void* context = nullptr);

  template <typename Handler>
  void feed(const uint8_t* data, size_t length, uint32_t now_ms, Handler handler) {
    feed(data, length, now_ms, &IncrementalParser::dispatch<Handler>, &handler);
  }

  size_t bufferedBytes() const { return size_; }
  const ParserCounters& counters() const { return counters_; }

 private:
  uint8_t buffer_[kRxBufferBytes] = {};
  size_t size_ = 0;
  bool candidate_active_ = false;
  uint32_t candidate_started_ms_ = 0;
  ParserCounters counters_;

  template <typename Handler>
  static void dispatch(const Frame& frame, void* context) {
    (*static_cast<Handler*>(context))(frame);
  }

  void append(const uint8_t* data, size_t length);
  size_t findMagic() const;
  bool partialExpired(uint32_t now_ms) const;
  void discardPrefix(size_t length);
  void consumePrefix(size_t length);
};

}  // namespace uart
}  // namespace go2_nixo
}  // namespace battlebang
