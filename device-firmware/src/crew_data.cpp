#include "crew_data.h"

#include <string.h>

namespace crew {
namespace {

bool hardwareEqual(const uint8_t *left, const uint8_t *right) {
  return memcmp(left, right, mesh::HARDWARE_ID_SIZE) == 0;
}

bool sequenceNewer(uint32_t candidate, uint32_t previous) {
  return static_cast<int32_t>(candidate - previous) > 0;
}

}  // namespace

PeerDirectory::PeerDirectory() { clear(); }

void PeerDirectory::clear() {
  memset(peers_, 0, sizeof(peers_));
  revision_ = 0;
}

int PeerDirectory::find(uint16_t address) const {
  for (size_t i = 0; i < MAX_PEERS; ++i) {
    if (peers_[i].used && peers_[i].address == address) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int PeerDirectory::allocate(uint32_t now) {
  refreshOnline(now);
  for (size_t i = 0; i < MAX_PEERS; ++i) {
    if (!peers_[i].used) {
      return static_cast<int>(i);
    }
  }

  int oldestOffline = -1;
  uint32_t greatestAge = 0;
  for (size_t i = 0; i < MAX_PEERS; ++i) {
    if (!peers_[i].online) {
      const uint32_t age = now - peers_[i].lastSeenAt;
      if (oldestOffline < 0 || age > greatestAge) {
        oldestOffline = static_cast<int>(i);
        greatestAge = age;
      }
    }
  }
  return oldestOffline;
}

PresenceUpdate PeerDirectory::updatePresence(
    uint16_t address, const mesh::SessionId &originSession, uint32_t floodId,
    const mesh::PresencePayload &presence, uint8_t hop, uint16_t lastRelay,
    int16_t rssi, uint32_t now) {
  int index = find(address);
  const bool isNewPeer = index < 0;
  if (index >= 0) {
    PeerRecord &existing = peers_[index];
    if (!hardwareEqual(existing.hardwareId, presence.hardwareId)) {
      existing.addressConflict = true;
      existing.online = true;
      existing.lastSeenAt = now;
      ++revision_;
      return PresenceUpdate::AddressConflict;
    }
    if (mesh::sessionEqual(existing.presenceSession, originSession) &&
        !sequenceNewer(floodId, existing.lastPresenceFlood)) {
      return PresenceUpdate::ReplayIgnored;
    }
  } else {
    index = allocate(now);
    if (index < 0) {
      return PresenceUpdate::TableFull;
    }
    peers_[index] = PeerRecord{};
    peers_[index].used = true;
    peers_[index].address = address;
    memcpy(peers_[index].hardwareId, presence.hardwareId,
           mesh::HARDWARE_ID_SIZE);
  }

  PeerRecord &peer = peers_[index];
  peer.online = true;
  if (isNewPeer) peer.addressConflict = false;
  memcpy(peer.name, presence.name, sizeof(peer.name));
  peer.presenceSession = originSession;
  peer.lastPresenceFlood = floodId;
  peer.lastSeenAt = now;
  peer.hop = hop;
  peer.lastRelay = lastRelay;
  peer.rssi = rssi;
  peer.gpsCurrent = presence.gpsValid;
  if (presence.gpsValid) {
    peer.hasLocation = true;
    peer.latitudeE7 = presence.latitudeE7;
    peer.longitudeE7 = presence.longitudeE7;
    peer.satellites = presence.satellites;
    peer.locationAgeAtReceipt = presence.fixAgeSeconds;
    peer.locationReceivedAt = now;
  }
  ++revision_;
  return isNewPeer ? PresenceUpdate::Added : PresenceUpdate::Updated;
}

void PeerDirectory::touch(uint16_t address, uint8_t hop, uint16_t lastRelay,
                          int16_t rssi, uint32_t now) {
  const int index = find(address);
  if (index < 0) {
    return;
  }
  PeerRecord &peer = peers_[index];
  const bool changed = !peer.online || peer.hop != hop ||
                       peer.lastRelay != lastRelay || peer.rssi != rssi;
  peer.online = true;
  peer.lastSeenAt = now;
  peer.hop = hop;
  peer.lastRelay = lastRelay;
  peer.rssi = rssi;
  if (changed) {
    ++revision_;
  }
}

bool PeerDirectory::refreshOnline(uint32_t now) {
  bool changed = false;
  for (size_t i = 0; i < MAX_PEERS; ++i) {
    if (!peers_[i].used) {
      continue;
    }
    const bool shouldBeOnline =
        !mesh::intervalElapsed(now, peers_[i].lastSeenAt, OFFLINE_TIMEOUT_MS);
    if (shouldBeOnline != peers_[i].online) {
      peers_[i].online = shouldBeOnline;
      changed = true;
    }
  }
  if (changed) {
    ++revision_;
  }
  return changed;
}

size_t PeerDirectory::onlineCount(uint32_t now) {
  refreshOnline(now);
  size_t result = 0;
  for (size_t i = 0; i < MAX_PEERS; ++i) {
    result += peers_[i].used && peers_[i].online ? 1 : 0;
  }
  return result;
}

PeerRecord *PeerDirectory::record(int index) {
  if (index < 0 || index >= static_cast<int>(MAX_PEERS)) {
    return nullptr;
  }
  return &peers_[index];
}

const PeerRecord *PeerDirectory::record(int index) const {
  if (index < 0 || index >= static_cast<int>(MAX_PEERS)) {
    return nullptr;
  }
  return &peers_[index];
}

uint32_t PeerDirectory::locationAgeSeconds(const PeerRecord &peer,
                                           uint32_t now) const {
  if (!peer.hasLocation) {
    return UINT32_MAX;
  }
  return static_cast<uint32_t>(peer.locationAgeAtReceipt) +
         static_cast<uint32_t>(now - peer.locationReceivedAt) / 1000UL;
}

MessageHistory::MessageHistory() { clear(); }

void MessageHistory::clear() {
  memset(messages_, 0, sizeof(messages_));
  nextWrite_ = 0;
  count_ = 0;
  revision_ = 0;
}

int MessageHistory::add(const MessageRecord &message) {
  const int index = static_cast<int>(nextWrite_);
  messages_[nextWrite_] = message;
  messages_[nextWrite_].used = true;
  nextWrite_ = (nextWrite_ + 1) % MESSAGE_HISTORY_SIZE;
  if (count_ < MESSAGE_HISTORY_SIZE) {
    ++count_;
  }
  ++revision_;
  return index;
}

bool MessageHistory::containsIncoming(uint16_t origin,
                                      uint32_t messageId) const {
  for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; ++i) {
    if (messages_[i].used &&
        messages_[i].direction == MessageDirection::Incoming &&
        messages_[i].originAddress == origin &&
        messages_[i].messageId == messageId) {
      return true;
    }
  }
  return false;
}

bool MessageHistory::updateDelivery(int physicalIndex,
                                    MessageDelivery delivery) {
  MessageRecord *message = record(physicalIndex);
  if (message == nullptr || !message->used) {
    return false;
  }
  if (message->delivery == delivery) {
    return true;
  }
  message->delivery = delivery;
  ++revision_;
  return true;
}

bool MessageHistory::markRead(int physicalIndex) {
  MessageRecord *message = record(physicalIndex);
  if (message == nullptr || !message->used) {
    return false;
  }
  if (!message->unread) {
    return true;
  }
  message->unread = false;
  ++revision_;
  return true;
}

bool MessageHistory::setUnread(int physicalIndex, bool unread) {
  MessageRecord *message = record(physicalIndex);
  if (message == nullptr || !message->used) return false;
  if (message->unread == unread) return true;
  message->unread = unread; ++revision_;
  return true;
}

size_t MessageHistory::markAllRead() {
  size_t changed = 0;
  for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; ++i) {
    if (messages_[i].used && messages_[i].unread) {
      messages_[i].unread = false;
      ++changed;
    }
  }
  if (changed > 0) {
    ++revision_;
  }
  return changed;
}

int MessageHistory::find(uint16_t origin, uint32_t messageId) const {
  for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; ++i) {
    if (messages_[i].used && messages_[i].originAddress == origin &&
        messages_[i].messageId == messageId) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

size_t MessageHistory::unreadCount() const {
  size_t result = 0;
  for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; ++i) {
    result += messages_[i].used && messages_[i].unread ? 1 : 0;
  }
  return result;
}

int MessageHistory::physicalIndexForNewest(size_t newestOffset) const {
  if (newestOffset >= count_) {
    return -1;
  }
  const size_t newest =
      (nextWrite_ + MESSAGE_HISTORY_SIZE - 1) % MESSAGE_HISTORY_SIZE;
  return static_cast<int>((newest + MESSAGE_HISTORY_SIZE - newestOffset) %
                          MESSAGE_HISTORY_SIZE);
}

MessageRecord *MessageHistory::record(int physicalIndex) {
  if (physicalIndex < 0 ||
      physicalIndex >= static_cast<int>(MESSAGE_HISTORY_SIZE)) {
    return nullptr;
  }
  return &messages_[physicalIndex];
}

const MessageRecord *MessageHistory::record(int physicalIndex) const {
  if (physicalIndex < 0 ||
      physicalIndex >= static_cast<int>(MESSAGE_HISTORY_SIZE)) {
    return nullptr;
  }
  return &messages_[physicalIndex];
}

}  // namespace crew
