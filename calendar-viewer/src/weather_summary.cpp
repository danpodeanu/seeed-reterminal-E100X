#include "weather_summary.h"

#include <Preferences.h>
#include <math.h>

#include "calendar_config_runtime.h"
#include "calendar_config_schema.h"
#include "config.h"
#include "local_time.h"
#include "weather_app_logic.h"
#include "weather_provider.h"

namespace weather_summary {
namespace {

constexpr const char* kValid = "w_valid";
constexpr const char* kSaved = "w_saved";
constexpr const char* kTemp = "w_temp";
constexpr const char* kFeels = "w_feels";
constexpr const char* kHumidity = "w_hum";
constexpr const char* kWind = "w_wind";
constexpr const char* kCode = "w_code";
constexpr const char* kHigh = "w_high";
constexpr const char* kLow = "w_low";
constexpr const char* kAlert = "w_alert";

}  // namespace

bool fetch(WeatherData& weather, String& failureReason,
           bool bypassHttpCache) {
  String response;
  if (calendar_config::runtime::weatherProvider() ==
      config::WeatherProvider::QWeather) {
    return weather_provider::fetchQWeather(
        weather, response, failureReason, bypassHttpCache);
  }
  return weather_provider::fetchOpenMeteo(
      weather, response, failureReason, bypassHttpCache);
}

bool loadCached(WeatherData& weather, uint64_t maximumAgeSeconds,
                String& failureReason) {
  failureReason = "";
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, true)) {
    failureReason = "Could not open weather cache";
    return false;
  }
  const bool valid = prefs.getBool(kValid, false);
  const time_t saved = static_cast<time_t>(prefs.getLong64(kSaved, 0));
  const bool fresh = valid && app_logic::cachedDataFresh(
                                  local_time::clockIsValid(), time(nullptr),
                                  saved, maximumAgeSeconds);
  if (fresh) {
    weather.temperatureC = prefs.getFloat(kTemp, NAN);
    weather.apparentC = prefs.getFloat(kFeels, NAN);
    weather.humidityPct = prefs.getFloat(kHumidity, NAN);
    weather.windKmh = prefs.getFloat(kWind, NAN);
    weather.weatherCode = prefs.getInt(kCode, -1);
    weather.days[0].maximumC = prefs.getFloat(kHigh, NAN);
    weather.days[0].minimumC = prefs.getFloat(kLow, NAN);
    weather.alertTitle = prefs.getString(kAlert, "");
  }
  prefs.end();
  weather.valid = fresh && isfinite(weather.temperatureC) &&
                  weather.weatherCode >= 0;
  weather.fromCache = weather.valid;
  weather.updateTime = saved;
  if (!weather.valid) failureReason = "No recent weather summary is stored";
  return weather.valid;
}

bool saveCached(const WeatherData& weather, String& failureReason) {
  failureReason = "";
  if (!weather.valid) {
    failureReason = "Weather summary is invalid";
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, false)) {
    failureReason = "Could not open weather cache";
    return false;
  }
  prefs.putBool(kValid, false);
  const time_t saved = weather.updateTime > 0 ? weather.updateTime : time(nullptr);
  bool stored =
      prefs.putLong64(kSaved, static_cast<int64_t>(saved)) > 0 &&
      prefs.putFloat(kTemp, weather.temperatureC) > 0 &&
      prefs.putFloat(kFeels, weather.apparentC) > 0 &&
      prefs.putFloat(kHumidity, weather.humidityPct) > 0 &&
      prefs.putFloat(kWind, weather.windKmh) > 0 &&
      prefs.putInt(kCode, weather.weatherCode) > 0 &&
      prefs.putFloat(kHigh, weather.days[0].maximumC) > 0 &&
      prefs.putFloat(kLow, weather.days[0].minimumC) > 0 &&
      prefs.putString(kAlert, weather.alertTitle) >= weather.alertTitle.length();
  if (stored) stored = prefs.putBool(kValid, true) > 0;
  prefs.end();
  if (!stored) failureReason = "Could not save the complete weather summary";
  return stored;
}

}  // namespace weather_summary
