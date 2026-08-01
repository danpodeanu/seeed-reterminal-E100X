#include "weather_background.h"

#include <pgmspace.h>

namespace weather_background {
namespace {

// Return the color the panel should use for gray level `idx` out of
// `kLevels`. Splitting this out keeps the pixel loop palette-agnostic:
// the blitter unpacks indices in the range 0..kLevels-1 and calls
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

Theme themeForWmoCode(int wmoCode) {
  // The ranges mirror weather_quotes::wmoToBucketIndex so the background
  // never disagrees with the hero icon or the footer proverb about what
  // "kind" of weather it is. Freezing precipitation is treated as snowy
  // for the picture even though other subsystems may bucket it as sleet.
  switch (wmoCode) {
    case 0:  return Theme::SUNNY;                    // clear sky
    case 1:                                          // mainly clear
    case 2:                                          // partly cloudy
    case 3:  return Theme::CLOUDY;                   // overcast
    case 45:                                         // fog
    case 48: return Theme::CLOUDY;                   // depositing rime fog
    default: break;
  }
  if (wmoCode >= 51 && wmoCode <= 55) return Theme::RAINY;    // drizzle
  if (wmoCode == 56 || wmoCode == 57) return Theme::SNOWY;    // freezing drizzle
  if (wmoCode >= 61 && wmoCode <= 65) return Theme::RAINY;    // rain
  if (wmoCode == 66 || wmoCode == 67) return Theme::SNOWY;    // freezing rain
  if (wmoCode >= 71 && wmoCode <= 77) return Theme::SNOWY;    // snow
  if (wmoCode >= 80 && wmoCode <= 82) return Theme::RAINY;    // rain showers
  if (wmoCode == 85 || wmoCode == 86) return Theme::SNOWY;    // snow showers
  if (wmoCode >= 95 && wmoCode <= 99) return Theme::RAINY;    // thunderstorm
  return Theme::CLOUDY;                                       // unknown / -1
}

void draw(TFT_eSPI& epaper, Theme theme) {
  const uint16_t w = kWidth;
  const uint16_t h = kHeight;
  const uint8_t bpp = kBitsPerPixel;
  const uint8_t scale = kScale;
  const uint16_t pixelsPerByte = 8 / bpp;
  const uint8_t mask = (1 << bpp) - 1;
  const size_t rowBytes = (size_t)w * bpp / 8;
  // Every slot in kThemeData is populated (single-theme models redirect
  // unsupported themes to the cloudy payload), so we can index by theme
  // without a null check.
  const uint8_t* row = kThemeData[static_cast<uint8_t>(theme)];
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

}  // namespace weather_background
