#include "weather_icons.h"

#include "generated/weather_icons_data.h"

#include <Arduino.h>
#include <pgmspace.h>

namespace weather_icons {

using generated::IconTriple;
using generated::Sprite;
using generated::kBpp;
using generated::kIcons;

// Convenience wrapper so callers don't have to reach into the
// generated:: namespace to spell `Sprite`.
using SpriteRef = Sprite;

namespace {

// Ink palette for 2 bpp sprites, indexed by sprite value 1..3 (lightest
// -> darkest). Panels that only have two native colors compile with
// kBpp == 1 and this table is unused -- the `color` argument to draw()
// is honored directly instead.
//
// AA strategy for 2 bpp sources depends on how many usable grey shades
// the panel has:
//
// * E1001 (UC8179 Gray4) has only three visible ink levels and, in
//   practice, single-pixel dots at a mid-grey shade get lost among the
//   solid-black stroke on this panel -- the eye reads the icons as
//   pure 1-bit ink even when the framebuffer clearly has grey pixels.
//   Two earlier palette-based iterations both failed this test. We now
//   ordered-dither the AA bands to pure black speckles on a white
//   background instead: the local density (25% for value 1, 50% for
//   value 2) reads as grey at any distance because it is actual
//   black/white contrast rather than a fragile grey shade.
//
// * E1003 (Gray16) has enough shades that a proper grey gradient is
//   perceptibly smoother than dither. Palette is preferred here.
//
// * E1002 / E1004 (colour panels) build with kBpp == 1 and don't hit
//   this table at all.

#if RETERMINAL_MODEL == 1003
constexpr uint32_t kInkPalette[3] = {
    TFT_GRAY_4,  // value 1: mid grey (light edge band)
    TFT_GRAY_1,  // value 2: near-black (mid edge band)
    TFT_GRAY_0,  // value 3: solid black (stroke interior)
};
#else
// Kept so the symbol resolves on E1001 / E1002 / E1004; unused when
// dither is on and value 3 is always solid.
constexpr uint32_t kInkPalette[3] = {0, 0, 0};
#endif

#if RETERMINAL_MODEL == 1001
// 4x4 Bayer matrix, values 0..15. A pixel with source value v is
// painted solid black when bayer[y%4][x%4] < threshold(v). Threshold 4
// yields ~25% density, threshold 8 yields ~50% density.
constexpr uint8_t kBayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};
#endif

// Pick the sprite from `triple` whose native size is closest to
// `requested`. Ties go to the larger sprite (upscaling from a slightly
// too-small source drops strokes; downscaling from slightly too-large
// source only thins them, which is easier on the eye).
const SpriteRef& pickSprite(const IconTriple& triple, int requested) {
  const int ds = abs(requested - triple.small.w);
  const int dm = abs(requested - triple.mid.w);
  const int dl = abs(requested - triple.large.w);
  if (dl <= dm && dl <= ds) return triple.large;
  if (dm <= ds)              return triple.mid;
  return triple.small;
}

// Draw the packed sprite centered at (cx, cy) with nearest-neighbour
// scaling to `targetSize`. For 1 bpp sources set bits paint `color`;
// for 2 bpp sources non-zero values look up kInkPalette so panel-native
// grey shades anti-alias the strokes. Transparent pixels are left
// untouched so the icon composites over the existing background.
void blit(TFT_eSPI& epaper, int cx, int cy, int targetSize,
          const SpriteRef& sprite, uint32_t color) {
  if (targetSize <= 0 || sprite.w == 0 || sprite.h == 0) return;
  const uint16_t sw = sprite.w;
  const uint16_t sh = sprite.h;
  // 1 bpp packs 8 px/byte; 2 bpp packs 4 px/byte. Both are row-padded
  // to whole bytes so the y*stride math is identical to a raw framebuffer.
  const int stride = (kBpp == 2) ? ((sw * 2 + 7) / 8) : ((sw + 7) / 8);
  const int x0 = cx - targetSize / 2;
  const int y0 = cy - targetSize / 2;
  for (int dy = 0; dy < targetSize; ++dy) {
    // Integer nearest-neighbour: sy = dy * sh / targetSize.
    const int sy = static_cast<int>(
        (static_cast<uint32_t>(dy) * sh) / static_cast<uint32_t>(targetSize));
    const uint8_t* row = sprite.bits + sy * stride;
    for (int dx = 0; dx < targetSize; ++dx) {
      const int sx = static_cast<int>(
          (static_cast<uint32_t>(dx) * sw) / static_cast<uint32_t>(targetSize));
      if (kBpp == 2) {
        // Four pixels per byte, top pair first.
        const uint8_t byte = pgm_read_byte(row + (sx >> 2));
        const uint8_t shift = 6 - 2 * (sx & 3);
        const uint8_t v = (byte >> shift) & 0x3;
        if (!v) continue;  // transparent
#if RETERMINAL_MODEL == 1001
        // Ordered dither: paint solid black when the Bayer cell falls
        // below the threshold for this source value. Value 3 always
        // paints. Otherwise leave the pixel untouched (white background
        // shows through), which the eye reads as a grey band.
        if (v == 3) {
          epaper.drawPixel(x0 + dx, y0 + dy, color);
        } else {
          const uint8_t threshold = (v == 2) ? 8 : 4;
          if (kBayer4[dy & 3][dx & 3] < threshold) {
            epaper.drawPixel(x0 + dx, y0 + dy, color);
          }
        }
#else
        epaper.drawPixel(x0 + dx, y0 + dy, kInkPalette[v - 1]);
#endif
      } else {
        const uint8_t byte = pgm_read_byte(row + (sx >> 3));
        if (byte & (0x80 >> (sx & 7))) {
          epaper.drawPixel(x0 + dx, y0 + dy, color);
        }
      }
    }
  }
}

}  // namespace

