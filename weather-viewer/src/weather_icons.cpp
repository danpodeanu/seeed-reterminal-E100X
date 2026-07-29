#include "weather_icons.h"

#include "generated/weather_icons_data.h"

#include <Arduino.h>
#include <pgmspace.h>

namespace weather_icons {

using generated::IconTriple;
using generated::Sprite;
using generated::kIcons;

// Convenience wrapper so callers don't have to reach into the
// generated:: namespace to spell `Sprite`.
using SpriteRef = Sprite;

namespace {

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
// scaling to `targetSize`. Set bits paint `color`; cleared bits are
// left untouched so the icon composites over the existing background.
void blit(TFT_eSPI& epaper, int cx, int cy, int targetSize,
          const SpriteRef& sprite, uint32_t color) {
  if (targetSize <= 0 || sprite.w == 0 || sprite.h == 0) return;
  const uint16_t sw = sprite.w;
  const uint16_t sh = sprite.h;
  const int stride = (sw + 7) / 8;
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
      const uint8_t byte = pgm_read_byte(row + (sx >> 3));
      if (byte & (0x80 >> (sx & 7))) {
        epaper.drawPixel(x0 + dx, y0 + dy, color);
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
