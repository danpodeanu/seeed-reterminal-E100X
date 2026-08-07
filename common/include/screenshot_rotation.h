#pragma once

#include <stdint.h>

namespace screenshot {

struct PixelCoordinate {
  int32_t x;
  int32_t y;
};

constexpr PixelCoordinate nativePixelCoordinate(
    uint8_t rotation, int32_t logicalWidth, int32_t logicalHeight,
    int32_t x, int32_t y) {
  switch (rotation & 3U) {
    case 1:
      return {logicalHeight - y - 1, x};
    case 2:
      return {logicalWidth - x - 1, logicalHeight - y - 1};
    case 3:
      return {y, logicalWidth - x - 1};
    default:
      return {x, y};
  }
}

}  // namespace screenshot
