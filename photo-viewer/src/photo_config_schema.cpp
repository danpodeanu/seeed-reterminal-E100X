#include "photo_config_schema.h"

namespace photo_config {
namespace {

using config_portal::Field;
using config_portal::FieldType;
using config_portal::Section;

#if RETERMINAL_MODEL == 1005
const char* const kOrientationValues[] = {
    "Portrait", "RotateCW", "RotateCCW", nullptr};
constexpr const char* kOrientationDefault = "Portrait";
constexpr const char* kOrientationHelp =
    "E1005 supports portrait and both physical landscape directions. Browser uploads follow this setting.";
#else
const char* const kOrientationValues[] = {
    "Native", "RotateCW", "RotateCCW", nullptr};
constexpr const char* kOrientationDefault = "Native";
constexpr const char* kOrientationHelp =
    "E1004 supports native portrait and both landscape directions. Other models ignore this setting.";
#endif

const Field kRefreshFields[] = {
    {kKeySleepSeconds,
     "Sleep between photo changes (s)",
     "Deep-sleep interval between automatic photo changes. Range 60..86400 (1 min .. 24 h).",
     FieldType::Int, "3600", nullptr, 60, 86400, nullptr},
    {kKeyTimezone,
     "Timezone",
     "Pick your region or choose \"Custom (POSIX)\" and enter a POSIX TZ string.",
     FieldType::Timezone, "GMT0BST,M3.5.0/1,M10.5.0/2", nullptr, 0, 32, nullptr},
};

const Field kQuietFields[] = {
    {kKeyQuietEnabled,
     "Quiet hours enabled",
     "Suppress automatic photo changes during quiet hours. Button wakes still change the photo.",
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

const Field kPhotoFields[] = {
    {kKeyRandomOrder,
     "Random photo order",
     "When on, photo order is shuffled at each boot. When off, files are shown in alphabetical order.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyOrientation,
     "Panel orientation",
     kOrientationHelp,
     FieldType::Enum, kOrientationDefault, kOrientationValues, 0, 0, nullptr},
};

const Field kBatteryFields[] = {
    {kKeyLowBatteryWarn,
     "Show low-battery warning",
     "When the battery drops below 5% (on boards with a battery sensor), replace the photo with a 'please recharge' screen.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
};

const Field kDebugFields[] = {
    {kKeyLogToSd,
     "Log serial to SD card",
     "Tee serial output to /logs/current.log on the SD card. Adds SD I/O per line.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
    {kKeyPinnedPhoto,
     "Pinned photo filename",
     "Debug override: when set, only this photo is shown (filename only, e.g. \"IMG_0001.bmp\"). Leave blank to rotate through all photos in /photos.",
     FieldType::String, "", nullptr, 0, 63, nullptr},
};

const Section kSections[] = {
    {"Refresh cadence", kRefreshFields,
     sizeof(kRefreshFields) / sizeof(kRefreshFields[0])},
    {"Quiet hours", kQuietFields,
     sizeof(kQuietFields) / sizeof(kQuietFields[0])},
    {"NTP servers", kNtpFields,
     sizeof(kNtpFields) / sizeof(kNtpFields[0])},
    {"Photos", kPhotoFields,
     sizeof(kPhotoFields) / sizeof(kPhotoFields[0])},
    {"Battery", kBatteryFields,
     sizeof(kBatteryFields) / sizeof(kBatteryFields[0])},
    {"Debug", kDebugFields,
     sizeof(kDebugFields) / sizeof(kDebugFields[0])},
};

}  // namespace

const config_portal::Schema kSchema = {
    kNamespace, kSections, sizeof(kSections) / sizeof(kSections[0])};

}  // namespace photo_config
