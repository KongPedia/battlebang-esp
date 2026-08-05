#pragma once

#include <stddef.h>
#include <stdint.h>

namespace go2 {
namespace serial {

constexpr uint8_t kMagic0 = 0xAA;
constexpr uint8_t kMagic1 = 0x55;
constexpr uint8_t kFrameVersion = 1;
constexpr size_t kMaxPayloadBytes = 64;
constexpr size_t kMinFrameBytes = 15;
constexpr size_t kMaxFrameBytes = 79;
constexpr size_t kRxBufferBytes = 512;
constexpr size_t kTxBufferBytes = 512;
constexpr uint32_t kPartialFrameTimeoutMs = 50;
constexpr uint32_t kResponseTimeoutMs = 100;
constexpr uint32_t kStatusPeriodMs = 500;
constexpr uint32_t kLinkStaleTimeoutMs = 1500;
constexpr uint32_t kDedupeExpiryMs = 2000;
constexpr size_t kDedupeEntryCount = 16;
constexpr size_t kReliableEntryCount = 8;
constexpr uint8_t kMaxTransmitAttempts = 3;

enum class MessageType : uint8_t {
  Connect = 0x01,
  Connected = 0x02,
  LinkStatus = 0x03,
  FireHold = 0x10,
  FireStop = 0x11,
  FireStatus = 0x12,
  HpReset = 0x20,
  HpStatus = 0x21,
  HitEvent = 0x22,
  HpDamage = 0x23,
  HpGuard = 0x24,
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
  Session,
};

enum CapabilityBits : uint32_t {
  CapabilityFireControl = 0x00000001,
  CapabilityHpStatus = 0x00000002,
  CapabilityHitEvent = 0x00000004,
  CapabilityLinkStatus = 0x00000008,
  CapabilityDiagEcho = 0x00000010,
  CapabilityRelay2Ch = 0x00000020,
  CapabilityHpDamage = 0x00000040,
  CapabilityHpGuard = 0x00000080,
};

enum class LinkState : uint8_t {
  Disconnected = 0,
  Connected = 1,
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
  SessionChanged = 7,
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
  SessionRequired = 6,
  SessionMismatch = 7,
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
  uint32_t session_id = 0;
  uint8_t payload_length = 0;
  uint8_t payload[kMaxPayloadBytes] = {};
  uint8_t version = kFrameVersion;
};

uint16_t crc16CcittFalse(const uint8_t* data, size_t length);
FrameError encodeFrame(const Frame& frame, uint8_t* output, size_t capacity, size_t& output_length);
FrameError decodeFrame(const uint8_t* wire, size_t length, Frame& frame);
bool isPayloadValid(const Frame& frame);
bool hasValidFlagsForType(const Frame& frame);
bool composeDiagEchoReply(const Frame& request, Frame& reply);
uint16_t nextSequence(uint16_t sequence);
bool isNewerSequence(uint16_t candidate, uint16_t previous);

#if defined(BATTLEBANG_UART_DIAGNOSTIC)
void diagnosticSetup();
void diagnosticLoop();
#endif

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
  uint32_t session_errors = 0;
};

using FrameHandler = void (*)(const Frame& frame, void* context);

class IncrementalParser {
 public:
  void feed(const uint8_t* data,
            size_t length,
            uint32_t now_ms,
            FrameHandler handler,
            void* context = nullptr);
  size_t bufferedBytes() const { return size_; }
  const ParserCounters& counters() const { return counters_; }

 private:
  uint8_t buffer_[kRxBufferBytes] = {};
  size_t size_ = 0;
  bool candidate_active_ = false;
  uint32_t candidate_started_ms_ = 0;
  ParserCounters counters_;

  void append(const uint8_t* data, size_t length);
  size_t findMagic() const;
  bool partialExpired(uint32_t now_ms);
  void discardPrefix(size_t length);
  void consumePrefix(size_t length);
};


}  // namespace serial
}  // namespace go2
