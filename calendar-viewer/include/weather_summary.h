#pragma once

#include <Arduino.h>

#include "weather_data.h"

namespace weather_summary {

uint64_t cacheIdentity();
bool fetch(WeatherData& weather, String& failureReason, bool bypassHttpCache);
bool loadCached(WeatherData& weather, String& failureReason);
bool saveCached(const WeatherData& weather, String& failureReason);

}  // namespace weather_summary
