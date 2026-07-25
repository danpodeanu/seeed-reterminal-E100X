#pragma once

#include <Arduino.h>

#include "weather_data.h"

// Each provider owns its own HTTP call pattern (Open-Meteo is a single GET;
// QWeather stitches three GETs together after signing an EdDSA JWT) but
// exposes the same fetch/parse pair. main.cpp routes calls through these
// via config::WEATHER_PROVIDER.
namespace weather_provider {

bool fetchOpenMeteo(WeatherData& weather, String& responseBody,
                    String& failureReason, bool bypassHttpCache);
bool parseOpenMeteo(const String& body, WeatherData& weather);

bool fetchQWeather(WeatherData& weather, String& responseBody,
                   String& failureReason, bool bypassHttpCache);
bool parseQWeather(const String& body, WeatherData& weather);

}  // namespace weather_provider
