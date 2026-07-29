#pragma once

// Pure (header-only, no Arduino/TFT dependencies) mapping from a WMO
// weather code to the quote bucket index. Split out from
// weather_quotes.cpp so the native Unity harness can exercise the
// mapping without pulling in the generated PROGMEM table or the
// display driver.
//
// The indices below MUST stay in sync with:
//   1. ``BUCKETS`` in ``tools/generate_weather_quotes.py`` (defines
//      the emitted array order in ``weather_quotes_data.h``).
//   2. The ``Bucket`` enum in ``weather_quotes_data.h`` itself.
// A regression test in the native harness pins the mapping so an
// accidental reorder is caught before it silently swaps buckets.

#include <cstdint>

namespace weather_quotes {
namespace pure {

enum BucketIndex : uint8_t {
  IDX_CLEAR         = 0,
  IDX_PARTLY_CLOUDY = 1,
  IDX_OVERCAST      = 2,
  IDX_FOG           = 3,
  IDX_HAZE          = 4,
  IDX_DRIZZLE       = 5,
  IDX_RAIN          = 6,
  IDX_SHOWERS       = 7,
  IDX_THUNDERSTORM  = 8,
  IDX_SNOW          = 9,
  IDX_SLEET         = 10,
  IDX_WIND          = 11,
  IDX_COLD          = 12,
  IDX_HEAT          = 13,
  IDX_UNIVERSAL     = 14,
  IDX_COUNT         = 15,
};

// Return the bucket index for ``wmoCode``. Ranges intentionally
// mirror ``weather_icons::wmoToIcon`` so the hero icon and footer
// proverb never disagree on the current condition. Unknown codes
// fall back to the condition-agnostic UNIVERSAL bucket.
inline BucketIndex wmoToBucketIndex(int wmoCode) {
  switch (wmoCode) {
    case 0:  return IDX_CLEAR;
    case 1:
    case 2:  return IDX_PARTLY_CLOUDY;
    case 3:  return IDX_OVERCAST;
    case 45: return IDX_FOG;
    case 48: return IDX_HAZE;
    default: break;
  }
  if (wmoCode >= 51 && wmoCode <= 55) return IDX_DRIZZLE;
  if (wmoCode == 56 || wmoCode == 57) return IDX_SLEET;   // freezing drizzle
  if (wmoCode >= 61 && wmoCode <= 65) return IDX_RAIN;
  if (wmoCode == 66 || wmoCode == 67) return IDX_SLEET;   // freezing rain
  if (wmoCode >= 71 && wmoCode <= 77) return IDX_SNOW;
  if (wmoCode >= 80 && wmoCode <= 82) return IDX_SHOWERS;
  if (wmoCode == 85 || wmoCode == 86) return IDX_SNOW;    // snow showers
  if (wmoCode >= 95 && wmoCode <= 99) return IDX_THUNDERSTORM;
  return IDX_UNIVERSAL;
}

}  // namespace pure
}  // namespace weather_quotes
