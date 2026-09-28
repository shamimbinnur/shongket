#pragma once

#include <stdint.h>

namespace device_config {

// Copy this file to device_config.h and change both values before each device
// is built. Addresses must be unique within a crew; 0 and 0xFFFF are reserved.
constexpr uint16_t DEVICE_ADDRESS = 0;
constexpr char DEVICE_NAME[] = "CHANGE_ME";

}  // namespace device_config
