#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <string.h>

#include "app_logger.h"
#include "app_logic.h"
#include "config.h"
#include "weather_data.h"
#include "weather_provider.h"

namespace weather_provider {

namespace {

String forecastUrl() {
  String url =
      "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(config::LATITUDE, 4);
  url += "&longitude=";
  url += String(config::LONGITUDE, 4);
  url +=
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
      "is_day,weather_code,wind_speed_10m"
      "&hourly=precipitation_probability,precipitation,rain,showers"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
      "uv_index_max,precipitation_probability_max"
      "&temperature_unit=celsius&wind_speed_unit=kmh&timezone=auto"
      "&forecast_hours=";
  url += String(config::RAIN_FORECAST_HOURS);
  url += "&forecast_days=";
  url += String(config::FORECAST_DAYS);
  return url;
}

}  // namespace

bool parseOpenMeteo(const String& body, WeatherData& weather) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    LOG.printf("[weather] JSON: %s\n", error.c_str());
    return false;
  }
  if (document["error"] | false) {
    LOG.printf("[weather] API error: %s\n",
               String(document["reason"] | "unknown").c_str());
    return false;
  }
  JsonObject current = document["current"];
  JsonObject daily = document["daily"];
  JsonArray dates = daily["time"];
  JsonArray codes = daily["weather_code"];
  JsonArray maxima = daily["temperature_2m_max"];
  JsonArray minima = daily["temperature_2m_min"];
  JsonArray uv = daily["uv_index_max"];
  JsonArray rain = daily["precipitation_probability_max"];
  if (current.isNull() || dates.size() < config::FORECAST_DAYS ||
      codes.size() < config::FORECAST_DAYS ||
      maxima.size() < config::FORECAST_DAYS ||
      minima.size() < config::FORECAST_DAYS ||
      uv.size() < config::FORECAST_DAYS ||
      rain.size() < config::FORECAST_DAYS) {
    LOG.println("[weather] response is missing required fields");
    return false;
  }

  weather.temperatureC = current["temperature_2m"] | NAN;
  weather.apparentC = current["apparent_temperature"] | NAN;
  weather.humidityPct = current["relative_humidity_2m"] | NAN;
  weather.windKmh = current["wind_speed_10m"] | NAN;
  weather.weatherCode = current["weather_code"] | -1;
  weather.isDay = (current["is_day"] | 1) != 0;
  weather.updateTime = String(current["time"] | "");
  weather.rainTimingAvailable = false;
  weather.rainExpected = false;
  weather.nextRainTime = "";
  weather.nextRainMm = NAN;
  weather.nextRainProbability = -1;

  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    weather.days[i].date = String(dates[i] | "");
    weather.days[i].weatherCode = codes[i] | -1;
    weather.days[i].maximumC = maxima[i] | NAN;
    weather.days[i].minimumC = minima[i] | NAN;
    weather.days[i].uvMaximum = uv[i] | NAN;
    weather.days[i].precipitationProbability = rain[i] | -1;
  }

  JsonObject hourly = document["hourly"];
  JsonArray hourlyTimes = hourly["time"];
  JsonArray hourlyProbability = hourly["precipitation_probability"];
  JsonArray hourlyPrecipitation = hourly["precipitation"];
  JsonArray hourlyRain = hourly["rain"];
  JsonArray hourlyShowers = hourly["showers"];
  const size_t hourlyCount =
      min(hourlyTimes.size(), hourlyPrecipitation.size());
  if (!hourly.isNull() && hourlyCount > 0) {
    weather.rainTimingAvailable = true;
    for (size_t i = 0; i < hourlyCount; ++i) {
      const String slotTime = String(hourlyTimes[i] | "");
      if (slotTime.isEmpty() ||
          (!weather.updateTime.isEmpty() &&
           strcmp(slotTime.c_str(), weather.updateTime.c_str()) <= 0)) {
        continue;
      }

      const float precipitation = hourlyPrecipitation[i] | 0.0f;
      const float rainAmount =
          i < hourlyRain.size() ? hourlyRain[i] | 0.0f : 0.0f;
      const float showerAmount =
          i < hourlyShowers.size() ? hourlyShowers[i] | 0.0f : 0.0f;
      const float liquidRain =
          max(precipitation, rainAmount + showerAmount);
      const int probability =
          i < hourlyProbability.size() && !hourlyProbability[i].isNull()
              ? hourlyProbability[i].as<int>()
              : -1;
      if (app_logic::rainSlotQualifies(
              liquidRain, probability, config::RAIN_START_THRESHOLD_MM,
              config::RAIN_PROBABILITY_THRESHOLD)) {
        weather.rainExpected = true;
        weather.nextRainTime = slotTime;
        weather.nextRainMm = liquidRain;
        weather.nextRainProbability = probability;
        break;
      }
    }
  }

  weather.valid = isfinite(weather.temperatureC) &&
                  isfinite(weather.apparentC) &&
                  isfinite(weather.humidityPct) &&
                  weather.weatherCode >= 0;
  if (!weather.valid) {
    LOG.println("[weather] current values are invalid");
    return false;
  }
  LOG.printf("[weather] %.1fC, feels %.1fC, %.0f%% RH, code=%d\n",
             weather.temperatureC, weather.apparentC, weather.humidityPct,
             weather.weatherCode);
  if (weather.rainExpected) {
    LOG.printf("[weather] next rain around %s, %.1fmm, probability=%d%%\n",
               weather.nextRainTime.c_str(), weather.nextRainMm,
               weather.nextRainProbability);
  } else if (weather.rainTimingAvailable) {
    LOG.printf("[weather] no qualifying rain in the next %u hours\n",
               config::RAIN_FORECAST_HOURS);
  } else {
    LOG.println("[weather] hourly rain timing unavailable");
  }
  return true;
}

