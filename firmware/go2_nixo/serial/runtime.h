#pragma once

#include "go2_nixo/serial/protocol.h"

namespace go2 {
namespace serial {

bool isFireHoldExpired(bool hold_active, uint32_t deadline_ms, uint32_t now_ms);
bool requiresExplicitFireRelease(FireReason reason);

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

struct HitSnapshot {
  uint32_t hit_sequence = 0;
  uint32_t hp_revision = 0;
  uint8_t sensor_id = 0;
  uint16_t strength = 0;
  uint32_t timestamp_ms = 0;
  uint16_t hp_remaining = 0;
  bool down = false;
};

struct SessionCallbacks {
  void* context = nullptr;
  uint8_t (*fire_hold)(CommandSource source, uint32_t now_ms, void* context) = nullptr;
  AckResult (*fire_stop)(uint8_t reason, uint32_t now_ms, void* context) = nullptr;
  uint8_t (*hp_reset)(uint8_t reason, uint32_t now_ms, void* context) = nullptr;
  void (*link_lost)(FireReason reason, uint32_t now_ms, void* context) = nullptr;
  HpSnapshot (*hp_snapshot)(void* context) = nullptr;
  FireSnapshot (*fire_snapshot)(uint32_t now_ms, void* context) = nullptr;
};

struct SessionCounters {
  uint32_t sequence_conflicts = 0;
  uint32_t out_of_order = 0;
  uint32_t session_mismatches = 0;
  uint32_t parser_faults = 0;
  uint32_t reliable_retries = 0;
  uint32_t reliable_drops = 0;
  uint32_t reliable_overflows = 0;
};

class ProductionSession {
 public:
  void begin(const char* device_id,
             const char* expected_robot_id,
             uint32_t esp_boot_id,
             uint32_t capabilities,
             const SessionCallbacks& callbacks,
             uint32_t now_ms = 0);
  void handleFrame(const Frame& frame, uint32_t now_ms);
  void tick(uint32_t now_ms);
  void parserFault(uint32_t now_ms);
  void setTransportCounters(uint32_t crc_errors, uint32_t overflow_errors);
  void notifyHpChanged(uint32_t now_ms);
  bool notifyHit(const HitSnapshot& hit, uint32_t now_ms);
  void notifyFireStatus(uint32_t now_ms);

  bool connected() const { return connected_; }
  uint32_t sessionId() const { return session_id_; }
  uint32_t espBootId() const { return esp_boot_id_; }
  LinkState state() const { return state_; }
  FrameTxQueue& tx() { return tx_; }
  const SessionCounters& counters() const { return counters_; }

 private:
  struct DedupeEntry {
    bool active = false;
    uint32_t stored_ms = 0;
    Frame request;
    bool has_response = false;
    Frame response;
  };

  struct ReliableEntry {
    bool active = false;
    Frame frame;
    uint8_t attempts = 0;
    uint32_t last_attempt_ms = 0;
  };

  char device_id_[33] = {};
  char expected_robot_id_[33] = {};
  uint32_t esp_boot_id_ = 0;
  uint32_t capabilities_ = 0;
  SessionCallbacks callbacks_;
  FrameTxQueue tx_;
  SessionCounters counters_;
  LinkState state_ = LinkState::Disconnected;
  bool connected_ = false;
  uint32_t session_id_ = 0;
  uint32_t last_rx_ms_ = 0;
  uint32_t last_link_status_ms_ = 0;
  uint32_t last_hp_status_ms_ = 0;
  uint32_t last_fire_status_ms_ = 0;
  uint32_t transport_crc_errors_ = 0;
  uint32_t transport_overflow_errors_ = 0;
  bool hp_dirty_ = true;
  bool fire_dirty_ = true;
  bool has_last_sequence_ = false;
  uint16_t last_sequence_ = 0;
  uint16_t next_tx_sequence_ = 0;
  DedupeEntry dedupe_[kDedupeEntryCount];
  size_t next_dedupe_entry_ = 0;
  ReliableEntry reliable_[kReliableEntryCount];
  size_t reliable_head_ = 0;
  size_t reliable_count_ = 0;

  bool queueFrame(const Frame& frame);
  uint16_t takeSequence();
  void handleConnect(const Frame& frame, uint32_t now_ms);
  void handleResponse(const Frame& frame, uint32_t now_ms);
  void sendAck(const Frame& request, AckResult result, Frame* cached = nullptr);
  void sendNack(const Frame& request, NackError error);
  bool rejectDuplicateOrOutOfOrder(const Frame& frame, uint32_t now_ms);
  void rememberRequest(const Frame& request, const Frame* response, uint32_t now_ms);
  void clearSessionData();
  void loseLink(LinkState state, FireReason reason, uint32_t now_ms);
  void queueConnected(const Frame& connect, Frame& response);
  void queueLinkStatus(uint32_t now_ms);
  void queueHpStatus(uint32_t now_ms);
  void queueFireStatus(uint32_t now_ms);
  bool enqueueReliable(const Frame& frame, uint32_t now_ms);
  void pumpReliable(uint32_t now_ms);
  void popReliable();
};

}  // namespace serial
}  // namespace go2
