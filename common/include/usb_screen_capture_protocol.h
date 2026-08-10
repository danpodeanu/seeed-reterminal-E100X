#pragma once

#include <stddef.h>
#include <stdint.h>

namespace usb_screen_capture {

inline constexpr char kRequest[] = "RETERMINAL_SCREEN_CAPTURE_V1";
inline constexpr size_t kMaximumCommandLength = 63;

inline uint32_t updateCrc32(uint32_t crc, const uint8_t* bytes, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return crc;
}

}  // namespace usb_screen_capture
