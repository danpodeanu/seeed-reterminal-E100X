#pragma once

#include "config_schema.h"

// photo-viewer app configuration schema exposed via the config portal.
//
// Field keys double as NVS keys, so each is <= 15 characters. When NVS
// has no value stored for a key, the constexpr defaults in config.h are
// used instead (see photo_config_runtime.h). This means the schema and
// config.h describe the same knobs; edit both if you add a setting.
namespace photo_config {

extern const config_portal::Schema kSchema;

// NVS keys -- kept in one place so config_portal accessors and the
// photo_config_runtime helpers agree on the naming.
constexpr const char* kKeySleepSeconds   = "sleep_secs";
constexpr const char* kKeyTimezone       = "timezone";

constexpr const char* kKeyQuietEnabled   = "quiet_on";
constexpr const char* kKeyQuietStartHour = "quiet_sh";
constexpr const char* kKeyQuietStartMin  = "quiet_sm";
constexpr const char* kKeyQuietEndHour   = "quiet_eh";
constexpr const char* kKeyQuietEndMin    = "quiet_em";

constexpr const char* kKeyNtpPrimary     = "ntp1";
constexpr const char* kKeyNtpSecondary   = "ntp2";

constexpr const char* kKeyRandomOrder    = "rand_order";
constexpr const char* kKeyLogToSd        = "log_to_sd";
constexpr const char* kKeyPinnedPhoto    = "pinned_photo";
constexpr const char* kKeyLowBatteryWarn = "low_batt_warn";

constexpr const char* kNamespace = "photo";

}  // namespace photo_config
