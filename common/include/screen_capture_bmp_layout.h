#pragma once

#include <stdint.h>

namespace screen_capture_bmp {

struct Layout {
  uint32_t rowSize;
  uint32_t pixelBytes;
  uint32_t pixelOffset;
  uint32_t fileSize;
};

constexpr Layout layout(uint32_t width, uint32_t height) {
  constexpr uint32_t kFileHeaderSize = 14;
  constexpr uint32_t kDibHeaderSize = 40;
  constexpr uint32_t kPaletteSize = 256 * 4;
  const uint32_t rowSize = (width + 3U) & ~3U;
  const uint32_t pixelBytes = rowSize * height;
  const uint32_t pixelOffset =
      kFileHeaderSize + kDibHeaderSize + kPaletteSize;
  return {rowSize, pixelBytes, pixelOffset, pixelOffset + pixelBytes};
}

constexpr uint8_t e1005PaletteGray(uint8_t index, uint8_t colorDepth) {
  if (colorDepth > 1) {
    return index <= 3 ? static_cast<uint8_t>(index * 85) : 255;
  }
  return index == 0 ? 0 : 255;
}

}  // namespace screen_capture_bmp
