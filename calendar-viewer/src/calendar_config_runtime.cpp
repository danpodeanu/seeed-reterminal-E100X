#include "calendar_config_runtime.h"

#include <Preferences.h>

#include "calendar_config_schema.h"
#include "config_storage.h"
#include "secrets.h"

namespace calendar_config {
namespace runtime {
namespace {

struct Cache {
  config::CalendarProvider calendarProvider = config::CALENDAR_PROVIDER;
  String icalUrl = config::ICAL_URL;
  String googleCalendarIds = config::GOOGLE_CALENDAR_IDS;
  String googleDelegatedUser = config::GOOGLE_DELEGATED_USER;
  config::CalendarView calendarView = config::CALENDAR_VIEW;
  config::WeekStart weekStart = config::WEEK_START;

  uint64_t sleepSeconds = config::SLEEP_SECONDS;
  String timezone = config::TIMEZONE;
  bool quietHoursEnabled = config::QUIET_HOURS_ENABLED;
  uint8_t quietStartHour = config::QUIET_START_HOUR;
  uint8_t quietStartMinute = config::QUIET_START_MINUTE;
  uint8_t quietEndHour = config::QUIET_END_HOUR;
  uint8_t quietEndMinute = config::QUIET_END_MINUTE;
  String ntpPrimary = config::NTP_SERVER_PRIMARY;
  String ntpSecondary = config::NTP_SERVER_SECONDARY;

  String locationName = config::LOCATION_NAME;
  double latitude = config::LATITUDE;
  double longitude = config::LONGITUDE;
  config::WeatherProvider weatherProvider = config::WEATHER_PROVIDER;
  config::TemperatureUnit tempUnit = config::TEMPERATURE_UNIT;
  config::WindSpeedUnit windUnit = config::WIND_SPEED_UNIT;
  bool nwsAlerts = config::NWS_ALERTS_ENABLED;
  String qwHost = QWEATHER_API_HOST;
  String qwProjectId = QWEATHER_PROJECT_ID;
  String qwCredentialId = QWEATHER_CREDENTIAL_ID;
  String qwPrivateKey = QWEATHER_PRIVATE_KEY_HEX;
  String qwLang = config::QWEATHER_LANG;
  bool qwAlerts = config::QWEATHER_ALERTS_ENABLED;

