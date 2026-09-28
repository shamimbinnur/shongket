#include "secure_frame.h"

#include <mbedtls/cipher.h>

#include <string.h>

namespace mesh {

SecureFrameCodec::SecureFrameCodec() { mbedtls_ccm_init(&context_); }

SecureFrameCodec::~SecureFrameCodec() { mbedtls_ccm_free(&context_); }

bool SecureFrameCodec::begin(const uint8_t key[16]) {
  if (key == nullptr) {
    return false;
  }
  uint8_t combined = 0;
  for (size_t i = 0; i < 16; ++i) {
    combined |= key[i];
  }
  if (combined == 0) {
    return false;
  }
  ready_ = mbedtls_ccm_setkey(&context_, MBEDTLS_CIPHER_ID_AES, key, 128) == 0;
  return ready_;
}

bool SecureFrameCodec::seal(const Packet &packet, uint32_t crewId,
                            uint16_t transmitter,
                            const SessionId &transmitterSession,
                            uint32_t frameSequence, uint8_t *output,
                            size_t capacity, size_t &written) {
  written = 0;
  if (!ready_ || output == nullptr) {
    return false;
  }

  uint8_t plaintext[MAX_PLAINTEXT_SIZE];
  size_t plaintextLength = 0;
  if (!encodePlaintext(packet, plaintext, sizeof(plaintext), plaintextLength) ||
      plaintextLength > 255) {
    return false;
  }

  FrameHeader header;
  header.crewId = crewId;
  header.transmitter = transmitter;
  header.transmitterSession = transmitterSession;
  header.frameSequence = frameSequence;
  header.plaintextLength = static_cast<uint8_t>(plaintextLength);
  uint8_t encodedHeader[FRAME_HEADER_SIZE];
  if (!encodeHeader(header, encodedHeader)) {
    return false;
  }

  const size_t frameLength =
      FRAME_HEADER_SIZE + plaintextLength + CCM_TAG_SIZE;
  if (capacity < frameLength || frameLength > MAX_FRAME_SIZE) {
    return false;
  }

  memcpy(output, encodedHeader, FRAME_HEADER_SIZE);
  uint8_t nonce[CCM_NONCE_SIZE];
  buildNonce(header, nonce);
  const int result = mbedtls_ccm_encrypt_and_tag(
      &context_, plaintextLength, nonce, sizeof(nonce), encodedHeader,
      sizeof(encodedHeader), plaintext, output + FRAME_HEADER_SIZE,
      output + FRAME_HEADER_SIZE + plaintextLength, CCM_TAG_SIZE);
  if (result != 0) {
    return false;
  }
  written = frameLength;
  return true;
}

FrameOpenResult SecureFrameCodec::open(const uint8_t *frame, size_t length,
                                       uint32_t expectedCrewId,
                                       FrameHeader &header, Packet &packet) {
  header = FrameHeader{};
  packet = Packet{};
  if (!ready_ || frame == nullptr || length < FRAME_HEADER_SIZE + CCM_TAG_SIZE ||
      length > MAX_FRAME_SIZE ||
      !decodeHeader(frame, FRAME_HEADER_SIZE, header)) {
    return FrameOpenResult::Malformed;
  }
  if (header.crewId != expectedCrewId) {
    return FrameOpenResult::WrongCrew;
  }
  const size_t expectedLength =
      FRAME_HEADER_SIZE + header.plaintextLength + CCM_TAG_SIZE;
  if (length != expectedLength) {
    return FrameOpenResult::Malformed;
  }

  uint8_t nonce[CCM_NONCE_SIZE];
  buildNonce(header, nonce);
  uint8_t plaintext[MAX_PLAINTEXT_SIZE];
  const int result = mbedtls_ccm_auth_decrypt(
      &context_, header.plaintextLength, nonce, sizeof(nonce), frame,
      FRAME_HEADER_SIZE, frame + FRAME_HEADER_SIZE, plaintext,
      frame + FRAME_HEADER_SIZE + header.plaintextLength, CCM_TAG_SIZE);
  if (result != 0) {
    return FrameOpenResult::AuthenticationFailed;
  }
  if (!decodePlaintext(plaintext, header.plaintextLength, packet)) {
    return FrameOpenResult::DecodeFailed;
  }
  return FrameOpenResult::Ok;
}

bool SecureFrameCodec::knownAnswerSelfTest() {
  // RFC 3610 packet vector #1, AES-CCM with an 8-byte authentication tag.
  const uint8_t key[16] = {
      0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
      0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
  };
  const uint8_t nonce[13] = {0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
                             0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
  const uint8_t aad[8] = {0x00, 0x01, 0x02, 0x03,
                          0x04, 0x05, 0x06, 0x07};
  // RFC 3610 vector #1 has 8 AAD octets followed by 23 message octets
  // (0x08 through 0x1E), then an 8-octet authentication value.
  const uint8_t plaintext[23] = {
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
  };
  const uint8_t expected[31] = {
      0x58, 0x8C, 0x97, 0x9A, 0x61, 0xC6, 0x63, 0xD2,
      0xF0, 0x66, 0xD0, 0xC2, 0xC0, 0xF9, 0x89, 0x80,
      0x6D, 0x5F, 0x6B, 0x61, 0xDA, 0xC3, 0x84, 0x17,
      0xE8, 0xD1, 0x2C, 0xFD, 0xF9, 0x26, 0xE0,
  };

  mbedtls_ccm_context testContext;
  mbedtls_ccm_init(&testContext);
  bool ok = mbedtls_ccm_setkey(&testContext, MBEDTLS_CIPHER_ID_AES, key, 128) == 0;
  uint8_t output[sizeof(expected)] = {};
  if (ok) {
    ok = mbedtls_ccm_encrypt_and_tag(
             &testContext, sizeof(plaintext), nonce, sizeof(nonce), aad,
             sizeof(aad), plaintext, output, output + sizeof(plaintext), 8) ==
         0;
  }
  if (ok) {
    ok = memcmp(output, expected, sizeof(expected)) == 0;
  }
  uint8_t recovered[sizeof(plaintext)] = {};
  if (ok) {
    ok = mbedtls_ccm_auth_decrypt(
             &testContext, sizeof(plaintext), nonce, sizeof(nonce), aad,
             sizeof(aad), output, recovered, output + sizeof(plaintext), 8) ==
         0;
  }
  if (ok) {
    ok = memcmp(recovered, plaintext, sizeof(plaintext)) == 0;
  }
  mbedtls_ccm_free(&testContext);
  return ok;
}

bool SecureFrameCodec::comprehensiveSelfTest() {
  if (!knownAnswerSelfTest()) {
    return false;
  }
  const uint8_t key[16] = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                           0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};
  uint8_t wrongKey[16];
  memcpy(wrongKey, key, sizeof(key));
  wrongKey[0] ^= 0x80;
  SecureFrameCodec sender;
  SecureFrameCodec receiver;
  SecureFrameCodec outsider;
  if (!sender.begin(key) || !receiver.begin(key) || !outsider.begin(wrongKey)) {
    return false;
  }

  SessionId session;
  for (size_t i = 0; i < SESSION_ID_SIZE; ++i) {
    session.bytes[i] = static_cast<uint8_t>(i + 1);
  }
  Packet source;
  source.envelope.type = PacketType::DirectMessage;
  source.envelope.origin = 7;
  source.envelope.originSession = session;
  source.envelope.destination = 9;
  source.envelope.floodId = 0x11223344;
  source.message.messageId = 0x55667788;
  source.message.timestamp = 1234;
  memcpy(source.message.text, "CCM round trip", 15);

  constexpr uint32_t crewId = 0xA1B2C3D4;
  uint8_t frame[MAX_FRAME_SIZE];
  size_t frameLength = 0;
  if (!sender.seal(source, crewId, 7, session, 42, frame, sizeof(frame),
                   frameLength)) {
    return false;
  }
  FrameHeader header;
  Packet opened;
  if (receiver.open(frame, frameLength, crewId, header, opened) !=
          FrameOpenResult::Ok ||
      opened.message.messageId != source.message.messageId ||
      strcmp(opened.message.text, source.message.text) != 0) {
    return false;
  }
  if (receiver.open(frame, frameLength, crewId ^ 1U, header, opened) !=
      FrameOpenResult::WrongCrew) {
    return false;
  }
  if (outsider.open(frame, frameLength, crewId, header, opened) !=
      FrameOpenResult::AuthenticationFailed) {
    return false;
  }

  uint8_t tampered[MAX_FRAME_SIZE];
  memcpy(tampered, frame, frameLength);
  tampered[FRAME_HEADER_SIZE] ^= 0x01;
  if (receiver.open(tampered, frameLength, crewId, header, opened) !=
      FrameOpenResult::AuthenticationFailed) {
    return false;
  }
  memcpy(tampered, frame, frameLength);
  tampered[6] ^= 0x01;  // Authenticated transmitter-address header field.
  if (receiver.open(tampered, frameLength, crewId, header, opened) !=
      FrameOpenResult::AuthenticationFailed) {
    return false;
  }
  return receiver.open(frame, frameLength - 1, crewId, header, opened) ==
         FrameOpenResult::Malformed;
}

}  // namespace mesh