IconId wmoToIcon(int wmoCode, bool isDay) {
  switch (wmoCode) {
    case 0:  return isDay ? ICON_CLEAR_DAY : ICON_CLEAR_NIGHT;
    case 1:
    case 2:  return isDay ? ICON_PARTLY_CLOUDY_DAY
                          : ICON_PARTLY_CLOUDY_NIGHT;
    case 3:  return isDay ? ICON_OVERCAST_DAY : ICON_OVERCAST_NIGHT;
    case 45: return isDay ? ICON_FOG_DAY : ICON_FOG_NIGHT;
    case 48: return ICON_HAZE;
    default: break;
  }

  if (wmoCode >= 51 && wmoCode <= 55) return ICON_DRIZZLE;
  if (wmoCode == 56 || wmoCode == 57) return ICON_SLEET;  // freezing drizzle
  if (wmoCode >= 61 && wmoCode <= 65) return ICON_RAIN;
  if (wmoCode == 66 || wmoCode == 67) return ICON_SLEET;  // freezing rain
  if (wmoCode >= 71 && wmoCode <= 77) return ICON_SNOW;
  if (wmoCode >= 80 && wmoCode <= 82) {
    return isDay ? ICON_PARTLY_CLOUDY_DAY_RAIN
                 : ICON_PARTLY_CLOUDY_NIGHT_RAIN;
  }
  if (wmoCode == 85 || wmoCode == 86) {
    return isDay ? ICON_PARTLY_CLOUDY_DAY_SNOW
                 : ICON_PARTLY_CLOUDY_NIGHT_SNOW;
  }
  if (wmoCode == 95) {
    return isDay ? ICON_THUNDERSTORMS_DAY : ICON_THUNDERSTORMS_NIGHT;
  }
  if (wmoCode >= 96 && wmoCode <= 99) {
    return isDay ? ICON_THUNDERSTORMS_DAY_RAIN
                 : ICON_THUNDERSTORMS_NIGHT_RAIN;
  }
  return ICON_CLOUDY;  // safe fallback for unknown / malformed codes
}

void draw(TFT_eSPI& epaper, int cx, int cy, int size, int wmoCode,
          bool isDay, uint32_t color) {
  const IconId id = wmoToIcon(wmoCode, isDay);
  const SpriteRef& sprite = pickSprite(kIcons[id], size);
  blit(epaper, cx, cy, size, sprite, color);
}

}  // namespace weather_icons