  bool debugBadges = config::DEBUG_SHOW_STATUS_BADGES;
  bool lowBatteryWarn = config::LOW_BATTERY_WARN_ENABLED;
};

Cache g_cache;

bool isPlaceholder(const char* value) {
  if (value == nullptr) return false;
  return strcmp(value, "YOUR_QWEATHER_PROJECT_ID") == 0 ||
         strcmp(value, "YOUR_QWEATHER_CREDENTIAL_ID") == 0 ||
         strcmp(value,
                "0000000000000000000000000000000000000000000000000000000000000000") ==
             0;
}

String loadSecret(config_portal::storage::PrefsStorage& prefs,
                  const char* key, const char* fallback) {
  const String value =
      config_portal::storage::getString(prefs, kSchema, key);
  if (value.isEmpty() || isPlaceholder(value.c_str())) {
    return isPlaceholder(fallback) ? String() : String(fallback ? fallback : "");
  }
  return value;
}

config::CalendarProvider parseCalendarProvider(const String& value) {
  return value == "Google" ? config::CalendarProvider::Google
                            : config::CalendarProvider::Ical;
}

config::CalendarView parseView(const String& value) {
  if (value == "Week") return config::CalendarView::Week;
  if (value == "Month") return config::CalendarView::Month;
  return config::CalendarView::Today;
}

config::WeekStart parseWeekStart(const String& value) {
  return value == "Sunday" ? config::WeekStart::Sunday
                            : config::WeekStart::Monday;
}

config::WeatherProvider parseWeatherProvider(const String& value) {
  return value == "QWeather" ? config::WeatherProvider::QWeather
                              : config::WeatherProvider::OpenMeteo;
}

config::TemperatureUnit parseTempUnit(const String& value) {
  return value == "Fahrenheit" ? config::TemperatureUnit::Fahrenheit
                               : config::TemperatureUnit::Celsius;
}

config::WindSpeedUnit parseWindUnit(const String& value) {
  return value == "MilesPerHour"
             ? config::WindSpeedUnit::MilesPerHour
             : config::WindSpeedUnit::KilometresPerHour;
}

const char* viewStorageValue(config::CalendarView view) {
  switch (view) {
    case config::CalendarView::Week:
      return "Week";
    case config::CalendarView::Month:
      return "Month";
    default:
      return "Today";
  }
}

}  // namespace

void load() {
  config_portal::storage::PrefsStorage prefs;
  g_cache.calendarProvider = parseCalendarProvider(
      config_portal::storage::getString(prefs, kSchema, kKeyCalendarProvider));
  g_cache.icalUrl =
      config_portal::storage::getString(prefs, kSchema, kKeyIcalUrl);
  g_cache.googleCalendarIds = config_portal::storage::getString(
      prefs, kSchema, kKeyGoogleCalendarIds);
  g_cache.googleDelegatedUser = config_portal::storage::getString(
      prefs, kSchema, kKeyGoogleDelegatedUser);
  g_cache.calendarView = parseView(
      config_portal::storage::getString(prefs, kSchema, kKeyCalendarView));
  g_cache.weekStart = parseWeekStart(
      config_portal::storage::getString(prefs, kSchema, kKeyWeekStart));

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

  g_cache.locationName =
      config_portal::storage::getString(prefs, kSchema, kKeyLocationName);
  g_cache.latitude = static_cast<double>(
      config_portal::storage::getFloat(prefs, kSchema, kKeyLatitude));
  g_cache.longitude = static_cast<double>(
      config_portal::storage::getFloat(prefs, kSchema, kKeyLongitude));
  g_cache.weatherProvider = parseWeatherProvider(
      config_portal::storage::getString(prefs, kSchema, kKeyWeatherProvider));
  g_cache.tempUnit = parseTempUnit(
      config_portal::storage::getString(prefs, kSchema, kKeyTempUnit));
  g_cache.windUnit = parseWindUnit(
      config_portal::storage::getString(prefs, kSchema, kKeyWindUnit));
  g_cache.nwsAlerts =
      config_portal::storage::getBool(prefs, kSchema, kKeyNwsAlerts);
  g_cache.qwHost = loadSecret(prefs, kKeyQwHost, QWEATHER_API_HOST);
  g_cache.qwProjectId =
      loadSecret(prefs, kKeyQwProjectId, QWEATHER_PROJECT_ID);
  g_cache.qwCredentialId =
      loadSecret(prefs, kKeyQwCredentialId, QWEATHER_CREDENTIAL_ID);
  g_cache.qwPrivateKey =
      loadSecret(prefs, kKeyQwPrivateKey, QWEATHER_PRIVATE_KEY_HEX);
  g_cache.qwLang =
      config_portal::storage::getString(prefs, kSchema, kKeyQwLang);
  g_cache.qwAlerts =
      config_portal::storage::getBool(prefs, kSchema, kKeyQwAlerts);
  g_cache.debugBadges =
      config_portal::storage::getBool(prefs, kSchema, kKeyDebugBadges);
  g_cache.lowBatteryWarn =
      config_portal::storage::getBool(prefs, kSchema, kKeyLowBatteryWarn);
}

config::CalendarProvider calendarProvider() {
  return g_cache.calendarProvider;
}
const char* icalUrl() { return g_cache.icalUrl.c_str(); }
const char* googleCalendarIds() { return g_cache.googleCalendarIds.c_str(); }
const char* googleDelegatedUser() {
  return g_cache.googleDelegatedUser.c_str();
}
config::CalendarView calendarView() { return g_cache.calendarView; }
config::WeekStart weekStart() { return g_cache.weekStart; }

bool setCalendarView(config::CalendarView view) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const size_t written = prefs.putString(kKeyCalendarView, viewStorageValue(view));
  prefs.end();
  if (written == 0) return false;
  g_cache.calendarView = view;
  return true;
}

uint64_t sleepSeconds() { return g_cache.sleepSeconds; }
const char* timezone() { return g_cache.timezone.c_str(); }
bool quietHoursEnabled() { return g_cache.quietHoursEnabled; }
uint8_t quietStartHour() { return g_cache.quietStartHour; }
uint8_t quietStartMinute() { return g_cache.quietStartMinute; }
uint8_t quietEndHour() { return g_cache.quietEndHour; }
uint8_t quietEndMinute() { return g_cache.quietEndMinute; }
const char* ntpPrimary() { return g_cache.ntpPrimary.c_str(); }
const char* ntpSecondary() { return g_cache.ntpSecondary.c_str(); }

const char* locationName() { return g_cache.locationName.c_str(); }
double latitude() { return g_cache.latitude; }
double longitude() { return g_cache.longitude; }
config::WeatherProvider weatherProvider() { return g_cache.weatherProvider; }
config::TemperatureUnit temperatureUnit() { return g_cache.tempUnit; }
config::WindSpeedUnit windSpeedUnit() { return g_cache.windUnit; }
bool nwsAlertsEnabled() { return g_cache.nwsAlerts; }
const char* qweatherHost() { return g_cache.qwHost.c_str(); }
const char* qweatherProjectId() { return g_cache.qwProjectId.c_str(); }
const char* qweatherCredentialId() {
  return g_cache.qwCredentialId.c_str();
}
const char* qweatherPrivateKeyHex() { return g_cache.qwPrivateKey.c_str(); }
const char* qweatherLang() { return g_cache.qwLang.c_str(); }
bool qweatherAlertsEnabled() { return g_cache.qwAlerts; }
bool debugShowStatusBadges() { return g_cache.debugBadges; }
bool lowBatteryWarn() { return g_cache.lowBatteryWarn; }

const char* calendarProviderName() {
  return calendarProvider() == config::CalendarProvider::Google ? "Google"
                                                                : "iCalendar";
}

const char* calendarViewName() {
  return viewStorageValue(calendarView());
}

}  // namespace runtime
}  // namespace calendar_config
