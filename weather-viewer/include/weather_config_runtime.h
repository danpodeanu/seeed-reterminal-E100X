#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config.h"
#include "weather_config_schema.h"

// Runtime-configurable shadow of config.h. Values are loaded once at
// boot from NVS via config_portal::storage; when a key is missing NVS
// returns the schema default -- which mirrors the constexpr default in
// config.h. For QWeather secrets, when NVS is empty (or holds the
// placeholder sentinel) we fall back to the values baked into
// secrets.h so out-of-the-box flashes keep working.
namespace weather_config {
namespace runtime {

// Reads every configurable value from NVS and caches it. Call once
// early in setup(), after the config portal has had a chance to run.
void load();

// Location
const char*  locationName();
double       latitude();
double       longitude();

// Refresh cadence
uint64_t     sleepSeconds();
const char*  timezone();

// Quiet hours
bool         quietHoursEnabled();
uint8_t      quietStartHour();
uint8_t      quietStartMinute();
uint8_t      quietEndHour();
uint8_t      quietEndMinute();

// NTP servers
const char*  ntpPrimary();
const char*  ntpSecondary();

// Presentation
::config::WeatherProvider   weatherProvider();
::config::TemperatureUnit   temperatureUnit();
::config::WindSpeedUnit     windSpeedUnit();
bool                        clutterFreeMode();
bool                        nwsAlertsEnabled();

// QWeather credentials (NVS-first with secrets.h fallback)
const char*  qweatherHost();
const char*  qweatherProjectId();
const char*  qweatherCredentialId();
const char*  qweatherPrivateKeyHex();
const char*  qweatherLang();
bool         qweatherAlertsEnabled();

// Debug
bool         debugShowStatusBadges();
bool         logToSd();

}  // namespace runtime
}  // namespace weather_config
