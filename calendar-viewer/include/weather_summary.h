#pragma once

#include <Arduino.h>

#include "weather_data.h"

namespace weather_summary {

bool fetch(WeatherData& weather, String& failureReason, bool bypassHttpCache);
bool loadCached(WeatherData& weather, uint64_t maximumAgeSeconds,
                String& failureReason);
bool saveCached(const WeatherData& weather, String& failureReason);

}  // namespace weather_summary
