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
  xkcd_orientation::Orientation orientation =
      ::config::ORIENTATION_DEFAULT;
  ::config::DateLocale dateLocale = ::config::DATE_LOCALE;

  bool     debugShowStatusBadges = ::config::DEBUG_SHOW_STATUS_BADGES;
  int32_t  debugForceComic       = ::config::DEBUG_FORCE_COMIC;
  bool     logToSd               = ::config::LOG_TO_SD;
  bool     lowBatteryWarn        = ::config::LOW_BATTERY_WARN_ENABLED;
};

Cache g_cache;
bool  g_loaded = false;

::config::DateLocale parseDateLocale(const String& s) {
  if (s == "MDY") return ::config::DateLocale::MDY;
  if (s == "YMD") return ::config::DateLocale::YMD;
  return ::config::DateLocale::DMY;
}

xkcd_orientation::Orientation parseOrientation(const String& s) {
  if (s == "RotateCW") return xkcd_orientation::Orientation::RotateCW;
  if (s == "RotateCCW") return xkcd_orientation::Orientation::RotateCCW;
  return xkcd_orientation::Orientation::Portrait;
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  // Each schema helper (getInt/getString/getBool/getFloat) opens its
  // own NVS session and closes it. If we held the namespace open here
  // ourselves those inner ``begin()`` calls would fail (Arduino's
  // Preferences::begin returns false when the handle is already open),
  // and every field would silently fall back to its schema default -
  // which is why the whole Settings page used to look like nothing had
  // been saved even after the portal wrote the values to NVS.
  PrefsStorage prefs;

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
  g_cache.orientation = parseOrientation(
      config_portal::storage::getString(prefs, kSchema, kKeyOrientation));
  g_cache.dateLocale =
      parseDateLocale(config_portal::storage::getString(prefs, kSchema, kKeyDateLocale));

  g_cache.debugShowStatusBadges =
      config_portal::storage::getBool(prefs, kSchema, kKeyDebugBadges);
  g_cache.debugForceComic =
      config_portal::storage::getInt(prefs, kSchema, kKeyForceComic);
  g_cache.logToSd =
      config_portal::storage::getBool(prefs, kSchema, kKeyLogToSd);
  g_cache.lowBatteryWarn =
      config_portal::storage::getBool(prefs, kSchema, kKeyLowBatteryWarn);

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
xkcd_orientation::Orientation orientation() {
  if (::config::MODEL != 1005) {
    return xkcd_orientation::Orientation::Portrait;
  }
  return g_cache.orientation;
}
bool isLandscape() {
  return xkcd_orientation::isLandscape(orientation());
}
int panelRotation() {
  return ::config::MODEL == 1005
             ? xkcd_orientation::panelRotation(orientation())
             : 0;
}
int panelWidth() {
  return ::config::MODEL == 1005
             ? xkcd_orientation::panelWidth(orientation())
             : ::config::PANEL_WIDTH;
}
int panelHeight() {
  return ::config::MODEL == 1005
             ? xkcd_orientation::panelHeight(orientation())
             : ::config::PANEL_HEIGHT;
}
::config::DateLocale dateLocale()    { return g_cache.dateLocale; }

bool         debugShowStatusBadges() { return g_cache.debugShowStatusBadges; }
int32_t      debugForceComic()       { return g_cache.debugForceComic; }
bool         logToSd()               { return g_cache.logToSd; }
bool         lowBatteryWarn()        { return g_cache.lowBatteryWarn; }

}  // namespace runtime
}  // namespace xkcd_config
