#pragma once

// Shared weather-condition icon renderer.
//
// The 26 icons are baked into flash at build time by
// common-weather/tools/generate_weather_icons.py (invoked via a
// PlatformIO pre-script) and embedded through the auto-generated
// weather_icons_data.h header. No SD card, no LittleFS, no PSRAM
// allocation -- the sprites live in the app binary and are read
// straight from the ESP32-S3's flash-mapped memory.
//
// Icons are derived from basmilius/weather-icons (Meteocons "line"
// style, MIT-licensed). See LICENSES/METEOCONS.txt for the upstream
// license; small tweaks (stroke +15%, sun/moon offsets in the partly-
// cloudy variants, animation stripping) are baked into the SVGs kept
// under common-weather/icons/svg/.
//
// Usage:
//   #include "weather_icons.h"
//   weather_icons::draw(epaper, cx, cy, size, weatherCode, isDay,
//                       PANEL_BLACK);

#include <TFT_eSPI.h>
#include <stddef.h>
#include <stdint.h>

namespace weather_icons {

// Icon identifiers -- MUST match the BUCKETS list order in
// tools/generate_weather_icons.py, since the generated table is
// indexed by this enum.
enum IconId : uint8_t {
  ICON_CLEAR_DAY = 0,
  ICON_CLEAR_NIGHT,
  ICON_PARTLY_CLOUDY_DAY,
  ICON_PARTLY_CLOUDY_NIGHT,
  ICON_OVERCAST,
  ICON_OVERCAST_DAY,
  ICON_OVERCAST_NIGHT,
  ICON_CLOUDY,
  ICON_FOG_DAY,
  ICON_FOG_NIGHT,
  ICON_HAZE,
  ICON_DRIZZLE,
  ICON_RAIN,
  ICON_PARTLY_CLOUDY_DAY_RAIN,
  ICON_PARTLY_CLOUDY_NIGHT_RAIN,
  ICON_SNOW,
  ICON_PARTLY_CLOUDY_DAY_SNOW,
  ICON_PARTLY_CLOUDY_NIGHT_SNOW,
  ICON_SLEET,
  ICON_THUNDERSTORMS,
  ICON_THUNDERSTORMS_DAY,
  ICON_THUNDERSTORMS_NIGHT,
  ICON_THUNDERSTORMS_DAY_RAIN,
  ICON_THUNDERSTORMS_NIGHT_RAIN,
  ICON_THUNDERSTORMS_DAY_SNOW,
  ICON_THUNDERSTORMS_NIGHT_SNOW,
  ICON_COUNT
};

// Map an Open-Meteo WMO weather code (or a QWeather icon that has
// already been routed through app_logic::qweatherIconToWmoCode) to
// one of the 26 icon buckets. Unknown codes fall back to ICON_CLOUDY
// so the display always shows something plausible rather than blank.
IconId wmoToIcon(int wmoCode, bool isDay);

// Draw a weather icon centered at (cx, cy) with roughly the requested
// pixel size. Picks the sprite with the closest native size from the
// generated triple, then nearest-neighbour scales to `size` while
// blitting. Only "ink" pixels are painted (`color`); the background
// is left untouched so the icon composites over whatever surface
// already exists.
void draw(TFT_eSPI& epaper, int cx, int cy, int size, int wmoCode,
          bool isDay, uint32_t color);

}  // namespace weather_icons
