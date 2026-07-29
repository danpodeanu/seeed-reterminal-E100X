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
// A previous iteration mapped value 1 to TFT_GRAY_2 (Gray4) / TFT_GRAY_10
// (Gray16) so lightest edges blended toward white. That looked
// staircased in practice because those shades are almost
// indistinguishable from the panel background at single-pixel edge
// widths.
//
// The second iteration used a single grey shade for value 1 and dropped
// values 2/3 to solid black. That produced only ~170 grey pixels on the
// 216 px hero (0.36% of area) while ~3300 pixels were solid black --
// visually indistinguishable from the pure 1-bit render. This palette
// promotes value 2 to the same visibly-grey shade as value 1, tripling
// the AA pixel count so edges actually read as anti-aliased.
#if RETERMINAL_MODEL == 1001
// Gray4: only three ink shades. Both AA levels use the dark-grey shade
// (same tone the footer text uses, which is definitely visible on the
// panel); only fully opaque sprite pixels stay solid black.
constexpr uint32_t kInkPalette[3] = {
    TFT_GRAY_1,  // value 1: dark grey (edge AA, light band)
    TFT_GRAY_1,  // value 2: dark grey (edge AA, mid band)
    TFT_GRAY_0,  // value 3: solid black (stroke interior)
};
#elif RETERMINAL_MODEL == 1003
// Gray16: enough shades to give a genuine gradient. Broaden the edge
// bands too so more sprite pixels contribute to the AA look.
constexpr uint32_t kInkPalette[3] = {
    TFT_GRAY_4,  // value 1: mid grey (light edge band)
    TFT_GRAY_1,  // value 2: near-black (mid edge band)
    TFT_GRAY_0,  // value 3: solid black (stroke interior)
};
#else
// 1002 / 1004 build with kBpp == 1 and never index this table; keep a
// stub so the array symbol resolves.
constexpr uint32_t kInkPalette[3] = {0, 0, 0};
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
        if (v) {
          epaper.drawPixel(x0 + dx, y0 + dy, kInkPalette[v - 1]);
        }
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
