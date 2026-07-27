#pragma once

// Display-side unit conversion. Weather providers deliver temperature in
// Celsius and wind speed in km/h; these helpers convert to the user-selected
// display unit and return the abbreviation ("C" / "F", "km/h" / "mph").
//
// The conversion functions are pure and header-only so the small helpers
// can be used from any translation unit (main.cpp today, tests tomorrow)
// without dragging in extra sources.

#include <Arduino.h>

#include "config.h"

namespace units {

inline float temperatureDisplay(float celsius) {
  if (config::TEMPERATURE_UNIT == config::TemperatureUnit::Fahrenheit) {
    return celsius * 9.0f / 5.0f + 32.0f;
  }
  return celsius;
}

inline const char* temperatureLabel() {
  return config::TEMPERATURE_UNIT == config::TemperatureUnit::Fahrenheit
             ? "F"
             : "C";
}

inline float windSpeedDisplay(float kmh) {
  if (config::WIND_SPEED_UNIT == config::WindSpeedUnit::MilesPerHour) {
    // 1 km/h == 0.621371 mph.
    return kmh * 0.621371f;
  }
  return kmh;
}

inline const char* windSpeedLabel() {
  return config::WIND_SPEED_UNIT == config::WindSpeedUnit::MilesPerHour
             ? "mph"
             : "km/h";
}

}  // namespace units
