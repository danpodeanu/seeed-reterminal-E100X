#pragma once

// Canonical on-disk / in-memory contract for weather data. Both providers
// (Open-Meteo and QWeather) parse their raw responses into WeatherData, and
// the SD forecast cache stores the serialised WeatherData rather than the
// raw provider body. That way the cache is provider-agnostic: switching
// config::WEATHER_PROVIDER never leaves behind an unreadable cache, and
// the load path does not need a per-provider parser (which used to log
// "QWeather envelope missing" on every boot after a provider swap).
//
// If the shape below ever changes in an incompatible way, bump the schema
// version -- the reader treats any mismatch as "no valid cache".

#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>

#include "app_logger.h"
#include "config.h"
#include "weather_data.h"

namespace canonical_weather {

constexpr char SCHEMA_VERSION[] = "reterminal-weather-v2";

namespace detail {

template <typename Container>
inline void writeFloat(Container&& doc, const char* key, float value) {
  if (isfinite(value)) doc[key] = value;
}

template <typename Container>
inline void writeInt(Container&& doc, const char* key, int value,
                     int sentinel = -1) {
  if (value != sentinel) doc[key] = value;
}

inline float readFloat(JsonVariantConst v) {
  return v.is<float>() ? v.as<float>() : NAN;
}

inline int readInt(JsonVariantConst v, int fallback = -1) {
  return v.is<int>() ? v.as<int>() : fallback;
}

}  // namespace detail

// Serialise a WeatherData into the canonical JSON form. Returns true on
// success. Fields with sentinel values (NaN, -1) are omitted so the file
// stays compact and unambiguous.
inline bool serialize(const WeatherData& weather, String& out) {
  JsonDocument doc;
  doc["_schema"] = SCHEMA_VERSION;
  // Timestamps are stored as UTC epoch seconds. See weather_data.h.
  doc["updateTime"] = static_cast<int64_t>(weather.updateTime);
  detail::writeFloat(doc, "temperatureC", weather.temperatureC);
  detail::writeFloat(doc, "apparentC", weather.apparentC);
  detail::writeFloat(doc, "humidityPct", weather.humidityPct);
  detail::writeFloat(doc, "windKmh", weather.windKmh);
  detail::writeInt(doc, "weatherCode", weather.weatherCode);
  doc["isDay"] = weather.isDay;
  doc["rainTimingAvailable"] = weather.rainTimingAvailable;
  doc["rainExpected"] = weather.rainExpected;
  if (weather.nextRainTime != 0) {
    doc["nextRainTime"] = static_cast<int64_t>(weather.nextRainTime);
  }
  detail::writeFloat(doc, "nextRainMm", weather.nextRainMm);
  detail::writeInt(doc, "nextRainProbability", weather.nextRainProbability);
  if (weather.alertTitle.length() > 0) {
    doc["alertTitle"] = weather.alertTitle;
    if (weather.alertSeverity.length() > 0) {
      doc["alertSeverity"] = weather.alertSeverity;
    }
    if (weather.alertOtherCount > 0) {
      doc["alertOtherCount"] = weather.alertOtherCount;
    }
  }
  JsonArray days = doc["days"].to<JsonArray>();
  for (size_t i = 0; i < config::FORECAST_DAYS; ++i) {
    const DailyForecast& d = weather.days[i];
    JsonObject entry = days.add<JsonObject>();
    entry["date"] = d.date;
    detail::writeInt(entry, "weatherCode", d.weatherCode);
    detail::writeFloat(entry, "minimumC", d.minimumC);
    detail::writeFloat(entry, "maximumC", d.maximumC);
    detail::writeFloat(entry, "uvMaximum", d.uvMaximum);
    detail::writeInt(entry, "precipitationProbability",
                     d.precipitationProbability);
  }
  const size_t written = serializeJson(doc, out);
  return written > 0;
}

// Parse a canonical JSON blob back into WeatherData. Returns false if the
// _schema key is absent or does not match SCHEMA_VERSION, or if the JSON
// itself is malformed. Missing individual fields fall back to the sentinel
// values used by WeatherData's default construction (NaN / -1 / empty).
inline bool parse(const String& body, WeatherData& weather) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    LOG.printf("[cache] JSON: %s\n", error.c_str());
    return false;
  }
  const char* schema = doc["_schema"] | "";
  if (strcmp(schema, SCHEMA_VERSION) != 0) {
    LOG.printf("[cache] schema %s does not match %s; ignoring\n",
               schema[0] == '\0' ? "(none)" : schema, SCHEMA_VERSION);
    return false;
  }
  weather = WeatherData{};
  weather.valid = true;
  weather.updateTime = static_cast<time_t>(doc["updateTime"] | int64_t{0});
  weather.temperatureC = detail::readFloat(doc["temperatureC"]);
  weather.apparentC = detail::readFloat(doc["apparentC"]);
  weather.humidityPct = detail::readFloat(doc["humidityPct"]);
  weather.windKmh = detail::readFloat(doc["windKmh"]);
  weather.weatherCode = detail::readInt(doc["weatherCode"]);
  weather.isDay = doc["isDay"] | true;
  weather.rainTimingAvailable = doc["rainTimingAvailable"] | false;
  weather.rainExpected = doc["rainExpected"] | false;
  weather.nextRainTime =
      static_cast<time_t>(doc["nextRainTime"] | int64_t{0});
  weather.nextRainMm = detail::readFloat(doc["nextRainMm"]);
  weather.nextRainProbability =
      detail::readInt(doc["nextRainProbability"]);
  weather.alertTitle = doc["alertTitle"] | "";
  weather.alertSeverity = doc["alertSeverity"] | "";
  weather.alertOtherCount = detail::readInt(doc["alertOtherCount"], 0);
  if (weather.alertOtherCount < 0) weather.alertOtherCount = 0;
  JsonArrayConst days = doc["days"].as<JsonArrayConst>();
  size_t i = 0;
  for (JsonObjectConst d : days) {
    if (i >= config::FORECAST_DAYS) break;
    DailyForecast& entry = weather.days[i++];
    entry.date = d["date"] | "";
    entry.weatherCode = detail::readInt(d["weatherCode"]);
    entry.minimumC = detail::readFloat(d["minimumC"]);
    entry.maximumC = detail::readFloat(d["maximumC"]);
    entry.uvMaximum = detail::readFloat(d["uvMaximum"]);
    entry.precipitationProbability =
        detail::readInt(d["precipitationProbability"]);
  }
  return true;
}

}  // namespace canonical_weather
