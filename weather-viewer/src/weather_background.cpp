#include "weather_background.h"

#include <pgmspace.h>

namespace weather_background {
namespace {

#if RETERMINAL_MODEL == 1003
// 16-gray panel: expand the 4 stored levels to the full 16-level
// palette using bilinear interpolation across a 2x2 source
// neighborhood. Each output pixel picks TFT_GRAY_0..TFT_GRAY_15
// directly, so we index a 16-entry palette instead of the 4-entry
// map used on the E1001.
static const uint32_t kPalette16[16] = {
    TFT_GRAY_0,  TFT_GRAY_1,  TFT_GRAY_2,  TFT_GRAY_3,
    TFT_GRAY_4,  TFT_GRAY_5,  TFT_GRAY_6,  TFT_GRAY_7,
    TFT_GRAY_8,  TFT_GRAY_9,  TFT_GRAY_10, TFT_GRAY_11,
    TFT_GRAY_12, TFT_GRAY_13, TFT_GRAY_14, TFT_GRAY_15,
};
#endif

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
  // Only used by the flat fallback path below; the bilinear fast path
  // in draw() indexes kPalette16 directly.
  static const uint32_t kMap[4] = {
      TFT_GRAY_0, TFT_GRAY_5, TFT_GRAY_10, TFT_GRAY_15,
  };
  return kMap[idx & 0x3];
#else
  // One-bit payloads use pure black vs. white. E1002/E1004 need this
  // because Spectra-6 has no usable intermediate grays; E1005 uses the
  // same mapping for its native monochrome palette.
  return idx ? TFT_WHITE : TFT_BLACK;
#endif
}

#if RETERMINAL_MODEL == 1003
// E1003 fast path: read 2bpp source pixels, bilinear-interpolate a
// 2x2 source neighborhood across the scale x scale output block, and
// map each interpolated value to one of TFT_GRAY_0..TFT_GRAY_15.
//
// Rationale: nearest-neighbor 4x upscale over the stored 4-level
// palette produces visible 4x4 flat blocks and coarse tone banding.
// Bilinear over the Floyd-Steinberg dithered source averages the
// noise back into continuous tone, and the 16-level output uses the
// panel's full palette so gradients read as smooth ink-wash rather
// than blocky gray. The whole thing stays integer math with a small
// bilinear-weight LUT.
void drawBilinearGray16(TFT_eSPI& epaper,
                        const uint8_t* row0,
                        uint16_t w,
                        uint16_t h,
                        uint8_t scale) {
  const size_t rowBytes = (size_t)w * 2 / 8;  // 2bpp packed rows
  // Fixed-point bilinear coefficients per (dx, dy) offset within the
  // scale x scale output block. The stored source pixel S(sx, sy) is
  // treated as the top-left corner of the block; each output offset
  // (dx, dy) interpolates toward the neighbors S(sx+1, sy),
  // S(sx, sy+1), S(sx+1, sy+1) with weights (dx/scale, dy/scale).
  //
  // Weights are stored as 8-bit fractions of `scale*scale` so all
  // math stays in integers; the final divide by (scale*scale*3) turns
  // the weighted sum of source levels (each 0..3) into a 0..15 index.
  const int total = scale * scale;
  auto readSrc = [&](int sx, int sy) -> int {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    if (sx >= w) sx = w - 1;
    if (sy >= h) sy = h - 1;
    const uint8_t b = pgm_read_byte(row0 + (size_t)sy * rowBytes + (sx >> 2));
    const uint8_t shift = (3 - (sx & 3)) * 2;
    return (b >> shift) & 0x3;
  };
  for (int sy = 0; sy < h; ++sy) {
    for (int sx = 0; sx < w; ++sx) {
      const int v00 = readSrc(sx,     sy);
      const int v10 = readSrc(sx + 1, sy);
      const int v01 = readSrc(sx,     sy + 1);
      const int v11 = readSrc(sx + 1, sy + 1);
      const int baseX = sx * scale;
      const int baseY = sy * scale;
      for (int dy = 0; dy < scale; ++dy) {
        const int wy1 = dy;
        const int wy0 = scale - dy;
        for (int dx = 0; dx < scale; ++dx) {
          const int wx1 = dx;
          const int wx0 = scale - dx;
          // Weighted sum of source levels (each 0..3) scaled by
          // wx0*wy0 + ... = scale*scale, so the sum lies in
          // [0, 3 * scale * scale]. Divide by (scale*scale) to get a
          // 0..3 average, then remap into a 0..15 palette index by
          // multiplying by 5 (round-to-nearest built in).
          const int num =
              v00 * wx0 * wy0 + v10 * wx1 * wy0 +
              v01 * wx0 * wy1 + v11 * wx1 * wy1;
          // idx = round(num * 5 / (scale*scale))
          int idx = (num * 5 + (total >> 1)) / total;
          if (idx < 0) idx = 0;
          if (idx > 15) idx = 15;
          epaper.drawPixel(baseX + dx, baseY + dy, kPalette16[idx]);
        }
      }
    }
  }
}
#endif  // RETERMINAL_MODEL == 1003

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
#if RETERMINAL_MODEL == 1005
  // The compact Sticky layout intentionally uses a clean white background.
  // It avoids spending flash and refresh time on a 480x800 bitmap while
  // keeping the small monochrome labels and separators crisp.
  (void)theme;
  epaper.fillScreen(TFT_WHITE);
#else
  const uint16_t w = kWidth;
  const uint16_t h = kHeight;
  const uint8_t bpp = kBitsPerPixel;
  const uint8_t scale = kScale;
  // Every slot in kThemeData is populated (single-theme models redirect
  // unsupported themes to the cloudy payload), so we can index by theme
  // without a null check.
  const uint8_t* row = kThemeData[static_cast<uint8_t>(theme)];
#if RETERMINAL_MODEL == 1003
  // On the 16-gray panel, upgrade the stored 4-level payload to a
  // smooth 16-level render via bilinear interpolation. The stored
  // Floyd-Steinberg dither averages back into continuous tone across
  // the 2x2 source neighborhood, and the 4x block is filled with a
  // gradient instead of a flat color, eliminating the coarse
  // pixelation of the nearest-neighbor upscale.
  drawBilinearGray16(epaper, row, w, h, scale);
  return;
#else
  const uint16_t pixelsPerByte = 8 / bpp;
  const uint8_t mask = (1 << bpp) - 1;
  const size_t rowBytes = (size_t)w * bpp / 8;
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
#endif
#endif
}

}  // namespace weather_background
