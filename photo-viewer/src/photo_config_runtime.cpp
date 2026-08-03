#include "photo_config_runtime.h"

#include "config_storage.h"

namespace photo_config {
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

  bool     randomOrder = ::config::PHOTO_ORDER_RANDOM;
  ::config::Orientation orientation = ::config::ORIENTATION_DEFAULT;
  bool     logToSd     = ::config::LOG_TO_SD;
  String   pinnedPhoto;  // empty = disabled (rotate through all photos)
  bool     lowBatteryWarn = ::config::LOW_BATTERY_WARN_ENABLED;
};

Cache g_cache;
bool  g_loaded = false;

::config::Orientation parseOrientation(const String& s) {
  if (s == "RotateCW") return ::config::Orientation::RotateCW;
  if (s == "RotateCCW") return ::config::Orientation::RotateCCW;
  return ::config::Orientation::Native;
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  // Each schema helper (getInt/getString/getBool) opens its own NVS
  // session and closes it; see xkcd_config_runtime.cpp for why we
  // don't hold the namespace open here.
  PrefsStorage prefs;

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

  g_cache.randomOrder =
      config_portal::storage::getBool(prefs, kSchema, kKeyRandomOrder);
  g_cache.orientation = parseOrientation(
      config_portal::storage::getString(prefs, kSchema, kKeyOrientation));
  g_cache.logToSd =
      config_portal::storage::getBool(prefs, kSchema, kKeyLogToSd);
  g_cache.pinnedPhoto =
      config_portal::storage::getString(prefs, kSchema, kKeyPinnedPhoto);
  g_cache.lowBatteryWarn =
      config_portal::storage::getBool(prefs, kSchema, kKeyLowBatteryWarn);

  g_loaded = true;
}

uint64_t    sleepSeconds()      { return g_cache.sleepSeconds; }
const char* timezone()          { return g_cache.timezone.c_str(); }

bool        quietHoursEnabled() { return g_cache.quietHoursEnabled; }
uint8_t     quietStartHour()    { return g_cache.quietStartHour; }
uint8_t     quietStartMinute()  { return g_cache.quietStartMinute; }
uint8_t     quietEndHour()      { return g_cache.quietEndHour; }
uint8_t     quietEndMinute()    { return g_cache.quietEndMinute; }

const char* ntpPrimary()        { return g_cache.ntpPrimary.c_str(); }
const char* ntpSecondary()      { return g_cache.ntpSecondary.c_str(); }

bool        randomOrder()       { return g_cache.randomOrder; }
::config::Orientation orientation() {
  // Only E1004 is a portrait-native panel that can be physically rotated.
  // On the other boards the panel is landscape at the driver level, so
  // "no rotation" (Native) is already what the user wants and the setting
  // is hard-locked here to keep call sites simple.
  if (::config::MODEL != 1004) return ::config::Orientation::Native;
  return g_cache.orientation;
}
bool        isLandscape() {
  return orientation() != ::config::Orientation::Native;
}
int         effectivePanelWidth() {
  return isLandscape() ? ::config::PANEL_HEIGHT : ::config::PANEL_WIDTH;
}
int         effectivePanelHeight() {
  return isLandscape() ? ::config::PANEL_WIDTH : ::config::PANEL_HEIGHT;
}
bool        logToSd()           { return g_cache.logToSd; }
const char* pinnedPhoto()       { return g_cache.pinnedPhoto.c_str(); }
bool        lowBatteryWarn()    { return g_cache.lowBatteryWarn; }

}  // namespace runtime
}  // namespace photo_config
