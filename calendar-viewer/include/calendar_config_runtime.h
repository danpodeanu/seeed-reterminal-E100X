#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config.h"

namespace calendar_config {
namespace runtime {

void load();

config::CalendarProvider calendarProvider();
const char* icalUrl();
const char* googleCalendarIds();
const char* googleDelegatedUser();
config::WeekStart weekStart();
config::TimeFormat timeFormat();
bool showSingleCalendarBackground();

uint64_t sleepSeconds();
config::E1005PowerMode e1005PowerMode();
const char* timezone();
bool quietHoursEnabled();
uint8_t quietStartHour();
uint8_t quietStartMinute();
uint8_t quietEndHour();
uint8_t quietEndMinute();
const char* ntpPrimary();
const char* ntpSecondary();

const char* locationName();
double latitude();
double longitude();
config::WeatherProvider weatherProvider();
config::TemperatureUnit temperatureUnit();
config::WindSpeedUnit windSpeedUnit();
bool nwsAlertsEnabled();
const char* qweatherHost();
const char* qweatherProjectId();
const char* qweatherCredentialId();
const char* qweatherPrivateKeyHex();
const char* qweatherLang();
bool qweatherAlertsEnabled();

bool debugShowStatusBadges();
bool lowBatteryWarn();

const char* calendarProviderName();

}  // namespace runtime
}  // namespace calendar_config
