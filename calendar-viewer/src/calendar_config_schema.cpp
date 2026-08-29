#include "calendar_config_schema.h"

namespace calendar_config {
namespace {

using config_portal::Field;
using config_portal::FieldType;
using config_portal::Section;

const char* const kCalendarProviderValues[] = {"Ical", "Google", nullptr};
const char* const kWeekStartValues[] = {"Monday", "Sunday", nullptr};
const char* const kWeatherProviderValues[] = {"OpenMeteo", "QWeather", nullptr};
const char* const kTempUnitValues[] = {"Celsius", "Fahrenheit", nullptr};
const char* const kWindUnitValues[] = {
    "KilometresPerHour", "MilesPerHour", nullptr};

const Field kCalendarFields[] = {
    {kKeyCalendarProvider, "Calendar source",
     "Use a public iCalendar URL or calendars shared with a Google service account.",
     FieldType::Enum, "Ical", kCalendarProviderValues, 0, 0, nullptr},
    {kKeyIcalUrl, "iCalendar URL",
     "HTTP(S) link to a public .ics feed. Prefer HTTPS. Saved only in device NVS and redacted after save.",
     FieldType::Secret, "", nullptr, 0, 2048, nullptr},
    {kKeyGoogleCalendarIds, "Google calendar IDs",
     "Optional comma-separated IDs to query directly. The device adds them to the service account's CalendarList when needed so their colors can be read.",
     FieldType::String, "", nullptr, 0, 512, nullptr},
    {kKeyGoogleDelegatedUser, "Google delegated user",
     "Optional Workspace user email for domain-wide delegation. Leave blank for calendars shared directly with the service account.",
     FieldType::String, "", nullptr, 0, 128, nullptr},
};

const Field kPresentationFields[] = {
    {kKeyWeekStart, "First day of week",
     "Used by the weekly and monthly calendar grids.",
     FieldType::Enum, "Monday", kWeekStartValues, 0, 0, nullptr},
};

const Field kRefreshFields[] = {
    {kKeySleepSeconds, "Sleep between updates (s)",
     "Wake, reconnect, and check calendar/weather data. Range 60..21600.",
     FieldType::Int, "900", nullptr, 60, 21600, nullptr},
    {kKeyTimezone, "Timezone",
     "Pick a region or choose Custom (POSIX) and enter a POSIX TZ string.",
     FieldType::Timezone, "GMT0BST,M3.5.0/1,M10.5.0/2", nullptr, 0, 64,
     nullptr},
};

const Field kQuietFields[] = {
    {kKeyQuietEnabled, "Quiet hours enabled",
     "Suppress scheduled checks during quiet hours. Button wakes still refresh.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyQuietStartHour, "Quiet-hours start (hour)", "0..23",
     FieldType::Int, "1", nullptr, 0, 23, nullptr},
    {kKeyQuietStartMin, "Quiet-hours start (minute)", "0..59",
     FieldType::Int, "0", nullptr, 0, 59, nullptr},
    {kKeyQuietEndHour, "Quiet-hours end (hour)", "0..23",
     FieldType::Int, "7", nullptr, 0, 23, nullptr},
    {kKeyQuietEndMin, "Quiet-hours end (minute)", "0..59",
     FieldType::Int, "0", nullptr, 0, 59, nullptr},
};

const Field kNtpFields[] = {
    {kKeyNtpPrimary, "Primary NTP server",
     "Fallback used when DHCP does not supply a working NTP server.",
     FieldType::String, "pool.ntp.org", nullptr, 0, 63, nullptr},
    {kKeyNtpSecondary, "Secondary NTP server", "Secondary time source.",
     FieldType::String, "time.cloudflare.com", nullptr, 0, 63, nullptr},
};

const Field kWeatherFields[] = {
    {kKeyLocationName, "Location name", "Label shown beside the forecast.",
     FieldType::String, "London", nullptr, 0, 32, nullptr},
    {kKeyLatitude, "Latitude", "Decimal degrees; south is negative.",
     FieldType::Float, "51.5074", nullptr, 0, 0, nullptr},
    {kKeyLongitude, "Longitude", "Decimal degrees; west is negative.",
     FieldType::Float, "-0.1278", nullptr, 0, 0, nullptr},
    {kKeyWeatherProvider, "Weather provider",
     "Open-Meteo needs no key; QWeather uses the credentials below.",
     FieldType::Enum, "OpenMeteo", kWeatherProviderValues, 0, 0, nullptr},
    {kKeyTempUnit, "Temperature unit", "Panel temperature units.",
     FieldType::Enum, "Celsius", kTempUnitValues, 0, 0, nullptr},
    {kKeyWindUnit, "Wind speed unit", "Panel wind units.",
     FieldType::Enum, "KilometresPerHour", kWindUnitValues, 0, 0, nullptr},
    {kKeyNwsAlerts, "NWS severe weather alerts",
     "Fetch active alerts for US locations when using Open-Meteo.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
};

const Field kQweatherFields[] = {
    {kKeyQwHost, "QWeather API host",
     "Free tier: devapi.qweather.com. Paid tiers use the assigned host.",
     FieldType::String, "devapi.qweather.com", nullptr, 0, 63, nullptr},
    {kKeyQwProjectId, "QWeather Project ID", "JWT sub claim.",
     FieldType::String, "", nullptr, 0, 63, nullptr},
    {kKeyQwCredentialId, "QWeather Credential ID", "JWT kid header.",
     FieldType::String, "", nullptr, 0, 63, nullptr},
    {kKeyQwPrivateKey, "QWeather private key (hex)",
     "32-byte Ed25519 private seed. Redacted after save.",
     FieldType::Secret, "", nullptr, 0, 128, nullptr},
    {kKeyQwLang, "QWeather language", "Language code such as en, zh, or de.",
     FieldType::String, "en", nullptr, 0, 8, nullptr},
    {kKeyQwAlerts, "QWeather severe weather alerts",
     "Fetch warning data when enabled for the QWeather project.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
};

const Field kDeviceFields[] = {
    {kKeyLowBatteryWarn, "Show low-battery warning",
     "Show a recharge screen below 5% when a battery reading is available.",
     FieldType::Bool, "true", nullptr, 0, 0, nullptr},
    {kKeyDebugBadges, "Show update diagnostics",
     "Show source and last-check details in the footer.",
     FieldType::Bool, "false", nullptr, 0, 0, nullptr},
};

const Section kSections[] = {
    {"Calendar source", kCalendarFields,
     sizeof(kCalendarFields) / sizeof(kCalendarFields[0])},
    {"Presentation", kPresentationFields,
     sizeof(kPresentationFields) / sizeof(kPresentationFields[0])},
    {"Refresh cadence", kRefreshFields,
     sizeof(kRefreshFields) / sizeof(kRefreshFields[0])},
    {"Quiet hours", kQuietFields,
     sizeof(kQuietFields) / sizeof(kQuietFields[0])},
    {"NTP servers", kNtpFields, sizeof(kNtpFields) / sizeof(kNtpFields[0])},
    {"Weather", kWeatherFields,
     sizeof(kWeatherFields) / sizeof(kWeatherFields[0])},
    {"QWeather credentials", kQweatherFields,
     sizeof(kQweatherFields) / sizeof(kQweatherFields[0])},
    {"Device", kDeviceFields,
     sizeof(kDeviceFields) / sizeof(kDeviceFields[0])},
};

}  // namespace

const config_portal::Schema kSchema = {
    kNamespace, kSections, sizeof(kSections) / sizeof(kSections[0])};

}  // namespace calendar_config
