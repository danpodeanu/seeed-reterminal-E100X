#include "xkcd_config_runtime.h"

#include "config_storage.h"

namespace xkcd_config {
namespace runtime {
namespace {

struct Cache {
  uint64_t sleepSeconds = ::config::SLEEP_SECONDS;
  String   timezone     = ::config::TIMEZONE;

  bool     quietHoursEnabled = ::config::QUIET_HOURS_ENABLED;
  uint8_t  quietStartHour    = ::config::QUIET_START_HOUR;
  uint8_t  quietStartMinute  = ::config::QUIET_START_MINUTE;
  uint8_t  quietEndHour      = ::config::QUIET_END_HOUR;
  uint8_t  quietEndMinute    = ::config::QUIET_END_MINUTE;

  String   ntpPrimary   = ::config::NTP_SERVER_PRIMARY;
  String   ntpSecondary = ::config::NTP_SERVER_SECONDARY;

  float    minDisplayScale = ::config::MIN_DISPLAY_SCALE;
  ::config::DateLocale dateLocale = ::config::DATE_LOCALE;

  bool     debugShowStatusBadges = ::config::DEBUG_SHOW_STATUS_BADGES;
  int32_t  debugForceComic       = ::config::DEBUG_FORCE_COMIC;
  bool     logToSd               = ::config::LOG_TO_SD;
};

Cache g_cache;
bool  g_loaded = false;

::config::DateLocale parseDateLocale(const String& s) {
  if (s == "MDY") return ::config::DateLocale::MDY;
  if (s == "YMD") return ::config::DateLocale::YMD;
  return ::config::DateLocale::DMY;
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  PrefsStorage prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
    g_loaded = true;
    return;
  }

  g_cache.sleepSeconds =
      static_cast<uint64_t>(config_portal::storage::getInt(prefs, kSchema, kKeySleepSeconds));
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

  g_cache.minDisplayScale =
      config_portal::storage::getFloat(prefs, kSchema, kKeyMinScale);
  g_cache.dateLocale =
      parseDateLocale(config_portal::storage::getString(prefs, kSchema, kKeyDateLocale));

  g_cache.debugShowStatusBadges =
      config_portal::storage::getBool(prefs, kSchema, kKeyDebugBadges);
  g_cache.debugForceComic =
      config_portal::storage::getInt(prefs, kSchema, kKeyForceComic);
  g_cache.logToSd =
      config_portal::storage::getBool(prefs, kSchema, kKeyLogToSd);

  prefs.end();
  g_loaded = true;
}

uint64_t     sleepSeconds()          { return g_cache.sleepSeconds; }
const char*  timezone()              { return g_cache.timezone.c_str(); }

bool         quietHoursEnabled()     { return g_cache.quietHoursEnabled; }
uint8_t      quietStartHour()        { return g_cache.quietStartHour; }
uint8_t      quietStartMinute()      { return g_cache.quietStartMinute; }
uint8_t      quietEndHour()          { return g_cache.quietEndHour; }
uint8_t      quietEndMinute()        { return g_cache.quietEndMinute; }

const char*  ntpPrimary()            { return g_cache.ntpPrimary.c_str(); }
const char*  ntpSecondary()          { return g_cache.ntpSecondary.c_str(); }

float        minDisplayScale()       { return g_cache.minDisplayScale; }
::config::DateLocale dateLocale()    { return g_cache.dateLocale; }

bool         debugShowStatusBadges() { return g_cache.debugShowStatusBadges; }
int32_t      debugForceComic()       { return g_cache.debugForceComic; }
bool         logToSd()               { return g_cache.logToSd; }

}  // namespace runtime
}  // namespace xkcd_config
