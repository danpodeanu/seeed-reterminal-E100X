#pragma once

#include "config_schema.h"

// xkcd-viewer app configuration schema exposed via the config portal.
//
// Field keys double as NVS keys, so each is <= 15 characters. When NVS
// has no value stored for a key, the constexpr defaults in config.h are
// used instead (see xkcd_config_runtime.h). This means the schema and
// config.h describe the same knobs; edit both if you add a setting.
namespace xkcd_config {

extern const config_portal::Schema kSchema;

// NVS keys -- kept in one place so config_portal accessors and the
// xkcd_config_runtime helpers agree on the naming.
constexpr const char* kKeySleepSeconds     = "sleep_secs";
constexpr const char* kKeyTimezone         = "timezone";
constexpr const char* kKeyQuietEnabled     = "quiet_on";
constexpr const char* kKeyQuietStartHour   = "quiet_sh";
constexpr const char* kKeyQuietStartMin    = "quiet_sm";
constexpr const char* kKeyQuietEndHour     = "quiet_eh";
constexpr const char* kKeyQuietEndMin      = "quiet_em";
constexpr const char* kKeyNtpPrimary       = "ntp1";
constexpr const char* kKeyNtpSecondary     = "ntp2";
constexpr const char* kKeyMinScale         = "min_scale";
constexpr const char* kKeyOrientation      = "orientation";
constexpr const char* kKeyDateLocale       = "date_locale";
constexpr const char* kKeyDebugBadges      = "dbg_badges";
constexpr const char* kKeyForceComic       = "dbg_force";
constexpr const char* kKeyLogToSd          = "log_to_sd";
constexpr const char* kKeyLowBatteryWarn   = "low_batt_warn";

constexpr const char* kNamespace = "xkcd";

}  // namespace xkcd_config
