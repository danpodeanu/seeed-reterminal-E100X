#pragma once

#include <Arduino.h>

#include "config.h"
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

// Single source of truth for the user-facing provider label. Every UI
// string that names the weather source (status line, logs, cache
// diagnostics) must route through this helper -- never hardcode
// "Open-Meteo" or "QWeather" at a draw site.
inline const char* name() {
  return config::WEATHER_PROVIDER == config::WeatherProvider::QWeather
             ? "QWeather"
             : "Open-Meteo";
}

}  // namespace weather_provider
