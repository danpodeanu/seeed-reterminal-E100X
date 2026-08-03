#include "xkcd_config_schema.h"

namespace xkcd_config {
namespace {

using config_portal::Field;
using config_portal::FieldType;
using config_portal::Section;

const char* const kDateLocaleValues[] = {"DMY", "MDY", "YMD", nullptr};

const Field kRefreshFields[] = {
    {kKeySleepSeconds,
     "Sleep between refreshes (s)",
     "Deep-sleep interval between automatic refreshes. Range 60..21600.",
     FieldType::Int, "900", nullptr, 60, 21600, nullptr},
    {kKeyTimezone,
     "Timezone",
     "Pick your region or choose \"Custom (POSIX)\" and enter a POSIX TZ string.",
     FieldType::Timezone, "GMT0BST,M3.5.0/1,M10.5.0/2", nullptr, 0, 32, nullptr},
};

const Field kQuietFields[] = {
    {kKeyQuietEnabled,
     "Quiet hours enabled",
     "Suppress automatic refreshes during quiet hours. Button wakes still refresh.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyQuietStartHour,
     "Quiet-hours start (hour)",
     "Hour of day (0..23) when quiet hours begin.",
     FieldType::Int, "1", nullptr, 0, 23, nullptr},
    {kKeyQuietStartMin,
     "Quiet-hours start (minute)",
     "Minute of hour (0..59) when quiet hours begin.",
     FieldType::Int, "0", nullptr, 0, 59, nullptr},
    {kKeyQuietEndHour,
     "Quiet-hours end (hour)",
     "Hour of day (0..23) when quiet hours end.",
     FieldType::Int, "7", nullptr, 0, 23, nullptr},
    {kKeyQuietEndMin,
     "Quiet-hours end (minute)",
     "Minute of hour (0..59) when quiet hours end.",
     FieldType::Int, "0", nullptr, 0, 59, nullptr},
};

const Field kNtpFields[] = {
    {kKeyNtpPrimary,
     "Primary NTP server",
     "Fallback NTP server when DHCP doesn't offer one.",
     FieldType::String, "pool.ntp.org", nullptr, 0, 63, nullptr},
    {kKeyNtpSecondary,
     "Secondary NTP server",
     "Secondary NTP server used if the primary fails.",
     FieldType::String, "time.cloudflare.com", nullptr, 0, 63, nullptr},
};

const Field kDisplayFields[] = {
    {kKeyMinScale,
     "Minimum display scale",
     "Minimum on-panel scale factor. Comics needing less are skipped. Range 0.10..1.00.",
     FieldType::Float, "0.65", nullptr, 0, 0, nullptr},
    {kKeyDateLocale,
     "Date format",
     "How the publication date is rendered above the footer.",
     FieldType::Enum, "DMY", kDateLocaleValues, 0, 0, nullptr},
};

const Field kDebugFields[] = {
    {kKeyDebugBadges,
     "Show status badges",
     "Draw the diagnostic badges in the top-right corner of the panel.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyForceComic,
     "Force comic number",
     "Debug: when > 0, the next cold boot loads this exact comic from the local cache.",
     FieldType::Int, "0", nullptr, 0, 100000, nullptr},
    {kKeyLogToSd,
     "Log serial to SD card",
     "Tee serial output to /logs/current.log on the SD card. Adds SD I/O per line.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
};

const Field kBatteryFields[] = {
    {kKeyLowBatteryWarn,
     "Show low-battery warning",
     "When the battery drops below 5% (on boards with a battery sensor), replace the comic with a 'please recharge' screen.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
};

const Section kSections[] = {
    {"Refresh cadence", kRefreshFields,
     sizeof(kRefreshFields) / sizeof(kRefreshFields[0])},
    {"Quiet hours", kQuietFields,
     sizeof(kQuietFields) / sizeof(kQuietFields[0])},
    {"NTP servers", kNtpFields,
     sizeof(kNtpFields) / sizeof(kNtpFields[0])},
    {"Display", kDisplayFields,
     sizeof(kDisplayFields) / sizeof(kDisplayFields[0])},
    {"Debug", kDebugFields,
     sizeof(kDebugFields) / sizeof(kDebugFields[0])},
    {"Battery", kBatteryFields,
     sizeof(kBatteryFields) / sizeof(kBatteryFields[0])},
};

}  // namespace

const config_portal::Schema kSchema = {
    kNamespace, kSections, sizeof(kSections) / sizeof(kSections[0])};

}  // namespace xkcd_config
