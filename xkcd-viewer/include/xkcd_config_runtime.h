#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config.h"
#include "xkcd_config_schema.h"

// Runtime-configurable shadow of config.h. Values are loaded once at
// boot from NVS via config_portal::storage; when a key is missing NVS
// returns the schema default -- which mirrors the constexpr default in
// config.h. Callers should treat these as read-only for the lifetime
// of a boot.
namespace xkcd_config {
namespace runtime {

// Reads every configurable value from NVS and caches it. Call once
// early in setup(), after the config portal has had a chance to run.
void load();

uint64_t     sleepSeconds();
const char*  timezone();

bool         quietHoursEnabled();
uint8_t      quietStartHour();
uint8_t      quietStartMinute();
uint8_t      quietEndHour();
uint8_t      quietEndMinute();

const char*  ntpPrimary();
const char*  ntpSecondary();

float        minDisplayScale();
::config::DateLocale dateLocale();

bool         debugShowStatusBadges();
int32_t      debugForceComic();
bool         logToSd();
bool         lowBatteryWarn();

}  // namespace runtime
}  // namespace xkcd_config
