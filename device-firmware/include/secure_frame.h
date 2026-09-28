#pragma once

#include "mesh_protocol.h"

#include <stddef.h>
#include <stdint.h>

#include <mbedtls/ccm.h>

namespace mesh {

enum class FrameOpenResult : uint8_t {
  Ok = 0,
  Malformed,
  WrongCrew,
  AuthenticationFailed,
  DecodeFailed,
};

class SecureFrameCodec {
 public:
  SecureFrameCodec();
  ~SecureFrameCodec();

  bool begin(const uint8_t key[16]);
  bool seal(const Packet &packet, uint32_t crewId, uint16_t transmitter,
            const SessionId &transmitterSession, uint32_t frameSequence,
            uint8_t *output, size_t capacity, size_t &written);
  FrameOpenResult open(const uint8_t *frame, size_t length,
                       uint32_t expectedCrewId, FrameHeader &header,
                       Packet &packet);
  bool ready() const { return ready_; }

  static bool knownAnswerSelfTest();
  static bool comprehensiveSelfTest();

 private:
  mbedtls_ccm_context context_;
  bool ready_ = false;
};

}  // namespace mesh
