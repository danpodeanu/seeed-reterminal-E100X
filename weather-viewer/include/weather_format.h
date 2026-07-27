#pragma once

// Small formatting helpers that guard against sentinels ("data not
// provided") on the WeatherData path. Providers use NAN for missing floats
// and -1 for missing ints, which - if concatenated straight into a
// display string - would render as "nan", "-2147483648", or "-1%". These
// helpers return a "--" placeholder instead and, for the temperature and
// wind cases, apply the configured display unit.

#include <Arduino.h>
#include <math.h>

#include "units.h"

namespace weather_format {

// The placeholder used whenever a value is missing. Kept short so it fits
// wherever the real value would.
inline const char* kMissing = "--";

// Formatted temperature with the current display unit's abbreviation
// appended, e.g. "31F" or "23.4C". Returns just the placeholder (no unit
// suffix) when the source value is NaN so a missing datum reads the same
// regardless of the configured unit.
inline String temperature(float celsius, unsigned int decimals = 0) {
  if (!isfinite(celsius)) return String(kMissing);
  return String(units::temperatureDisplay(celsius), decimals) +
         units::temperatureLabel();
}

// Formatted wind speed *with* the unit label appended, e.g. "12 mph".
// Returns just the placeholder when the source value is NaN.
inline String windSpeed(float kmh, unsigned int decimals = 0) {
  if (!isfinite(kmh)) return String(kMissing);
  return String(units::windSpeedDisplay(kmh), decimals) + " " +
         units::windSpeedLabel();
}

// Generic float formatter for values without a unit (UV index etc.).
inline String number(float value, unsigned int decimals) {
  if (!isfinite(value)) return String(kMissing);
  return String(value, decimals);
}

// Generic integer formatter. Returns "--" when the value matches the
// sentinel (-1 by default, which is what WeatherData uses for
// probability-style fields).
inline String integer(int value, int sentinel = -1) {
  if (value == sentinel) return String(kMissing);
  return String(value);
}

}  // namespace weather_format