bool fetchOpenMeteo(WeatherData& weather, String& responseBody,
                    String& failureReason, bool bypassHttpCache) {
  responseBody = "";
  failureReason = "";
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const String url = forecastUrl();
  if (!http.begin(client, url)) {
    LOG.println("[weather] could not start HTTPS request");
    failureReason = "Could not start weather request";
    return false;
  }
  if (bypassHttpCache) {
    http.addHeader("Cache-Control", "no-cache, no-store");
    http.addHeader("Pragma", "no-cache");
    LOG.println("[weather] button wake: forcing live API refresh");
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[weather] HTTP GET -> %d\n", status);
    http.end();
    failureReason = "Weather service returned an error";
    return false;
  }
  // Reject an implausibly large body before letting http.getString() load
  // it all into heap. Well-formed Open-Meteo responses for this query are
  // well under 100 KiB; a runaway redirect loop or a malformed proxy could
  // otherwise easily exhaust the ~320 KiB ESP32 heap.
  constexpr int kMaxResponseBytes = 256 * 1024;
  const int declaredSize = http.getSize();
  if (declaredSize > kMaxResponseBytes) {
    LOG.printf("[weather] response too large: %d bytes\n", declaredSize);
    http.end();
    failureReason = "Weather response is too large";
    return false;
  }
  responseBody = http.getString();
  if (static_cast<int>(responseBody.length()) > kMaxResponseBytes) {
    LOG.printf("[weather] streamed response too large: %u bytes\n",
               static_cast<unsigned>(responseBody.length()));
    responseBody = "";
    http.end();
    failureReason = "Weather response is too large";
    return false;
  }
  http.end();
  LOG.printf("[weather] received %u bytes from Open-Meteo\n",
             static_cast<unsigned>(responseBody.length()));
  weather.fromCache = false;
  if (!parseOpenMeteo(responseBody, weather)) {
    failureReason = "Weather service returned invalid data";
    return false;
  }
  return true;
}

}  // namespace weather_provider
