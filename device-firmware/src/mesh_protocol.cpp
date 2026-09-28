#include "mesh_protocol.h"

#include <string.h>

namespace mesh {
namespace {

void writeU16(uint8_t *output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value >> 8U);
  output[1] = static_cast<uint8_t>(value);
}

void writeU32(uint8_t *output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value >> 24U);
  output[1] = static_cast<uint8_t>(value >> 16U);
  output[2] = static_cast<uint8_t>(value >> 8U);
  output[3] = static_cast<uint8_t>(value);
}

uint16_t readU16(const uint8_t *input) {
  return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8U) |
                               static_cast<uint16_t>(input[1]));
}

uint32_t readU32(const uint8_t *input) {
  return (static_cast<uint32_t>(input[0]) << 24U) |
         (static_cast<uint32_t>(input[1]) << 16U) |
         (static_cast<uint32_t>(input[2]) << 8U) |
         static_cast<uint32_t>(input[3]);
}

bool printable(const char *value, size_t maximum, bool allowEmpty) {
  if (value == nullptr) {
    return false;
  }
  const size_t length = strnlen(value, maximum + 1);
  if (length > maximum || (!allowEmpty && length == 0)) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c < 0x20 || c > 0x7e) {
      return false;
    }
  }
  return true;
}

bool encodeEnvelope(const Envelope &envelope, uint8_t *output) {
  if (!validPacketType(envelope.type) || !validNodeAddress(envelope.origin) ||
      sessionIsZero(envelope.originSession) || envelope.hop > MAX_HOPS) {
    return false;
  }
  if (envelope.type == PacketType::Presence ||
      envelope.type == PacketType::CrewMessage) {
    if (envelope.destination != BROADCAST_ADDRESS) {
      return false;
    }
  } else if (!validNodeAddress(envelope.destination)) {
    return false;
  }

  output[0] = static_cast<uint8_t>(envelope.type);
  writeU16(output + 1, envelope.origin);
  memcpy(output + 3, envelope.originSession.bytes, SESSION_ID_SIZE);
  writeU16(output + 10, envelope.destination);
  writeU32(output + 12, envelope.floodId);
  output[16] = envelope.hop;
  return true;
}

bool decodeEnvelope(const uint8_t *input, size_t length, Envelope &envelope) {
  if (length < ENVELOPE_SIZE) {
    return false;
  }
  envelope.type = static_cast<PacketType>(input[0]);
  envelope.origin = readU16(input + 1);
  memcpy(envelope.originSession.bytes, input + 3, SESSION_ID_SIZE);
  envelope.destination = readU16(input + 10);
  envelope.floodId = readU32(input + 12);
  envelope.hop = input[16];

  uint8_t check[ENVELOPE_SIZE];
  return encodeEnvelope(envelope, check);
}

}  // namespace

bool validNodeAddress(uint16_t address) {
  return address > 0 && address < BROADCAST_ADDRESS;
}

bool validPacketType(PacketType type) {
  return type == PacketType::Presence || type == PacketType::DirectMessage ||
         type == PacketType::CrewMessage || type == PacketType::DeliveryAck;
}

bool sessionEqual(const SessionId &left, const SessionId &right) {
  return memcmp(left.bytes, right.bytes, SESSION_ID_SIZE) == 0;
}

bool sessionIsZero(const SessionId &session) {
  uint8_t combined = 0;
  for (size_t i = 0; i < SESSION_ID_SIZE; ++i) {
    combined |= session.bytes[i];
  }
  return combined == 0;
}

bool shouldStoreMessage(const Envelope &envelope, uint16_t localAddress) {
  if (!validNodeAddress(localAddress) || envelope.origin == localAddress) {
    return false;
  }
  if (envelope.type == PacketType::CrewMessage) {
    return envelope.destination == BROADCAST_ADDRESS;
  }
  return envelope.type == PacketType::DirectMessage &&
         envelope.destination == localAddress;
}

bool encodeHeader(const FrameHeader &header,
                  uint8_t output[FRAME_HEADER_SIZE]) {
  if (output == nullptr || header.crewId == 0 ||
      !validNodeAddress(header.transmitter) ||
      sessionIsZero(header.transmitterSession) ||
      header.plaintextLength < ENVELOPE_SIZE ||
      header.plaintextLength > MAX_PLAINTEXT_SIZE) {
    return false;
  }
  output[0] = MAGIC_0;
  output[1] = MAGIC_1;
  writeU32(output + 2, header.crewId);
  writeU16(output + 6, header.transmitter);
  memcpy(output + 8, header.transmitterSession.bytes, SESSION_ID_SIZE);
  writeU32(output + 15, header.frameSequence);
  output[19] = header.plaintextLength;
  return true;
}

bool decodeHeader(const uint8_t *input, size_t length, FrameHeader &header) {
  header = FrameHeader{};
  if (input == nullptr || length < FRAME_HEADER_SIZE || input[0] != MAGIC_0 ||
      input[1] != MAGIC_1) {
    return false;
  }
  header.crewId = readU32(input + 2);
  header.transmitter = readU16(input + 6);
  memcpy(header.transmitterSession.bytes, input + 8, SESSION_ID_SIZE);
  header.frameSequence = readU32(input + 15);
  header.plaintextLength = input[19];
  uint8_t check[FRAME_HEADER_SIZE];
  return encodeHeader(header, check);
}

void buildNonce(const FrameHeader &header, uint8_t nonce[CCM_NONCE_SIZE]) {
  writeU16(nonce, header.transmitter);
  memcpy(nonce + 2, header.transmitterSession.bytes, SESSION_ID_SIZE);
  writeU32(nonce + 9, header.frameSequence);
}

