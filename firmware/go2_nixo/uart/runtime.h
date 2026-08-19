#pragma once

#include "go2_nixo/uart/protocol.h"

namespace battlebang {
namespace go2_nixo {
namespace uart {

class FrameTxQueue {
 public:
  bool enqueue(const Frame& frame);
  const uint8_t* peek(size_t& length) const;
  void consume(size_t length);
  void clear();
  size_t pendingBytes() const { return size_; }
  uint32_t overflowErrors() const { return overflow_errors_; }

 private:
  uint8_t buffer_[kTxBufferBytes] = {};
  size_t head_ = 0;
  size_t size_ = 0;
  uint32_t overflow_errors_ = 0;
};

struct HpSnapshot {
  uint32_t revision = 0;
  uint16_t remaining = 0;
  uint16_t maximum = 0;
  uint16_t accepted_hits = 0;
  bool down = false;
  uint32_t last_hit_sequence = 0;
};

struct FireSnapshot {
  FireState state = FireState::Idle;
  bool inhibited = false;
  CommandSource source = CommandSource::Unknown;
  uint16_t remaining_ms = 0;
  FireReason reason = FireReason::None;
};

struct PacketCallbacks {
  void* context = nullptr;
  AckResult (*fire_stop)(uint8_t reason, uint32_t now_ms, void* context) = nullptr;
};

enum class DedupeResult : uint8_t {
  NewRequest = 0,
  Duplicate = 1,
  Conflict = 2,
};

class InboundDedupe {
 public:
  DedupeResult check(const Frame& request, Frame& cached_response, uint32_t now_ms) const;
  void remember(const Frame& request, const Frame& response, uint32_t now_ms);

 private:
  struct Entry {
    bool active = false;
    uint32_t stored_ms = 0;
    Frame request;
    Frame response;
  };

  Entry entries_[kDedupeEntryCount] = {};
  uint8_t next_ = 0;
};

class ReliableFrameTracker {
 public:
  bool track(const Frame& frame, uint32_t now_ms);
  bool handleAckFrame(const Frame& frame);
  void retryDue(uint32_t now_ms, FrameTxQueue& tx);
  size_t activeCount() const;
  uint32_t retryCount() const { return retry_count_; }

 private:
  struct Entry {
    bool active = false;
    Frame frame;
    uint8_t attempts = 0;
    uint32_t last_sent_ms = 0;
  };

  Entry entries_[kReliableEntryCount] = {};
  uint32_t retry_count_ = 0;
};

AckResult applyFireStop(const Frame& frame, uint32_t now_ms, const PacketCallbacks& callbacks);
void composeAck(const Frame& request, AckResult result, Frame& response, uint32_t sender_epoch);
void composeNack(const Frame& request, NackError error, Frame& response, uint32_t sender_epoch);
void composeDeviceStatus(const char* device_id, uint32_t capabilities, uint32_t sender_epoch, uint16_t sequence, Frame& frame);
void composeHpSnapshot(const HpSnapshot& hp, uint32_t sender_epoch, uint16_t sequence, Frame& frame);
void composeFireStatus(const FireSnapshot& fire, uint32_t sender_epoch, uint16_t sequence, Frame& frame);
void composeLinkMetrics(uint32_t uptime_ms,
                        uint16_t crc_errors,
                        uint16_t overflow_errors,
                        uint16_t retry_count,
                        LinkState state,
                        uint32_t sender_epoch,
                        uint16_t sequence,
                        Frame& frame);

}  // namespace uart
}  // namespace go2_nixo
}  // namespace battlebang
