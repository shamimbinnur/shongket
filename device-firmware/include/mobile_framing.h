#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mobile {

constexpr uint8_t TRANSPORT_VERSION = 1;
constexpr size_t FRAGMENT_HEADER_SIZE = 8;
constexpr size_t MAX_JSON_SIZE = 512;
constexpr uint8_t FLAG_START = 0x01;
constexpr uint8_t FLAG_END = 0x02;

enum class FragmentResult : uint8_t {
  Accepted = 0,
  Complete,
  Invalid,
  OutOfOrder,
  TooLarge,
};

class Reassembler {
 public:
  Reassembler();
  void reset();
  FragmentResult ingest(const uint8_t *fragment, size_t length,
                        uint16_t &completedFrameId, const char *&completedJson,
                        size_t &completedLength);

 private:
  bool active_ = false;
  uint16_t frameId_ = 0;
  uint16_t totalLength_ = 0;
  uint16_t received_ = 0;
  char json_[MAX_JSON_SIZE + 1] = {};
};

bool buildFragment(uint16_t frameId, const char *json, size_t jsonLength,
                   size_t offset, size_t packetCapacity, uint8_t *output,
                   size_t &written, size_t &nextOffset);

}  // namespace mobile
