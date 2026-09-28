#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mesh {

constexpr uint8_t MAGIC_0 = 'L';
constexpr uint8_t MAGIC_1 = '3';
constexpr uint8_t MAX_HOPS = 3;
constexpr uint16_t BROADCAST_ADDRESS = 0xFFFF;
constexpr size_t SESSION_ID_SIZE = 7;
constexpr size_t HARDWARE_ID_SIZE = 6;
constexpr size_t MAX_NAME_LENGTH = 12;
constexpr size_t MAX_TEXT_LENGTH = 80;
constexpr size_t FRAME_HEADER_SIZE = 20;
constexpr size_t CCM_TAG_SIZE = 8;
constexpr size_t CCM_NONCE_SIZE = 13;
constexpr size_t ENVELOPE_SIZE = 17;
constexpr size_t MAX_PLAINTEXT_SIZE = 112;
constexpr size_t MAX_FRAME_SIZE =
    FRAME_HEADER_SIZE + MAX_PLAINTEXT_SIZE + CCM_TAG_SIZE;

enum class PacketType : uint8_t {
  Invalid = 0,
  Presence = 1,
  DirectMessage = 2,
  CrewMessage = 3,
  DeliveryAck = 4,
};

struct SessionId {
  uint8_t bytes[SESSION_ID_SIZE] = {};
};

struct FrameHeader {
  uint32_t crewId = 0;
  uint16_t transmitter = 0;
  SessionId transmitterSession;
  uint32_t frameSequence = 0;
  uint8_t plaintextLength = 0;
};

struct Envelope {
  PacketType type = PacketType::Invalid;
  uint16_t origin = 0;
  SessionId originSession;
  uint16_t destination = 0;
  uint32_t floodId = 0;
  uint8_t hop = 0;
};

struct PresencePayload {
  uint8_t hardwareId[HARDWARE_ID_SIZE] = {};
  char name[MAX_NAME_LENGTH + 1] = {};
  bool gpsValid = false;
  int32_t latitudeE7 = 0;
  int32_t longitudeE7 = 0;
  uint8_t satellites = 0;
  uint16_t fixAgeSeconds = 0;
};

struct MessagePayload {
  uint32_t messageId = 0;
  uint32_t timestamp = 0;
  char text[MAX_TEXT_LENGTH + 1] = {};
};

struct AckPayload {
  uint32_t messageId = 0;
};

struct Packet {
  Envelope envelope;
  PresencePayload presence;
  MessagePayload message;
  AckPayload ack;
};

bool validNodeAddress(uint16_t address);
bool validPacketType(PacketType type);
bool sessionEqual(const SessionId &left, const SessionId &right);
bool sessionIsZero(const SessionId &session);
bool shouldStoreMessage(const Envelope &envelope, uint16_t localAddress);

bool encodeHeader(const FrameHeader &header,
                  uint8_t output[FRAME_HEADER_SIZE]);
bool decodeHeader(const uint8_t *input, size_t length, FrameHeader &header);
void buildNonce(const FrameHeader &header, uint8_t nonce[CCM_NONCE_SIZE]);

bool encodePlaintext(const Packet &packet, uint8_t *output, size_t capacity,
                     size_t &written);
bool decodePlaintext(const uint8_t *input, size_t length, Packet &packet);

}  // namespace mesh
