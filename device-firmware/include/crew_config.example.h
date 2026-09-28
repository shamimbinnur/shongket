#pragma once

#include <stdint.h>

namespace crew_config {

// Copy this file to crew_config.h. Use the same values on every crew device.
// Generate a new key before field use; never publish the real header.
constexpr uint32_t CREW_ID = 0x00000000UL;
constexpr uint8_t CREW_KEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

}  // namespace crew_config
