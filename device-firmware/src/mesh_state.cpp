#include "mesh_state.h"

#include <string.h>

namespace mesh {

bool timeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool intervalElapsed(uint32_t now, uint32_t started, uint32_t interval) {
  return static_cast<uint32_t>(now - started) >= interval;
}

bool floodKeyEqual(const FloodKey &left, const FloodKey &right) {
  return left.type == right.type && left.origin == right.origin &&
         left.floodId == right.floodId &&
         sessionEqual(left.originSession, right.originSession);
}

FloodKey floodKeyFor(const Packet &packet) {
  FloodKey key;
  key.type = packet.envelope.type;
  key.origin = packet.envelope.origin;
  key.originSession = packet.envelope.originSession;
  key.floodId = packet.envelope.floodId;
  return key;
}

SeenCache::SeenCache() { clear(); }

void SeenCache::clear() { memset(entries_, 0, sizeof(entries_)); }

void SeenCache::expire(uint32_t now) {
  for (size_t i = 0; i < SEEN_CACHE_SIZE; ++i) {
    if (entries_[i].active &&
        intervalElapsed(now, entries_[i].lastSeenAt, SEEN_EXPIRY_MS)) {
      entries_[i] = SeenEntry{};
    }
  }
}

int SeenCache::find(const FloodKey &key, uint32_t now) {
  expire(now);
  for (size_t i = 0; i < SEEN_CACHE_SIZE; ++i) {
    if (entries_[i].active && floodKeyEqual(entries_[i].key, key)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

SeenResult SeenCache::observe(const FloodKey &key, uint16_t transmitter,
                              uint32_t now) {
  SeenResult result;
  const int existing = find(key, now);
  if (existing >= 0) {
    SeenEntry &seen = entries_[existing];
    seen.lastSeenAt = now;
    if (seen.duplicateCopies < UINT16_MAX) {
      ++seen.duplicateCopies;
    }
    if (transmitter != seen.firstTransmitter) {
      bool known = false;
      for (uint8_t i = 0; i < seen.alternateCount; ++i) {
        known = known || seen.alternateTransmitters[i] == transmitter;
      }
      if (!known && seen.alternateCount < 2) {
        seen.alternateTransmitters[seen.alternateCount++] = transmitter;
      }
    }
    result.index = existing;
    return result;
  }

  int selected = -1;
  uint32_t greatestAge = 0;
  for (size_t i = 0; i < SEEN_CACHE_SIZE; ++i) {
    if (!entries_[i].active) {
      selected = static_cast<int>(i);
      break;
    }
    const uint32_t age = now - entries_[i].lastSeenAt;
    if (selected < 0 || age > greatestAge) {
      selected = static_cast<int>(i);
      greatestAge = age;
    }
  }

  SeenEntry &created = entries_[selected];
  created = SeenEntry{};
  created.active = true;
  created.key = key;
  created.lastSeenAt = now;
  created.firstTransmitter = transmitter;
  result.index = selected;
  result.isNew = true;
  return result;
}

SeenEntry *SeenCache::entry(int index) {
  if (index < 0 || index >= static_cast<int>(SEEN_CACHE_SIZE)) {
    return nullptr;
  }
  return &entries_[index];
}

const SeenEntry *SeenCache::entry(int index) const {
  if (index < 0 || index >= static_cast<int>(SEEN_CACHE_SIZE)) {
    return nullptr;
  }
  return &entries_[index];
}

void SeenCache::markRelayed(int index) {
  SeenEntry *seen = entry(index);
  if (seen != nullptr) {
    seen->relayed = true;
  }
}

AckQueue::AckQueue() { clear(); }

void AckQueue::clear() { memset(jobs_, 0, sizeof(jobs_)); }

bool AckQueue::schedule(uint16_t destination, uint32_t messageId,
                        uint32_t dueAt) {
  for (size_t i = 0; i < ACK_QUEUE_SIZE; ++i) {
    if (jobs_[i].active && jobs_[i].destination == destination &&
        jobs_[i].messageId == messageId) {
      return true;
    }
  }
  for (size_t i = 0; i < ACK_QUEUE_SIZE; ++i) {
    if (!jobs_[i].active) {
      jobs_[i].active = true;
      jobs_[i].destination = destination;
      jobs_[i].messageId = messageId;
      jobs_[i].dueAt = dueAt;
      return true;
    }
  }
  return false;
}

int AckQueue::dueIndex(uint32_t now) const {
  for (size_t i = 0; i < ACK_QUEUE_SIZE; ++i) {
    if (jobs_[i].active && timeReached(now, jobs_[i].dueAt)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

AckJob *AckQueue::job(int index) {
  if (index < 0 || index >= static_cast<int>(ACK_QUEUE_SIZE)) {
    return nullptr;
  }
  return &jobs_[index];
}

void AckQueue::remove(int index) {
  AckJob *selected = job(index);
  if (selected != nullptr) {
    *selected = AckJob{};
  }
}

RelayQueue::RelayQueue() { clear(); }

void RelayQueue::clear() { memset(jobs_, 0, sizeof(jobs_)); }

bool RelayQueue::schedule(const Packet &packet, int seenIndex,
                          uint32_t dueAt) {
  const FloodKey key = floodKeyFor(packet);
  for (size_t i = 0; i < RELAY_QUEUE_SIZE; ++i) {
    if (jobs_[i].active && floodKeyEqual(floodKeyFor(jobs_[i].packet), key)) {
      if (packet.envelope.hop < jobs_[i].packet.envelope.hop) {
        jobs_[i].packet = packet;
        jobs_[i].seenIndex = seenIndex;
      }
      return true;
    }
  }
  for (size_t i = 0; i < RELAY_QUEUE_SIZE; ++i) {
    if (!jobs_[i].active) {
      jobs_[i].active = true;
      jobs_[i].packet = packet;
      jobs_[i].seenIndex = seenIndex;
      jobs_[i].dueAt = dueAt;
      return true;
    }
  }
  return false;
}

size_t RelayQueue::cancel(const FloodKey &key) {
  size_t cancelled = 0;
  for (size_t i = 0; i < RELAY_QUEUE_SIZE; ++i) {
    if (jobs_[i].active && floodKeyEqual(floodKeyFor(jobs_[i].packet), key)) {
      jobs_[i] = RelayJob{};
      ++cancelled;
    }
  }
  return cancelled;
}

int RelayQueue::dueIndex(uint32_t now, bool presenceClass) const {
  for (size_t i = 0; i < RELAY_QUEUE_SIZE; ++i) {
    if (jobs_[i].active && timeReached(now, jobs_[i].dueAt) &&
        (jobs_[i].packet.envelope.type == PacketType::Presence) ==
            presenceClass) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

RelayJob *RelayQueue::job(int index) {
  if (index < 0 || index >= static_cast<int>(RELAY_QUEUE_SIZE)) {
    return nullptr;
  }
  return &jobs_[index];
}

const RelayJob *RelayQueue::job(int index) const {
  if (index < 0 || index >= static_cast<int>(RELAY_QUEUE_SIZE)) {
    return nullptr;
  }
  return &jobs_[index];
}

void RelayQueue::remove(int index) {
  RelayJob *selected = job(index);
  if (selected != nullptr) {
    *selected = RelayJob{};
  }
}

void DeliveryTracker::start(uint32_t messageId, uint16_t destination,
                            bool crewMessage, int historyIndex, uint32_t now,
                            uint32_t initialDelay) {
  phase_ = DeliveryPhase::Backoff;
  messageId_ = messageId;
  destination_ = destination;
  crewMessage_ = crewMessage;
  historyIndex_ = historyIndex;
  attempts_ = 0;
  deadline_ = now + initialDelay;
}

bool DeliveryTracker::sendDue(uint32_t now) const {
  return phase_ == DeliveryPhase::Backoff && timeReached(now, deadline_);
}

void DeliveryTracker::noteSent(uint32_t now, uint32_t ackTimeout) {
  if (phase_ != DeliveryPhase::Backoff) {
    return;
  }
  if (attempts_ < UINT8_MAX) {
    ++attempts_;
  }
  phase_ = DeliveryPhase::AwaitingAck;
  deadline_ = now + ackTimeout;
}

bool DeliveryTracker::handleTimeout(uint32_t now, uint32_t retryDelay,
                                    uint8_t maxAttempts) {
  if (phase_ != DeliveryPhase::AwaitingAck || !timeReached(now, deadline_)) {
    return false;
  }
  if (attempts_ >= maxAttempts) {
    phase_ = DeliveryPhase::Failed;
  } else {
    phase_ = DeliveryPhase::Backoff;
    deadline_ = now + retryDelay;
  }
  return true;
}

bool DeliveryTracker::confirm(uint32_t messageId) {
  if ((phase_ != DeliveryPhase::Backoff &&
       phase_ != DeliveryPhase::AwaitingAck) ||
      messageId_ != messageId) {
    return false;
  }
  phase_ = DeliveryPhase::Delivered;
  return true;
}

void DeliveryTracker::reset() {
  phase_ = DeliveryPhase::Idle;
  messageId_ = 0;
  destination_ = 0;
  crewMessage_ = false;
  historyIndex_ = -1;
  attempts_ = 0;
  deadline_ = 0;
}

}  // namespace mesh
