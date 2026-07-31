#pragma once

#include "config_schema.h"

// weather-viewer app configuration schema exposed via the config portal.
//
// Field keys double as NVS keys, so each is <= 15 characters. When NVS
// has no value stored for a key, the constexpr defaults in config.h are
// used instead (see weather_config_runtime.h). This means the schema and
// config.h describe the same knobs; edit both if you add a setting.
namespace weather_config {

extern const config_portal::Schema kSchema;

// NVS keys -- kept in one place so config_portal accessors and the
// weather_config_runtime helpers agree on the naming.

// Location
constexpr const char* kKeyLocationName      = "loc_name";
constexpr const char* kKeyLatitude          = "latitude";
constexpr const char* kKeyLongitude         = "longitude";

// Refresh cadence
constexpr const char* kKeySleepSeconds      = "sleep_secs";
constexpr const char* kKeyTimezone          = "timezone";

// Quiet hours
constexpr const char* kKeyQuietEnabled      = "quiet_on";
constexpr const char* kKeyQuietStartHour    = "quiet_sh";
constexpr const char* kKeyQuietStartMin     = "quiet_sm";
constexpr const char* kKeyQuietEndHour      = "quiet_eh";
constexpr const char* kKeyQuietEndMin       = "quiet_em";

// NTP servers
constexpr const char* kKeyNtpPrimary        = "ntp1";
constexpr const char* kKeyNtpSecondary      = "ntp2";

// Presentation
constexpr const char* kKeyProvider          = "provider";
constexpr const char* kKeyTempUnit          = "temp_unit";
constexpr const char* kKeyWindUnit          = "wind_unit";
constexpr const char* kKeyClutterFree       = "clutter_free";
constexpr const char* kKeyNwsAlerts         = "nws_alerts";

// QWeather credentials (sensitive; keys stay under 15 chars)
constexpr const char* kKeyQwHost            = "qw_host";
constexpr const char* kKeyQwProjectId       = "qw_proj_id";
constexpr const char* kKeyQwCredentialId    = "qw_cred_id";
constexpr const char* kKeyQwPrivateKey      = "qw_key";
constexpr const char* kKeyQwLang            = "qw_lang";
constexpr const char* kKeyQwAlerts          = "qw_alerts";

// Debug
constexpr const char* kKeyDebugBadges       = "dbg_badges";
constexpr const char* kKeyLogToSd           = "log_to_sd";

constexpr const char* kNamespace = "weather";

}  // namespace weather_config
