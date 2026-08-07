#pragma once

#include <Arduino.h>
#include <SD.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "app_logger.h"
#include "sd_card.h"
#include "screenshot_rotation.h"

// Screenshot BMP writer shared by the xkcd and weather viewers (photo does
// not export screenshots). The palette matches the panel color mode
// selected at compile time via RETERMINAL_MODEL. The main entry point
// saveScreenshotBmp() is a template so it can invoke readPixelValue() on
// whatever epaper type the calling app happens to use, without pulling
// TFT_eSPI's headers into this common lib.

namespace screenshot {

inline bool writeLittleEndian16(File& file, uint16_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
  };
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

inline bool writeLittleEndian32(File& file, uint32_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 24),
  };
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

inline void paletteColor(uint8_t index, uint8_t& red, uint8_t& green,
                         uint8_t& blue) {
#if RETERMINAL_MODEL == 1001
  const uint8_t gray = index <= 3 ? static_cast<uint8_t>(index * 85) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1003
  const uint8_t gray = index <= 15 ? static_cast<uint8_t>(index * 17) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1005
  // A 1-bit TFT_eSprite stores black as 0 and white as 1.
  const uint8_t gray = index == 0 ? 0 : 255;
  red = green = blue = gray;
#else
  red = green = blue = 255;
  switch (index) {
    case 0x0: red = 255; green = 255; blue = 255; break;
    case 0x2: red = 29;  green = 185; blue = 84;  break;
    case 0x6: red = 229; green = 57;  blue = 53;  break;
    case 0xB: red = 255; green = 216; blue = 0;   break;
    case 0xD: red = 0;   green = 76;  blue = 255; break;
    case 0xF: red = 0;   green = 0;   blue = 0;   break;
  }
#endif
}

template <typename EPaper>
inline bool saveScreenshotBmp(EPaper& epaper, uint32_t width, uint32_t height,
                              const char* screenshotPath = "/screenshot.bmp",
                              const char* temporaryPath = "/screenshot.bmp.part") {
  constexpr uint32_t fileHeaderSize = 14;
  constexpr uint32_t dibHeaderSize = 40;
  constexpr uint32_t paletteSize = 256 * 4;
  constexpr uint32_t pixelOffset =
      fileHeaderSize + dibHeaderSize + paletteSize;

  const uint32_t rowSize = (width + 3U) & ~3U;
  const uint32_t pixelBytes = rowSize * height;
  const uint32_t fileSize = pixelOffset + pixelBytes;

  uint8_t* row = static_cast<uint8_t*>(malloc(rowSize));
  if (!row) {
    LOG.println("[screenshot] could not allocate BMP row buffer");
    return false;
  }

  sd_card::removeFile(temporaryPath);
  File file = sd_card::openForWrite(temporaryPath);
  if (!file) {
    LOG.println("[screenshot] could not create temporary BMP");
    free(row);
    return false;
  }

  bool ok = file.write(static_cast<uint8_t>('B')) == 1 &&
            file.write(static_cast<uint8_t>('M')) == 1 &&
            writeLittleEndian32(file, fileSize) &&
            writeLittleEndian32(file, 0) &&
            writeLittleEndian32(file, pixelOffset) &&
            writeLittleEndian32(file, dibHeaderSize) &&
            writeLittleEndian32(file, width) &&
            writeLittleEndian32(file, height) &&
            writeLittleEndian16(file, 1) &&
            writeLittleEndian16(file, 8) &&
            writeLittleEndian32(file, 0) &&
            writeLittleEndian32(file, pixelBytes) &&
            writeLittleEndian32(file, 2835) &&
            writeLittleEndian32(file, 2835) &&
            writeLittleEndian32(file, 256) &&
            writeLittleEndian32(file, 16);

  for (uint16_t index = 0; ok && index < 256; ++index) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    paletteColor(static_cast<uint8_t>(index), red, green, blue);
    const uint8_t entry[] = {blue, green, red, 0};
    ok = file.write(entry, sizeof(entry)) == sizeof(entry);
  }

  memset(row, 0, rowSize);
  // BMPs store rows bottom-up when height is positive. readPixelValue()
  // returns the raw monochrome, Gray4, Gray16, or E6 palette index from the
  // composed panel sprite.
#if RETERMINAL_MODEL == 1005
  // Pinned Seeed_GFX maps rotation-1 reads with native height instead of
  // native width, which runs beyond its 1bpp buffer for portrait rows
  // 480..799. Read the unchanged sprite through rotation 0 and apply the
  // correct logical-to-native transform here.
  const uint8_t screenshotRotation = epaper.getRotation();
  epaper.setRotation(0);
#endif
  for (int32_t y = static_cast<int32_t>(height) - 1; ok && y >= 0; --y) {
    for (uint32_t x = 0; x < width; ++x) {
#if RETERMINAL_MODEL == 1005
      const PixelCoordinate native = nativePixelCoordinate(
          screenshotRotation, static_cast<int32_t>(width),
          static_cast<int32_t>(height), static_cast<int32_t>(x), y);
      row[x] =
          static_cast<uint8_t>(epaper.readPixelValue(native.x, native.y));
#else
      row[x] = static_cast<uint8_t>(epaper.readPixelValue(x, y));
#endif
    }
    ok = file.write(row, rowSize) == rowSize;
    if ((y & 31) == 0) delay(1);
  }
#if RETERMINAL_MODEL == 1005
  epaper.setRotation(screenshotRotation);
#endif

  file.flush();
  file.close();
  free(row);

  if (!ok) {
    LOG.println("[screenshot] BMP write failed");
    sd_card::removeFile(temporaryPath);
    return false;
  }

  sd_card::removeFile(screenshotPath);
  if (!sd_card::renameFile(temporaryPath, screenshotPath)) {
    LOG.println("[screenshot] could not install /screenshot.bmp");
    sd_card::removeFile(temporaryPath);
    return false;
  }

  LOG.printf("[screenshot] saved %s (%lu bytes)\n", screenshotPath,
             static_cast<unsigned long>(fileSize));
  return true;
}

}  // namespace screenshot
