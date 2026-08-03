#include "weather_config_runtime.h"

#include "config_storage.h"
#include "secrets.h"

namespace weather_config {
namespace runtime {
namespace {

// secrets.h.example placeholders for QWeather. Treated as "not
// configured" so the QWeather section of the portal presents blank
// inputs and the runtime falls back to nothing (JWT signing will then
// fail loudly with a clear error rather than silently sending a JWT
// signed with an all-zero key).
bool isQweatherPlaceholder(const char* s) {
  if (!s) return false;
  return strcmp(s, "YOUR_QWEATHER_PROJECT_ID") == 0 ||
         strcmp(s, "YOUR_QWEATHER_CREDENTIAL_ID") == 0 ||
         strcmp(s,
                "0000000000000000000000000000000000000000000000000000000000000000") ==
             0;
}

struct Cache {
  String   locationName    = ::config::LOCATION_NAME;
  double   latitude        = ::config::LATITUDE;
  double   longitude       = ::config::LONGITUDE;

  uint64_t sleepSeconds    = ::config::SLEEP_SECONDS;
  String   timezone        = ::config::TIMEZONE;

  bool     quietHoursEnabled = ::config::QUIET_HOURS_ENABLED;
  uint8_t  quietStartHour    = ::config::QUIET_START_HOUR;
  uint8_t  quietStartMinute  = ::config::QUIET_START_MINUTE;
  uint8_t  quietEndHour      = ::config::QUIET_END_HOUR;
  uint8_t  quietEndMinute    = ::config::QUIET_END_MINUTE;

  String   ntpPrimary   = ::config::NTP_SERVER_PRIMARY;
  String   ntpSecondary = ::config::NTP_SERVER_SECONDARY;

  ::config::WeatherProvider provider   = ::config::WEATHER_PROVIDER;
  ::config::TemperatureUnit tempUnit   = ::config::TEMPERATURE_UNIT;
  ::config::WindSpeedUnit   windUnit   = ::config::WIND_SPEED_UNIT;
  bool     clutterFreeMode   = ::config::CLUTTER_FREE_MODE;
  bool     weatherBackgroundEnabled = ::config::WEATHER_BACKGROUND_ENABLED;
  bool     nwsAlertsEnabled  = ::config::NWS_ALERTS_ENABLED;

  String   qwHost         = QWEATHER_API_HOST;
  String   qwProjectId    = QWEATHER_PROJECT_ID;
  String   qwCredentialId = QWEATHER_CREDENTIAL_ID;
  String   qwPrivateKey   = QWEATHER_PRIVATE_KEY_HEX;
  String   qwLang         = ::config::QWEATHER_LANG;
  bool     qwAlertsEnabled = ::config::QWEATHER_ALERTS_ENABLED;

