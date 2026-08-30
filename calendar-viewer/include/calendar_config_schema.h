#pragma once

#include "config_schema.h"

namespace calendar_config {

extern const config_portal::Schema kSchema;
constexpr const char* kNamespace = "calendar";

constexpr const char* kKeyCalendarProvider = "cal_provider";
constexpr const char* kKeyIcalUrl = "ical_url";
constexpr const char* kKeyGoogleCalendarIds = "gcal_ids";
constexpr const char* kKeyGoogleDelegatedUser = "gcal_user";
constexpr const char* kKeyWeekStart = "week_start";
constexpr const char* kKeyTimeFormat = "time_format";
constexpr const char* kKeyShowSingleCalendarBackground = "single_cal_bg";

constexpr const char* kKeySleepSeconds = "sleep_secs";
constexpr const char* kKeyTimezone = "timezone";
constexpr const char* kKeyQuietEnabled = "quiet_on";
constexpr const char* kKeyQuietStartHour = "quiet_sh";
constexpr const char* kKeyQuietStartMin = "quiet_sm";
constexpr const char* kKeyQuietEndHour = "quiet_eh";
constexpr const char* kKeyQuietEndMin = "quiet_em";
constexpr const char* kKeyNtpPrimary = "ntp1";
constexpr const char* kKeyNtpSecondary = "ntp2";

constexpr const char* kKeyLocationName = "loc_name";
constexpr const char* kKeyLatitude = "latitude";
constexpr const char* kKeyLongitude = "longitude";
constexpr const char* kKeyWeatherProvider = "wx_provider";
constexpr const char* kKeyTempUnit = "temp_unit";
constexpr const char* kKeyWindUnit = "wind_unit";
constexpr const char* kKeyNwsAlerts = "nws_alerts";
constexpr const char* kKeyQwHost = "qw_host";
constexpr const char* kKeyQwProjectId = "qw_proj_id";
constexpr const char* kKeyQwCredentialId = "qw_cred_id";
constexpr const char* kKeyQwPrivateKey = "qw_key";
constexpr const char* kKeyQwLang = "qw_lang";
constexpr const char* kKeyQwAlerts = "qw_alerts";

constexpr const char* kKeyDebugBadges = "dbg_badges";
constexpr const char* kKeyLowBatteryWarn = "low_batt_warn";

}  // namespace calendar_config