bool encodePlaintext(const Packet &packet, uint8_t *output, size_t capacity,
                     size_t &written) {
  written = 0;
  if (output == nullptr || capacity < ENVELOPE_SIZE ||
      !encodeEnvelope(packet.envelope, output)) {
    return false;
  }
  size_t cursor = ENVELOPE_SIZE;

  if (packet.envelope.type == PacketType::Presence) {
    const size_t nameLength = strnlen(packet.presence.name, MAX_NAME_LENGTH + 1);
    constexpr size_t fixedTail = HARDWARE_ID_SIZE + 1 + 1 + 4 + 4 + 1 + 2;
    if (!printable(packet.presence.name, MAX_NAME_LENGTH, false) ||
        capacity < cursor + fixedTail + nameLength ||
        packet.presence.latitudeE7 < -900000000 ||
        packet.presence.latitudeE7 > 900000000 ||
        packet.presence.longitudeE7 < -1800000000 ||
        packet.presence.longitudeE7 > 1800000000) {
      return false;
    }
    memcpy(output + cursor, packet.presence.hardwareId, HARDWARE_ID_SIZE);
    cursor += HARDWARE_ID_SIZE;
    output[cursor++] = static_cast<uint8_t>(nameLength);
    memcpy(output + cursor, packet.presence.name, nameLength);
    cursor += nameLength;
    output[cursor++] = packet.presence.gpsValid ? 1 : 0;
    writeU32(output + cursor,
             static_cast<uint32_t>(packet.presence.latitudeE7));
    cursor += 4;
    writeU32(output + cursor,
             static_cast<uint32_t>(packet.presence.longitudeE7));
    cursor += 4;
    output[cursor++] = packet.presence.satellites;
    writeU16(output + cursor, packet.presence.fixAgeSeconds);
    cursor += 2;
  } else if (packet.envelope.type == PacketType::DirectMessage ||
             packet.envelope.type == PacketType::CrewMessage) {
    const size_t textLength = strnlen(packet.message.text, MAX_TEXT_LENGTH + 1);
    if (!printable(packet.message.text, MAX_TEXT_LENGTH, false) ||
        capacity < cursor + 9 + textLength) {
      return false;
    }
    writeU32(output + cursor, packet.message.messageId);
    cursor += 4;
    writeU32(output + cursor, packet.message.timestamp);
    cursor += 4;
    output[cursor++] = static_cast<uint8_t>(textLength);
    memcpy(output + cursor, packet.message.text, textLength);
    cursor += textLength;
  } else if (packet.envelope.type == PacketType::DeliveryAck) {
    if (capacity < cursor + 4) {
      return false;
    }
    writeU32(output + cursor, packet.ack.messageId);
    cursor += 4;
  } else {
    return false;
  }

  if (cursor > MAX_PLAINTEXT_SIZE) {
    return false;
  }
  written = cursor;
  return true;
}

bool decodePlaintext(const uint8_t *input, size_t length, Packet &packet) {
  packet = Packet{};
  if (input == nullptr || length < ENVELOPE_SIZE ||
      !decodeEnvelope(input, length, packet.envelope)) {
    return false;
  }
  size_t cursor = ENVELOPE_SIZE;

  if (packet.envelope.type == PacketType::Presence) {
    if (length < cursor + HARDWARE_ID_SIZE + 1) {
      return false;
    }
    memcpy(packet.presence.hardwareId, input + cursor, HARDWARE_ID_SIZE);
    cursor += HARDWARE_ID_SIZE;
    const size_t nameLength = input[cursor++];
    if (nameLength == 0 || nameLength > MAX_NAME_LENGTH ||
        length != cursor + nameLength + 12) {
      return false;
    }
    memcpy(packet.presence.name, input + cursor, nameLength);
    packet.presence.name[nameLength] = '\0';
    cursor += nameLength;
    const uint8_t flags = input[cursor++];
    if ((flags & 0xFEU) != 0) {
      return false;
    }
    packet.presence.gpsValid = (flags & 1U) != 0;
    packet.presence.latitudeE7 = static_cast<int32_t>(readU32(input + cursor));
    cursor += 4;
    packet.presence.longitudeE7 = static_cast<int32_t>(readU32(input + cursor));
    cursor += 4;
    packet.presence.satellites = input[cursor++];
    packet.presence.fixAgeSeconds = readU16(input + cursor);
    cursor += 2;
    return cursor == length &&
           printable(packet.presence.name, MAX_NAME_LENGTH, false) &&
           packet.presence.latitudeE7 >= -900000000 &&
           packet.presence.latitudeE7 <= 900000000 &&
           packet.presence.longitudeE7 >= -1800000000 &&
           packet.presence.longitudeE7 <= 1800000000;
  }

  if (packet.envelope.type == PacketType::DirectMessage ||
      packet.envelope.type == PacketType::CrewMessage) {
    if (length < cursor + 9) {
      return false;
    }
    packet.message.messageId = readU32(input + cursor);
    cursor += 4;
    packet.message.timestamp = readU32(input + cursor);
    cursor += 4;
    const size_t textLength = input[cursor++];
    if (textLength == 0 || textLength > MAX_TEXT_LENGTH ||
        length != cursor + textLength) {
      return false;
    }
    memcpy(packet.message.text, input + cursor, textLength);
    packet.message.text[textLength] = '\0';
    return printable(packet.message.text, MAX_TEXT_LENGTH, false);
  }

  if (packet.envelope.type == PacketType::DeliveryAck) {
    if (length != cursor + 4) {
      return false;
    }
    packet.ack.messageId = readU32(input + cursor);
    return true;
  }

  return false;
}

}  // namespace mesh
