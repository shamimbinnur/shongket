#include "mobile_framing.h"

#include <string.h>

namespace mobile {
namespace {

uint16_t readU16(const uint8_t *value) {
  return static_cast<uint16_t>((static_cast<uint16_t>(value[0]) << 8U) |
                               value[1]);
}

void writeU16(uint8_t *output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value >> 8U);
  output[1] = static_cast<uint8_t>(value);
}

}  // namespace

Reassembler::Reassembler() { reset(); }

void Reassembler::reset() {
  active_ = false;
  frameId_ = 0;
  totalLength_ = 0;
  received_ = 0;
  json_[0] = '\0';
}

FragmentResult Reassembler::ingest(const uint8_t *fragment, size_t length,
                                   uint16_t &completedFrameId,
                                   const char *&completedJson,
                                   size_t &completedLength) {
  completedFrameId = 0;
  completedJson = nullptr;
  completedLength = 0;
  if (fragment == nullptr || length <= FRAGMENT_HEADER_SIZE ||
      fragment[0] != TRANSPORT_VERSION || (fragment[1] & ~0x03U) != 0) {
    reset();
    return FragmentResult::Invalid;
  }
  const uint8_t flags = fragment[1];
  const uint16_t frameId = readU16(fragment + 2);
  const uint16_t totalLength = readU16(fragment + 4);
  const uint16_t offset = readU16(fragment + 6);
  const size_t payloadLength = length - FRAGMENT_HEADER_SIZE;
  if (frameId == 0 || totalLength == 0 || totalLength > MAX_JSON_SIZE ||
      offset > totalLength || payloadLength > totalLength - offset) {
    reset();
    return totalLength > MAX_JSON_SIZE ? FragmentResult::TooLarge
                                       : FragmentResult::Invalid;
  }
  if ((flags & FLAG_START) != 0) {
    if (offset != 0) {
      reset();
      return FragmentResult::Invalid;
    }
    active_ = true;
    frameId_ = frameId;
    totalLength_ = totalLength;
    received_ = 0;
  }
  if (!active_ || frameId != frameId_ || totalLength != totalLength_ ||
      offset != received_) {
    reset();
    return FragmentResult::OutOfOrder;
  }
  memcpy(json_ + received_, fragment + FRAGMENT_HEADER_SIZE, payloadLength);
  received_ += payloadLength;
  const bool atEnd = received_ == totalLength_;
  if (((flags & FLAG_END) != 0) != atEnd) {
    if ((flags & FLAG_END) != 0 || atEnd) {
      reset();
      return FragmentResult::Invalid;
    }
  }
  if (!atEnd) return FragmentResult::Accepted;

  json_[received_] = '\0';
  completedFrameId = frameId_;
  completedJson = json_;
  completedLength = received_;
  active_ = false;
  return FragmentResult::Complete;
}

bool buildFragment(uint16_t frameId, const char *json, size_t jsonLength,
                   size_t offset, size_t packetCapacity, uint8_t *output,
                   size_t &written, size_t &nextOffset) {
  written = 0;
  nextOffset = offset;
  if (frameId == 0 || json == nullptr || output == nullptr || jsonLength == 0 ||
      jsonLength > MAX_JSON_SIZE || offset >= jsonLength ||
      packetCapacity <= FRAGMENT_HEADER_SIZE) {
    return false;
  }
  size_t payloadLength = packetCapacity - FRAGMENT_HEADER_SIZE;
  if (payloadLength > jsonLength - offset) payloadLength = jsonLength - offset;
  output[0] = TRANSPORT_VERSION;
  output[1] = (offset == 0 ? FLAG_START : 0) |
              (offset + payloadLength == jsonLength ? FLAG_END : 0);
  writeU16(output + 2, frameId);
  writeU16(output + 4, static_cast<uint16_t>(jsonLength));
  writeU16(output + 6, static_cast<uint16_t>(offset));
  memcpy(output + FRAGMENT_HEADER_SIZE, json + offset, payloadLength);
  written = FRAGMENT_HEADER_SIZE + payloadLength;
  nextOffset = offset + payloadLength;
  return true;
}

}  // namespace mobile
