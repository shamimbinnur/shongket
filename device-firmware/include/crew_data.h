#pragma once

#include "mesh_protocol.h"
#include "mesh_state.h"

#include <stddef.h>
#include <stdint.h>

namespace crew {

constexpr size_t MAX_PEERS = 11;
constexpr uint32_t OFFLINE_TIMEOUT_MS = 120000UL;
constexpr size_t MESSAGE_HISTORY_SIZE = 20;

struct PeerRecord {
  bool used = false;
  bool online = false;
  bool addressConflict = false;
  uint16_t address = 0;
  char name[mesh::MAX_NAME_LENGTH + 1] = {};
  uint8_t hardwareId[mesh::HARDWARE_ID_SIZE] = {};
  mesh::SessionId presenceSession;
  uint32_t lastPresenceFlood = 0;
  uint32_t lastSeenAt = 0;
  uint8_t hop = 0;
  uint16_t lastRelay = 0;
  int16_t rssi = 0;
  bool hasLocation = false;
  bool gpsCurrent = false;
  int32_t latitudeE7 = 0;
  int32_t longitudeE7 = 0;
  uint8_t satellites = 0;
  uint16_t locationAgeAtReceipt = 0;
  uint32_t locationReceivedAt = 0;
};

enum class PresenceUpdate : uint8_t {
  Added = 0,
  Updated,
  ReplayIgnored,
  AddressConflict,
  TableFull,
};

class PeerDirectory {
 public:
  PeerDirectory();
  void clear();
  PresenceUpdate updatePresence(uint16_t address,
                                const mesh::SessionId &originSession,
                                uint32_t floodId,
                                const mesh::PresencePayload &presence,
                                uint8_t hop, uint16_t lastRelay, int16_t rssi,
                                uint32_t now);
  void touch(uint16_t address, uint8_t hop, uint16_t lastRelay, int16_t rssi,
             uint32_t now);
  bool refreshOnline(uint32_t now);
  size_t onlineCount(uint32_t now);
  int find(uint16_t address) const;
  PeerRecord *record(int index);
  const PeerRecord *record(int index) const;
  uint32_t locationAgeSeconds(const PeerRecord &peer, uint32_t now) const;
  uint32_t revision() const { return revision_; }

 private:
  int allocate(uint32_t now);
  PeerRecord peers_[MAX_PEERS];
  uint32_t revision_ = 0;
};

enum class MessageDirection : uint8_t {
  Incoming = 0,
  Outgoing,
};

enum class MessageDelivery : uint8_t {
  Received = 0,
  Sending,
  Delivered,
  Failed,
};

enum class MessageSource : uint8_t {
  Radio = 0,
  Keypad,
  Mobile,
};

struct MessageRecord {
  bool used = false;
  MessageDirection direction = MessageDirection::Incoming;
  MessageSource source = MessageSource::Radio;
  bool crewMessage = false;
  bool unread = false;
  uint16_t peerAddress = 0;
  char peerName[mesh::MAX_NAME_LENGTH + 1] = {};
  uint16_t originAddress = 0;
  uint32_t messageId = 0;
  uint32_t timestamp = 0;
  uint32_t storedAt = 0;
  MessageDelivery delivery = MessageDelivery::Received;
  char text[mesh::MAX_TEXT_LENGTH + 1] = {};
};

class MessageHistory {
 public:
  MessageHistory();
  void clear();
  int add(const MessageRecord &message);
  bool containsIncoming(uint16_t origin, uint32_t messageId) const;
  bool updateDelivery(int physicalIndex, MessageDelivery delivery);
  bool markRead(int physicalIndex);
  bool setUnread(int physicalIndex, bool unread);
  size_t markAllRead();
  int find(uint16_t origin, uint32_t messageId) const;
  size_t count() const { return count_; }
  size_t unreadCount() const;
  uint32_t revision() const { return revision_; }
  int physicalIndexForNewest(size_t newestOffset) const;
  MessageRecord *record(int physicalIndex);
  const MessageRecord *record(int physicalIndex) const;

 private:
  MessageRecord messages_[MESSAGE_HISTORY_SIZE];
  size_t nextWrite_ = 0;
  size_t count_ = 0;
  uint32_t revision_ = 0;
};

}  // namespace crew
