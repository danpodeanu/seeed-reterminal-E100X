#pragma once

#include <Arduino.h>

#include "calendar_options.h"

namespace config {

enum class WeatherProvider {
  OpenMeteo,
  QWeather,
};

enum class TemperatureUnit {
  Celsius,
  Fahrenheit,
};

enum class WindSpeedUnit {
  KilometresPerHour,
  MilesPerHour,
};

constexpr CalendarProvider CALENDAR_PROVIDER = CalendarProvider::Ical;
constexpr WeekStart WEEK_START = WeekStart::Monday;
constexpr TimeFormat TIME_FORMAT = TimeFormat::TwelveHour;
constexpr char ICAL_URL[] = "";
constexpr char GOOGLE_CALENDAR_IDS[] = "";
constexpr char GOOGLE_DELEGATED_USER[] = "";

constexpr uint64_t SLEEP_SECONDS = 15ULL * 60ULL;
constexpr char TIMEZONE[] = "GMT0BST,M3.5.0/1,M10.5.0/2";
constexpr bool QUIET_HOURS_ENABLED = true;
constexpr uint8_t QUIET_START_HOUR = 1;
constexpr uint8_t QUIET_START_MINUTE = 0;
constexpr uint8_t QUIET_END_HOUR = 7;
constexpr uint8_t QUIET_END_MINUTE = 0;
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";

constexpr char LOCATION_NAME[] = "London";
constexpr double LATITUDE = 51.5074;
constexpr double LONGITUDE = -0.1278;
constexpr WeatherProvider WEATHER_PROVIDER = WeatherProvider::OpenMeteo;
constexpr TemperatureUnit TEMPERATURE_UNIT = TemperatureUnit::Celsius;
constexpr WindSpeedUnit WIND_SPEED_UNIT = WindSpeedUnit::KilometresPerHour;
constexpr char QWEATHER_LANG[] = "en";
constexpr bool QWEATHER_ALERTS_ENABLED = false;
constexpr bool NWS_ALERTS_ENABLED = false;

constexpr bool LOW_BATTERY_WARN_ENABLED = true;
constexpr bool DEBUG_SHOW_STATUS_BADGES = false;
constexpr bool DEBUG_FORCE_NTP = false;
constexpr bool DEBUG_LOG_JWT = false;

// Shared weather-provider constants. Calendar Viewer renders only the current
// summary, but the provider contract also fills a compact three-day forecast.
constexpr uint8_t FORECAST_DAYS = 3;
constexpr uint8_t RAIN_FORECAST_HOURS = 24;
constexpr float RAIN_START_THRESHOLD_MM = 0.1f;
constexpr uint8_t RAIN_PROBABILITY_THRESHOLD = 30;

}  // namespace config

#include "system_config.h"
