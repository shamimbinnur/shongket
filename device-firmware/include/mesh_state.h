#pragma once

#include "mesh_protocol.h"

#include <stddef.h>
#include <stdint.h>

namespace mesh {

constexpr size_t SEEN_CACHE_SIZE = 64;
constexpr uint32_t SEEN_EXPIRY_MS = 120000UL;
constexpr size_t ACK_QUEUE_SIZE = 4;
constexpr size_t RELAY_QUEUE_SIZE = 8;

bool timeReached(uint32_t now, uint32_t deadline);
bool intervalElapsed(uint32_t now, uint32_t started, uint32_t interval);

struct FloodKey {
  PacketType type = PacketType::Invalid;
  uint16_t origin = 0;
  SessionId originSession;
  uint32_t floodId = 0;
};

bool floodKeyEqual(const FloodKey &left, const FloodKey &right);
FloodKey floodKeyFor(const Packet &packet);

struct SeenEntry {
  bool active = false;
  FloodKey key;
  uint32_t lastSeenAt = 0;
  uint16_t firstTransmitter = 0;
  uint16_t alternateTransmitters[2] = {};
  uint8_t alternateCount = 0;
  uint16_t duplicateCopies = 0;
  bool relayed = false;
};

struct SeenResult {
  int index = -1;
  bool isNew = false;
};

class SeenCache {
 public:
  SeenCache();
  void clear();
  SeenResult observe(const FloodKey &key, uint16_t transmitter, uint32_t now);
  int find(const FloodKey &key, uint32_t now);
  SeenEntry *entry(int index);
  const SeenEntry *entry(int index) const;
  void markRelayed(int index);

 private:
  void expire(uint32_t now);
  SeenEntry entries_[SEEN_CACHE_SIZE];
};

struct AckJob {
  bool active = false;
  uint16_t destination = 0;
  uint32_t messageId = 0;
  uint32_t dueAt = 0;
};

class AckQueue {
 public:
  AckQueue();
  void clear();
  bool schedule(uint16_t destination, uint32_t messageId, uint32_t dueAt);
  int dueIndex(uint32_t now) const;
  AckJob *job(int index);
  void remove(int index);

 private:
  AckJob jobs_[ACK_QUEUE_SIZE];
};

struct RelayJob {
  bool active = false;
  Packet packet;
  int seenIndex = -1;
  uint32_t dueAt = 0;
};

class RelayQueue {
 public:
  RelayQueue();
  void clear();
  bool schedule(const Packet &packet, int seenIndex, uint32_t dueAt);
  size_t cancel(const FloodKey &key);
  int dueIndex(uint32_t now, bool presenceClass) const;
  RelayJob *job(int index);
  const RelayJob *job(int index) const;
  void remove(int index);

 private:
  RelayJob jobs_[RELAY_QUEUE_SIZE];
};

enum class DeliveryPhase : uint8_t {
  Idle = 0,
  Backoff,
  AwaitingAck,
  Delivered,
  Failed,
};

class DeliveryTracker {
 public:
  void start(uint32_t messageId, uint16_t destination, bool crewMessage,
             int historyIndex, uint32_t now, uint32_t initialDelay);
  bool sendDue(uint32_t now) const;
  void noteSent(uint32_t now, uint32_t ackTimeout);
  bool handleTimeout(uint32_t now, uint32_t retryDelay, uint8_t maxAttempts);
  bool confirm(uint32_t messageId);
  void reset();

  DeliveryPhase phase() const { return phase_; }
  uint32_t messageId() const { return messageId_; }
  uint16_t destination() const { return destination_; }
  bool crewMessage() const { return crewMessage_; }
  int historyIndex() const { return historyIndex_; }
  uint8_t attempts() const { return attempts_; }

 private:
  DeliveryPhase phase_ = DeliveryPhase::Idle;
  uint32_t messageId_ = 0;
  uint16_t destination_ = 0;
  bool crewMessage_ = false;
  int historyIndex_ = -1;
  uint8_t attempts_ = 0;
  uint32_t deadline_ = 0;
};

}  // namespace mesh
