#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sd_web_upload {

inline constexpr uint32_t kCrc32Initial = 0xFFFFFFFFU;
inline constexpr uint32_t kCrc32NibbleTable[16] = {
    0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
    0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
    0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
    0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
};

inline uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ kCrc32NibbleTable[crc & 0x0FU];
    crc = (crc >> 4) ^ kCrc32NibbleTable[crc & 0x0FU];
  }
  return crc;
}

inline constexpr uint32_t crc32Finish(uint32_t crc) {
  return crc ^ 0xFFFFFFFFU;
}

}  // namespace sd_web_upload
