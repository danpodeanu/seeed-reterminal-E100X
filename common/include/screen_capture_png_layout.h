#pragma once

#include <stddef.h>
#include <stdint.h>

namespace screen_capture_png {

inline constexpr uint32_t kPaletteEntries = 256;
inline constexpr uint32_t kPaletteBytes = kPaletteEntries * 3;

struct Layout {
  uint32_t rowBytes;
  uint32_t deflateBlockBytes;
  uint32_t idatDataBytes;
  uint32_t fileSize;
};

constexpr Layout layout(uint32_t width, uint32_t height) {
  constexpr uint32_t kPngSignatureBytes = 8;
  constexpr uint32_t kChunkOverheadBytes = 12;
  constexpr uint32_t kIhdrDataBytes = 13;
  constexpr uint32_t kZlibHeaderBytes = 2;
  constexpr uint32_t kAdler32Bytes = 4;
  const uint32_t rowBytes = width + 1U;
  const uint32_t deflateBlockBytes = 5U + rowBytes;
  const uint32_t idatDataBytes =
      kZlibHeaderBytes + deflateBlockBytes * height + kAdler32Bytes;
  const uint32_t fileSize =
      kPngSignatureBytes +
      (kChunkOverheadBytes + kIhdrDataBytes) +
      (kChunkOverheadBytes + kPaletteBytes) +
      (kChunkOverheadBytes + idatDataBytes) + kChunkOverheadBytes;
  return {rowBytes, deflateBlockBytes, idatDataBytes, fileSize};
}

constexpr uint8_t e1005PaletteGray(uint8_t index, uint8_t colorDepth) {
  if (colorDepth > 1) {
    return index <= 3 ? static_cast<uint8_t>(index * 85) : 255;
  }
  return index == 0 ? 0 : 255;
}

inline uint32_t updateCrc32(uint32_t crc, const uint8_t* bytes,
                            size_t length) {
  for (size_t index = 0; index < length; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return crc;
}

inline uint32_t updateAdler32(uint32_t adler, const uint8_t* bytes,
                              size_t length) {
  constexpr uint32_t kModAdler = 65521;
  uint32_t sum1 = adler & 0xFFFFU;
  uint32_t sum2 = adler >> 16;
  for (size_t index = 0; index < length; ++index) {
    sum1 += bytes[index];
    if (sum1 >= kModAdler) sum1 -= kModAdler;
    sum2 += sum1;
    if (sum2 >= kModAdler) sum2 -= kModAdler;
  }
  return (sum2 << 16) | sum1;
}

}  // namespace screen_capture_png