  bool     debugShowStatusBadges = ::config::DEBUG_SHOW_STATUS_BADGES;
  bool     logToSd               = ::config::LOG_TO_SD;
  bool     lowBatteryWarn        = ::config::LOW_BATTERY_WARN_ENABLED;
};

Cache g_cache;
bool  g_loaded = false;

// Enum parsers -- MUST match an exact schema value, otherwise fall back
// to the compile-time default. Previously an empty/unknown string
// silently mapped to the "other" enum value (QWeather / Fahrenheit /
// MilesPerHour), which meant a stray blank in NVS overrode the intended
// firmware default rather than being ignored.
::config::WeatherProvider parseProvider(const String& s) {
  if (s == "OpenMeteo") return ::config::WeatherProvider::OpenMeteo;
  if (s == "QWeather")  return ::config::WeatherProvider::QWeather;
  return ::config::WEATHER_PROVIDER;
}

::config::TemperatureUnit parseTempUnit(const String& s) {
  if (s == "Celsius")    return ::config::TemperatureUnit::Celsius;
  if (s == "Fahrenheit") return ::config::TemperatureUnit::Fahrenheit;
  return ::config::TEMPERATURE_UNIT;
}

::config::WindSpeedUnit parseWindUnit(const String& s) {
  if (s == "KilometresPerHour") return ::config::WindSpeedUnit::KilometresPerHour;
  if (s == "MilesPerHour")      return ::config::WindSpeedUnit::MilesPerHour;
  return ::config::WIND_SPEED_UNIT;
}

// Load a string from NVS via the schema. If the loaded value is empty
// or matches a known secrets.h placeholder sentinel, fall back to the
// compile-time value baked into secrets.h so out-of-the-box flashes
// keep working even before the user visits the portal.
String loadQweatherString(config_portal::storage::PrefsStorage& prefs,
                          const char* key,
                          const char* secretsFallback) {
  const String v = config_portal::storage::getString(prefs, kSchema, key);
  if (v.length() == 0 || isQweatherPlaceholder(v.c_str())) {
    return String(secretsFallback ? secretsFallback : "");
  }
  return v;
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  // Each schema helper (getInt/getString/getBool/getFloat) opens its
  // own NVS session and closes it. If we held the namespace open here
  // ourselves those inner ``begin()`` calls would fail (Arduino's
  // Preferences::begin returns false when the handle is already open),
  // and every field would silently fall back to its schema default.
  PrefsStorage prefs;

  g_cache.locationName =
      config_portal::storage::getString(prefs, kSchema, kKeyLocationName);
  g_cache.latitude =
      static_cast<double>(config_portal::storage::getFloat(prefs, kSchema, kKeyLatitude));
  g_cache.longitude =
      static_cast<double>(config_portal::storage::getFloat(prefs, kSchema, kKeyLongitude));

  g_cache.sleepSeconds = static_cast<uint64_t>(
      config_portal::storage::getInt(prefs, kSchema, kKeySleepSeconds));
  g_cache.timezone =
      config_portal::storage::getString(prefs, kSchema, kKeyTimezone);

  g_cache.quietHoursEnabled =
      config_portal::storage::getBool(prefs, kSchema, kKeyQuietEnabled);
  g_cache.quietStartHour = static_cast<uint8_t>(
      config_portal::storage::getInt(prefs, kSchema, kKeyQuietStartHour));
  g_cache.quietStartMinute = static_cast<uint8_t>(
      config_portal::storage::getInt(prefs, kSchema, kKeyQuietStartMin));
  g_cache.quietEndHour = static_cast<uint8_t>(
      config_portal::storage::getInt(prefs, kSchema, kKeyQuietEndHour));
  g_cache.quietEndMinute = static_cast<uint8_t>(
      config_portal::storage::getInt(prefs, kSchema, kKeyQuietEndMin));

  g_cache.ntpPrimary =
      config_portal::storage::getString(prefs, kSchema, kKeyNtpPrimary);
  g_cache.ntpSecondary =
      config_portal::storage::getString(prefs, kSchema, kKeyNtpSecondary);

  g_cache.provider =
      parseProvider(config_portal::storage::getString(prefs, kSchema, kKeyProvider));
  g_cache.tempUnit =
      parseTempUnit(config_portal::storage::getString(prefs, kSchema, kKeyTempUnit));
  g_cache.windUnit =
      parseWindUnit(config_portal::storage::getString(prefs, kSchema, kKeyWindUnit));
  g_cache.clutterFreeMode =
      config_portal::storage::getBool(prefs, kSchema, kKeyClutterFree);
  g_cache.weatherBackgroundEnabled =
      config_portal::storage::getBool(prefs, kSchema, kKeyWeatherBackground);
  g_cache.nwsAlertsEnabled =
      config_portal::storage::getBool(prefs, kSchema, kKeyNwsAlerts);

  // QWeather credentials -- NVS-first with secrets.h fallback + placeholder scrub.
  // Special case: for the Secret field the storage layer converts empty
  // NVS to the schema default ("") so we treat that as "unconfigured"
  // and fall back to secrets.h.
  g_cache.qwHost         = loadQweatherString(prefs, kKeyQwHost, QWEATHER_API_HOST);
  g_cache.qwProjectId    = loadQweatherString(prefs, kKeyQwProjectId, QWEATHER_PROJECT_ID);
  g_cache.qwCredentialId = loadQweatherString(prefs, kKeyQwCredentialId, QWEATHER_CREDENTIAL_ID);
  g_cache.qwPrivateKey   = loadQweatherString(prefs, kKeyQwPrivateKey, QWEATHER_PRIVATE_KEY_HEX);
  g_cache.qwLang         = config_portal::storage::getString(prefs, kSchema, kKeyQwLang);
  g_cache.qwAlertsEnabled =
      config_portal::storage::getBool(prefs, kSchema, kKeyQwAlerts);

  g_cache.debugShowStatusBadges =
      config_portal::storage::getBool(prefs, kSchema, kKeyDebugBadges);
  g_cache.logToSd =
      config_portal::storage::getBool(prefs, kSchema, kKeyLogToSd);
  g_cache.lowBatteryWarn =
      config_portal::storage::getBool(prefs, kSchema, kKeyLowBatteryWarn);

  g_loaded = true;
}

const char*  locationName()          { return g_cache.locationName.c_str(); }
double       latitude()              { return g_cache.latitude; }
double       longitude()             { return g_cache.longitude; }

uint64_t     sleepSeconds()          { return g_cache.sleepSeconds; }
const char*  timezone()              { return g_cache.timezone.c_str(); }

bool         quietHoursEnabled()     { return g_cache.quietHoursEnabled; }
uint8_t      quietStartHour()        { return g_cache.quietStartHour; }
uint8_t      quietStartMinute()      { return g_cache.quietStartMinute; }
uint8_t      quietEndHour()          { return g_cache.quietEndHour; }
uint8_t      quietEndMinute()        { return g_cache.quietEndMinute; }

const char*  ntpPrimary()            { return g_cache.ntpPrimary.c_str(); }
const char*  ntpSecondary()          { return g_cache.ntpSecondary.c_str(); }

::config::WeatherProvider   weatherProvider() { return g_cache.provider; }
::config::TemperatureUnit   temperatureUnit() { return g_cache.tempUnit; }
::config::WindSpeedUnit     windSpeedUnit()   { return g_cache.windUnit; }
bool                        clutterFreeMode() { return g_cache.clutterFreeMode; }
bool                        weatherBackgroundEnabled() { return g_cache.weatherBackgroundEnabled; }
bool                        nwsAlertsEnabled(){ return g_cache.nwsAlertsEnabled; }

const char*  qweatherHost()          { return g_cache.qwHost.c_str(); }
const char*  qweatherProjectId()     { return g_cache.qwProjectId.c_str(); }
const char*  qweatherCredentialId()  { return g_cache.qwCredentialId.c_str(); }
const char*  qweatherPrivateKeyHex() { return g_cache.qwPrivateKey.c_str(); }
const char*  qweatherLang()          { return g_cache.qwLang.c_str(); }
bool         qweatherAlertsEnabled() { return g_cache.qwAlertsEnabled; }

bool         debugShowStatusBadges() { return g_cache.debugShowStatusBadges; }
bool         logToSd()               { return g_cache.logToSd; }
bool         lowBatteryWarn()        { return g_cache.lowBatteryWarn; }

}  // namespace runtime
}  // namespace weather_config
