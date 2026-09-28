#pragma once

#include "crew_config.h"
#include "device_config.h"

#include <stddef.h>
#include <stdint.h>

namespace app_config {

constexpr size_t MAX_DEVICE_NAME = 12;
constexpr uint16_t BROADCAST_ADDRESS = 0xFFFF;

static_assert(crew_config::CREW_ID != 0, "Set a nonzero CREW_ID");
static_assert(sizeof(crew_config::CREW_KEY) == 16,
              "CREW_KEY must contain exactly 16 bytes");
static_assert(device_config::DEVICE_ADDRESS > 0 &&
                  device_config::DEVICE_ADDRESS < BROADCAST_ADDRESS,
              "DEVICE_ADDRESS must be between 1 and 65534");
static_assert(sizeof(device_config::DEVICE_NAME) > 1 &&
                  sizeof(device_config::DEVICE_NAME) <= MAX_DEVICE_NAME + 1,
              "DEVICE_NAME must contain 1 to 12 characters");

inline bool runtimeConfigValid() {
  uint8_t keyOr = 0;
  for (size_t i = 0; i < sizeof(crew_config::CREW_KEY); ++i) {
    keyOr |= crew_config::CREW_KEY[i];
  }
  if (keyOr == 0) {
    return false;
  }
  const char placeholder[] = "CHANGE_ME";
  size_t compared = 0;
  while (device_config::DEVICE_NAME[compared] != '\0' &&
         placeholder[compared] != '\0' &&
         device_config::DEVICE_NAME[compared] == placeholder[compared]) {
    ++compared;
  }
  if (device_config::DEVICE_NAME[compared] == '\0' &&
      placeholder[compared] == '\0') {
    return false;
  }
  for (size_t i = 0; device_config::DEVICE_NAME[i] != '\0'; ++i) {
    const unsigned char c =
        static_cast<unsigned char>(device_config::DEVICE_NAME[i]);
    if (c < 0x20 || c > 0x7e || c == '|') {
      return false;
    }
  }
  return true;
}

}  // namespace app_config
