#pragma once

#include <Arduino.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "screen_capture_bmp_layout.h"
#include "screenshot_rotation.h"

namespace screen_capture_bmp {

inline void paletteColor(uint8_t index, uint8_t colorDepth, uint8_t& red,
                         uint8_t& green, uint8_t& blue) {
  (void)colorDepth;
#if RETERMINAL_MODEL == 1001
  const uint8_t gray = index <= 3 ? static_cast<uint8_t>(index * 85) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1003
  const uint8_t gray = index <= 15 ? static_cast<uint8_t>(index * 17) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1005
  const uint8_t gray = e1005PaletteGray(index, colorDepth);
  red = green = blue = gray;
#else
  red = green = blue = 255;
  switch (index) {
    case 0x0:
      red = 255;
      green = 255;
      blue = 255;
      break;
    case 0x2:
      red = 29;
      green = 185;
      blue = 84;
      break;
    case 0x6:
      red = 229;
      green = 57;
      blue = 53;
      break;
    case 0xB:
      red = 255;
      green = 216;
      blue = 0;
      break;
    case 0xD:
      red = 0;
      green = 76;
      blue = 255;
      break;
    case 0xF:
      red = 0;
      green = 0;
      blue = 0;
      break;
  }
#endif
}

template <typename Sink>
inline bool writeBytes(Sink& sink, const uint8_t* bytes, size_t length) {
  constexpr size_t kChunkSize = 512;
  size_t offset = 0;
  while (offset < length) {
    const size_t chunk =
        min(kChunkSize, static_cast<size_t>(length - offset));
    const size_t written = sink.write(bytes + offset, chunk);
    if (written == 0) return false;
    offset += written;
  }
  return true;
}

template <typename Sink>
inline bool writeLittleEndian16(Sink& sink, uint16_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
  };
  return writeBytes(sink, bytes, sizeof(bytes));
}

template <typename Sink>
inline bool writeLittleEndian32(Sink& sink, uint32_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 24),
  };
  return writeBytes(sink, bytes, sizeof(bytes));
}

template <typename EPaper, typename Sink>
inline bool write(EPaper& epaper, uint32_t width, uint32_t height, Sink& sink) {
  constexpr uint32_t kDibHeaderSize = 40;
  const Layout bmp = layout(width, height);
  const uint8_t colorDepth = static_cast<uint8_t>(epaper.getColorDepth());
  uint8_t* row = static_cast<uint8_t*>(malloc(bmp.rowSize));
  if (row == nullptr) return false;

  const uint8_t signature[] = {'B', 'M'};
  bool ok = writeBytes(sink, signature, sizeof(signature)) &&
            writeLittleEndian32(sink, bmp.fileSize) &&
            writeLittleEndian32(sink, 0) &&
            writeLittleEndian32(sink, bmp.pixelOffset) &&
            writeLittleEndian32(sink, kDibHeaderSize) &&
            writeLittleEndian32(sink, width) &&
            writeLittleEndian32(sink, height) &&
            writeLittleEndian16(sink, 1) &&
            writeLittleEndian16(sink, 8) &&
            writeLittleEndian32(sink, 0) &&
            writeLittleEndian32(sink, bmp.pixelBytes) &&
            writeLittleEndian32(sink, 2835) &&
            writeLittleEndian32(sink, 2835) &&
            writeLittleEndian32(sink, 256) &&
            writeLittleEndian32(sink, 16);

  for (uint16_t index = 0; ok && index < 256; ++index) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    paletteColor(static_cast<uint8_t>(index), colorDepth, red, green, blue);
    const uint8_t entry[] = {blue, green, red, 0};
    ok = writeBytes(sink, entry, sizeof(entry));
  }

  memset(row, 0, bmp.rowSize);
#if RETERMINAL_MODEL == 1005
  const uint8_t captureRotation = epaper.getRotation();
  epaper.setRotation(0);
#endif
  for (int32_t y = static_cast<int32_t>(height) - 1; ok && y >= 0; --y) {
    for (uint32_t x = 0; x < width; ++x) {
#if RETERMINAL_MODEL == 1005
      const screenshot::PixelCoordinate native =
          screenshot::nativePixelCoordinate(
              captureRotation, static_cast<int32_t>(width),
              static_cast<int32_t>(height), static_cast<int32_t>(x), y);
      row[x] =
          static_cast<uint8_t>(epaper.readPixelValue(native.x, native.y));
#else
      row[x] = static_cast<uint8_t>(epaper.readPixelValue(x, y));
#endif
    }
    ok = writeBytes(sink, row, bmp.rowSize);
    if ((y & 31) == 0) delay(1);
  }
#if RETERMINAL_MODEL == 1005
  epaper.setRotation(captureRotation);
#endif

  free(row);
  return ok;
}

}  // namespace screen_capture_bmp
