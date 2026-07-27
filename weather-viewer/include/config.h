#pragma once

// User-tweakable configuration for the weather viewer. Everything in this
// header is intended to be edited when setting up a new device or when
// changing personal preferences (location, refresh cadence, quiet hours,
// weather provider, language, alert opt-ins).
//
// Hardware, timing budgets, cache paths, and other implementation details
// live in system_config.h, which is included from the bottom of this file
// so it can derive from the user values above.

#include <Arduino.h>

namespace config {

// --- Location ---------------------------------------------------------------
// Edit these values for the forecast location. QWeather rounds to two
// decimals; Open-Meteo uses the full precision.
constexpr char LOCATION_NAME[] = "Suzhou";
constexpr double LATITUDE = 31.31361;
constexpr double LONGITUDE = 120.69167;

// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8.
constexpr char TIMEZONE[] = "CST-8";

// --- Refresh cadence --------------------------------------------------------
// How long the device sleeps between automatic refreshes. Shorter = fresher
// data but noticeably more battery drain and more panel wear.
constexpr uint64_t SLEEP_SECONDS = 15ULL * 60ULL;

// --- Forecast shape ---------------------------------------------------------
// How many days of high/low forecasts to render below the current
// conditions. The layout math assumes 3; changing this needs matching
// renderer tweaks.
constexpr uint8_t FORECAST_DAYS = 3;
// How far ahead to scan hourly precipitation for the "rain expected" hint.
constexpr uint8_t RAIN_FORECAST_HOURS = 48;
// Minimum modelled liquid rain (mm) in a slot for it to count as
// "expected"; smaller values are treated as drizzle noise.
constexpr float RAIN_START_THRESHOLD_MM = 0.1f;
// Minimum precipitation probability (%) for a slot to count as "expected".
constexpr uint8_t RAIN_PROBABILITY_THRESHOLD = 30;

// --- Quiet hours ------------------------------------------------------------
// Suppress automatic and right-button refreshes overnight. A green-button
// wake still refreshes immediately, then sleeps until the configured end.
constexpr bool QUIET_HOURS_ENABLED = true;
constexpr uint8_t QUIET_START_HOUR = 1;
constexpr uint8_t QUIET_START_MINUTE = 0;
constexpr uint8_t QUIET_END_HOUR = 7;
constexpr uint8_t QUIET_END_MINUTE = 0;
static_assert(QUIET_START_HOUR < 24 && QUIET_END_HOUR < 24,
              "Quiet-hour values must be between 0 and 23");
static_assert(QUIET_START_MINUTE < 60 && QUIET_END_MINUTE < 60,
              "Quiet-minute values must be between 0 and 59");

// --- NTP servers ------------------------------------------------------------
// Primary and secondary NTP servers. The DHCP-advertised server is tried
// first regardless; these are the fall-backs when DHCP does not offer one
// or the DHCP server fails.
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";

// --- Weather provider -------------------------------------------------------
// Weather data provider. Open-Meteo needs no API key and works out of the
// box worldwide. QWeather (https://dev.qweather.com/) requires
// QWEATHER_PROJECT_ID, QWEATHER_CREDENTIAL_ID, QWEATHER_PRIVATE_KEY_HEX,
// and QWEATHER_API_HOST in secrets.h; see the examples in
// secrets.h.example.
enum class WeatherProvider {
  OpenMeteo,
  QWeather,
};
constexpr WeatherProvider WEATHER_PROVIDER = WeatherProvider::QWeather;

// Language for QWeather's textual fields (condition names, warnings).
// Common values: "en", "zh" (Simplified Chinese, default upstream),
// "zh-hant", "de", "es", "fr", "ja", "ko", "ru". Ignored when the
// active provider is Open-Meteo. See https://dev.qweather.com/en/docs/
// resource/language/ for the full list.
constexpr char QWEATHER_LANG[] = "en";

// Whether to fetch /v7/warning/now on every refresh. On QWeather's free
// tier the endpoint requires the "Weather Warning" data resource to be
// subscribed to the project in the QWeather console -- unsubscribed keys
// return HTTP 403. Toggle to false if this account cannot access the
// resource so we skip the guaranteed-to-fail round-trip (~1s of wake
// time per refresh). Has no effect when the active provider is Open-Meteo.
constexpr bool QWEATHER_ALERTS_ENABLED = true;

// Whether to fetch severe-weather alerts from the US National Weather
// Service (api.weather.gov/alerts/active) on every refresh. Free, no key,
// but the coverage is US only -- flip to true if the device sits in a
// US state or territory. Non-US points return HTTP 400 so leaving it on
// would just waste ~1s per refresh. Has no effect when the active
// provider is QWeather. Open-Meteo itself does not expose a
// government-alerts endpoint.
constexpr bool NWS_ALERTS_ENABLED = false;

// --- Presentation -----------------------------------------------------------
// Display units. Weather providers always report in metric (Celsius, km/h);
// these knobs choose how numbers are formatted for the panel. Each unit is
// selected independently so metric-with-Fahrenheit or similar mixes are
// possible.
enum class TemperatureUnit {
  Celsius,
  Fahrenheit,
};
constexpr TemperatureUnit TEMPERATURE_UNIT = TemperatureUnit::Celsius;

enum class WindSpeedUnit {
  KilometresPerHour,
  MilesPerHour,
};
constexpr WindSpeedUnit WIND_SPEED_UNIT = WindSpeedUnit::KilometresPerHour;

// Clutter-free mode hides the per-day Rain % / UV summary line on each
// forecast card. The condition name and min/max are still shown; only the
// small grey secondary line is suppressed. Turn off to bring the numbers
// back.
constexpr bool CLUTTER_FREE_MODE = true;

// --- Debug knobs ------------------------------------------------------------
// Show the diagnostic last-refresh badge in the top-right corner.
// Handy while iterating on refresh cadence and provider behaviour;
// turn off for a cleaner day-to-day display.
constexpr bool DEBUG_SHOW_STATUS_BADGES = true;

// Debug knobs for QWeather 401 triage. Both default to false. Enable one
// or both temporarily to diagnose auth failures:
//   DEBUG_FORCE_NTP:  ignore the "24h refresh" gate and re-sync every wake
//                     so JWT iat/exp always use fresh NTP time.
//   DEBUG_LOG_JWT:    print the JWT header, payload, and signature that
//                     the firmware sends. Compare against the tester's
//                     `tools/test_credentials.py --dump-jwt` output.
constexpr bool DEBUG_FORCE_NTP = false;
constexpr bool DEBUG_LOG_JWT = false;

}  // namespace config

// System-level constants (hardware model, timeouts, cache layout,
// derived values). Included after the user constants above so it can
// reference them (e.g. CACHE_MAX_AGE_SECONDS = SLEEP_SECONDS).
#include "system_config.h"
