#include "weather_config_schema.h"

namespace weather_config {
namespace {

using config_portal::Field;
using config_portal::FieldType;
using config_portal::Section;

const char* const kProviderValues[]   = {"OpenMeteo", "QWeather", nullptr};
const char* const kTempUnitValues[]   = {"Celsius", "Fahrenheit", nullptr};
const char* const kWindUnitValues[]   = {"KilometresPerHour", "MilesPerHour", nullptr};

const Field kLocationFields[] = {
    {kKeyLocationName,
     "Location name",
     "Display name shown on the panel header (e.g. \"London\").",
     FieldType::String, "London", nullptr, 0, 32, nullptr},
    {kKeyLatitude,
     "Latitude",
     "Decimal degrees; south is negative. QWeather rounds to two decimals.",
     FieldType::Float, "51.5074", nullptr, 0, 0, nullptr},
    {kKeyLongitude,
     "Longitude",
     "Decimal degrees; west is negative.",
     FieldType::Float, "-0.1278", nullptr, 0, 0, nullptr},
};

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

const Field kPresentationFields[] = {
    {kKeyProvider,
     "Weather provider",
     "OpenMeteo needs no API key; QWeather requires credentials below.",
     FieldType::Enum, "OpenMeteo", kProviderValues, 0, 0, nullptr},
    {kKeyTempUnit,
     "Temperature unit",
     "How temperatures are formatted for the panel.",
     FieldType::Enum, "Celsius", kTempUnitValues, 0, 0, nullptr},
    {kKeyWindUnit,
     "Wind speed unit",
     "How wind speeds are formatted for the panel.",
     FieldType::Enum, "KilometresPerHour", kWindUnitValues, 0, 0, nullptr},
    {kKeyClutterFree,
     "Clutter-free mode",
     "Hide the per-day Rain % / UV secondary line on each forecast card.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyZenithBackground,
     "Zenith background (E1001 only)",
     "Show the bundled zenith ink-wash landscape behind the main render on the 4.0-inch panel. Turn off for a plain white background.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyNwsAlerts,
     "NWS severe weather alerts",
     "Fetch US National Weather Service alerts. Only useful in the US, and only when the provider is OpenMeteo.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
};

const Field kQweatherFields[] = {
    {kKeyQwHost,
     "QWeather API host",
     "Free tier: devapi.qweather.com. Paid tier: xxxxxxxx.re.qweatherapi.com.",
     FieldType::String, "devapi.qweather.com", nullptr, 0, 63, nullptr},
    {kKeyQwProjectId,
     "QWeather Project ID",
     "The project ID from the QWeather console. Used as the JWT \"sub\" claim.",
     FieldType::String, "", nullptr, 0, 63, nullptr},
    {kKeyQwCredentialId,
     "QWeather Credential ID",
     "The credential ID from the QWeather console. Used as the JWT \"kid\" header.",
     FieldType::String, "", nullptr, 0, 63, nullptr},
    {kKeyQwPrivateKey,
     "QWeather private key (hex)",
     "The 32-byte ed25519 private seed as 64 hex chars. Redacted after save.",
     FieldType::Secret, "", nullptr, 0, 128, nullptr},
    {kKeyQwLang,
     "QWeather language",
     "Language code for condition names and warnings (e.g. en, zh, de).",
     FieldType::String, "en", nullptr, 0, 8, nullptr},
    {kKeyQwAlerts,
     "QWeather severe weather alerts",
     "Fetch /v7/warning/now. Requires the \"Weather Warning\" data resource on the QWeather project.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
};

const Field kDebugFields[] = {
    {kKeyDebugBadges,
     "Show status badges",
     "Draw the diagnostic last-refresh badge in the top-right corner of the panel.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyLogToSd,
     "Log serial to SD card",
     "Tee serial output to /logs/current.log on the SD card. Adds SD I/O per line.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
};

const Section kSections[] = {
    {"Location", kLocationFields,
     sizeof(kLocationFields) / sizeof(kLocationFields[0])},
    {"Refresh cadence", kRefreshFields,
     sizeof(kRefreshFields) / sizeof(kRefreshFields[0])},
    {"Quiet hours", kQuietFields,
     sizeof(kQuietFields) / sizeof(kQuietFields[0])},
    {"NTP servers", kNtpFields,
     sizeof(kNtpFields) / sizeof(kNtpFields[0])},
    {"Presentation", kPresentationFields,
     sizeof(kPresentationFields) / sizeof(kPresentationFields[0])},
    {"QWeather credentials", kQweatherFields,
     sizeof(kQweatherFields) / sizeof(kQweatherFields[0])},
    {"Debug", kDebugFields,
     sizeof(kDebugFields) / sizeof(kDebugFields[0])},
};

}  // namespace

const config_portal::Schema kSchema = {
    kNamespace, kSections, sizeof(kSections) / sizeof(kSections[0])};

}  // namespace weather_config
