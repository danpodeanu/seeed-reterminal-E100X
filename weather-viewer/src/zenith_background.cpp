#include "zenith_background.h"

#include <pgmspace.h>

namespace zenith_background {
namespace {

// Return the color the panel should use for gray level `idx` out of
// `kLevels`. Splitting this out keeps the pixel loop palette-agnostic:
// the blitter unpacks kData indices in the range 0..kLevels-1 and calls
// paletteColor() to translate to the model's native palette.
inline uint32_t paletteColor(uint8_t idx) {
#if RETERMINAL_MODEL == 1001
  // 4-gray panel: identity mapping across TFT_GRAY_0..TFT_GRAY_3 (dark
  // to light).
  static const uint32_t kMap[4] = {
      TFT_GRAY_0, TFT_GRAY_1, TFT_GRAY_2, TFT_GRAY_3,
  };
  return kMap[idx & 0x3];
#elif RETERMINAL_MODEL == 1003
  // 16-gray panel: spread the 4 dithered levels across the palette so
  // the ink-wash still reads with visible contrast on this display.
  static const uint32_t kMap[4] = {
      TFT_GRAY_0, TFT_GRAY_5, TFT_GRAY_10, TFT_GRAY_15,
  };
  return kMap[idx & 0x3];
#else
  // Spectra-6 panels (E1002, E1004). We store the picture as pure black
  // vs. white because the six-color palette has no usable intermediate
  // grays - anything else would render as a solid color instead of a
  // gradient. The 1bpp payload maps 0 -> black ink, 1 -> paper white.
  return idx ? TFT_WHITE : TFT_BLACK;
#endif
}

}  // namespace

void draw(TFT_eSPI& epaper) {
  const uint16_t w = kWidth;
  const uint16_t h = kHeight;
  const uint8_t bpp = kBitsPerPixel;
  const uint8_t scale = kScale;
  const uint16_t pixelsPerByte = 8 / bpp;
  const uint8_t mask = (1 << bpp) - 1;
  const size_t rowBytes = (size_t)w * bpp / 8;
  const uint8_t* row = kData;
  for (uint16_t y = 0; y < h; ++y) {
    for (uint16_t x = 0; x < w; x += pixelsPerByte) {
      const uint8_t b = pgm_read_byte(row + (x * bpp / 8));
      // Unpack MSB-first: the leftmost pixel occupies the top bits.
      for (uint8_t p = 0; p < pixelsPerByte; ++p) {
        const uint8_t shift = (pixelsPerByte - 1 - p) * bpp;
        const uint8_t idx = (b >> shift) & mask;
        const uint32_t color = paletteColor(idx);
        const uint16_t px = (x + p) * scale;
        const uint16_t py = y * scale;
        if (scale == 1) {
          epaper.drawPixel(px, py, color);
        } else {
          // Nearest-neighbor upscale: paint a scale x scale block per
          // stored pixel. fillRect is a single primitive on TFT_eSPI so
          // this avoids scale*scale drawPixel calls per source pixel.
          epaper.fillRect(px, py, scale, scale, color);
        }
      }
    }
    row += rowBytes;
  }
}

}  // namespace zenith_background
