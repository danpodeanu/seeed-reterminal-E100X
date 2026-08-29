#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <string.h>

#include "app_logger.h"
#include "weather_app_logic.h"
#include "config.h"
#include "local_time.h"
#include "trusted_client.h"
#include "weather_config_runtime.h"
#include "weather_data.h"
#include "weather_provider.h"

namespace weather_provider {

namespace {

String forecastUrl() {
  String url =
      "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(weather_config::runtime::latitude(), 4);
  url += "&longitude=";
  url += String(weather_config::runtime::longitude(), 4);
  url +=
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
      "is_day,weather_code,wind_speed_10m"
      "&hourly=precipitation_probability,precipitation,rain,showers"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
      "uv_index_max,precipitation_probability_max"
      "&temperature_unit=celsius&wind_speed_unit=kmh"
      // timezone=auto so Open-Meteo groups daily entries by the
      // observation location's calendar day. timeformat=unixtime so
      // every "time" field arrives as an integer UTC epoch, sidestepping
      // the "did we mean the observation's local wall clock or the
      // device's?" ambiguity we hit with QWeather.
      "&timezone=auto&timeformat=unixtime"
      "&forecast_hours=";
  url += String(config::RAIN_FORECAST_HOURS);
  url += "&forecast_days=";
  url += String(config::FORECAST_DAYS);
  return url;
}

// Fill weather.alert* from the US National Weather Service. Best-effort:
// any failure clears the alert fields and returns without disturbing the
// forecast result. NWS uses the same "Extreme > Severe > Moderate > Minor
// > Unknown" severity taxonomy as QWeather, so we reuse the shared rank
// helper in app_logic. api.weather.gov requires a descriptive User-Agent.
void fetchNwsAlerts(WeatherData& weather) {
  weather.alertTitle = "";
  weather.alertSeverity = "";
  weather.alertOtherCount = 0;

  tls_client::DefaultRootClient client;
  if (!local_time::clockIsValid()) client.setInsecure();
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  String url = "https://api.weather.gov/alerts/active?point=";
  url += String(weather_config::runtime::latitude(), 4);
  url += ",";
  url += String(weather_config::runtime::longitude(), 4);
  if (!http.begin(client, url)) {
    LOG.println("[weather] NWS alerts: could not start HTTPS request");
    return;
  }
  LOG.printf("[weather] NWS alerts GET %s\n", url.c_str());
  // NWS requires a descriptive User-Agent; anonymous requests return 403.
  http.addHeader("User-Agent",
                 "reterminal-weather-viewer/1.0 "
                 "(https://github.com/danpodeanu/seeed-reterminal-E100X)");
  http.addHeader("Accept", "application/geo+json");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[weather] NWS alerts: HTTP GET -> %d (continuing)\n", status);
    http.end();
    return;
  }
  // Alert bodies for a single point are tiny (typically < 8 KiB even during
  // active severe weather). Cap generously at 64 KiB to catch runaway
  // responses without exhausting heap.
  constexpr int kMaxAlertBytes = 64 * 1024;
  const int declaredSize = http.getSize();
  if (declaredSize > kMaxAlertBytes) {
    LOG.printf("[weather] NWS alerts: response too large: %d bytes\n",
               declaredSize);
    http.end();
    return;
  }
  const String body = http.getString();
  http.end();
  if (static_cast<int>(body.length()) > kMaxAlertBytes) {
    LOG.printf("[weather] NWS alerts: streamed too large: %u bytes\n",
               static_cast<unsigned>(body.length()));
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    LOG.printf("[weather] NWS alerts JSON: %s\n", error.c_str());
    return;
  }
  JsonArray features = doc["features"];
  int total = 0;
  int bestRank = -1;
  String bestTitle;
  String bestSeverity;
  for (JsonObject feature : features) {
    JsonObject props = feature["properties"];
    if (props.isNull()) continue;
    // Prefer "event" over "headline" for the display line -- "event" is a
    // compact phrase like "Tornado Warning" whereas "headline" is a long
    // sentence like "Tornado Warning issued August 26 at 4:15 PM EDT ...".
    const char* title = props["event"] | (props["headline"] | "");
    if (title[0] == '\0') continue;
    ++total;
    const char* severity = props["severity"] | "";
    const int rank = app_logic::qweatherAlertSeverityRank(severity);
    if (rank > bestRank) {
      bestRank = rank;
      bestTitle = title;
      bestSeverity = severity;
    }
  }
  if (total > 0 && !bestTitle.isEmpty()) {
    weather.alertTitle = bestTitle;
    weather.alertSeverity = bestSeverity;
    weather.alertOtherCount = total - 1;
    LOG.printf("[weather] NWS alert (%s): %s%s\n",
               weather.alertSeverity.isEmpty()
                   ? "unknown"
                   : weather.alertSeverity.c_str(),
               weather.alertTitle.c_str(),
               weather.alertOtherCount > 0
                   ? (String(" (+") +
                      String(weather.alertOtherCount) + " more)")
                         .c_str()
                   : "");
  } else {
    LOG.println("[weather] NWS alerts: no active alerts");
  }
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
  weather.updateTime = static_cast<time_t>(current["time"] | int64_t{0});
  weather.rainTimingAvailable = false;
  weather.rainExpected = false;
  weather.nextRainTime = 0;
  weather.nextRainMm = NAN;
  weather.nextRainProbability = -1;

  // With timeformat=unixtime, daily.time[i] is the UTC epoch of the
  // observation-local midnight for that calendar day. Shifting by
  // utc_offset_seconds and formatting as UTC gives the observation
  // location's calendar date, which is what the panel wants.
  const int32_t utcOffsetSeconds =
      document["utc_offset_seconds"] | int32_t{0};

  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    const int64_t dailyEpoch = dates[i] | int64_t{0};
    if (dailyEpoch == 0) {
      weather.days[i].date = "";
    } else {
      const time_t shifted =
          static_cast<time_t>(dailyEpoch + utcOffsetSeconds);
      struct tm tm = {};
      char buf[11] = "";  // "YYYY-MM-DD"
      if (gmtime_r(&shifted, &tm) != nullptr) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
      }
      weather.days[i].date = String(buf);
    }
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
      const time_t slotTime =
          static_cast<time_t>(hourlyTimes[i] | int64_t{0});
      if (slotTime == 0 ||
          (weather.updateTime != 0 && slotTime <= weather.updateTime)) {
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
  {
    char buf[24];
    local_time::formatLocalIso(weather.updateTime, buf, sizeof(buf));
    LOG.printf("[weather] Open-Meteo current.time=%lld (local %s)\n",
               static_cast<long long>(weather.updateTime), buf);
  }
  if (weather.rainExpected) {
    char buf[24];
    local_time::formatLocalIso(weather.nextRainTime, buf, sizeof(buf));
    LOG.printf("[weather] next rain around %s, %.1fmm, probability=%d%%\n",
               buf, weather.nextRainMm, weather.nextRainProbability);
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
  tls_client::DefaultRootClient client;
  if (!local_time::clockIsValid()) client.setInsecure();
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
  LOG.printf("[weather] Open-Meteo GET %s\n", url.c_str());
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
  // parseOpenMeteo does not touch the alert fields (Open-Meteo itself has
  // no alerts endpoint). Populate them from the US NWS when enabled and
  // the point is inside NWS coverage; failures are non-fatal.
  if (weather_config::runtime::nwsAlertsEnabled()) {
    fetchNwsAlerts(weather);
  }
  return true;
}

}  // namespace weather_provider
