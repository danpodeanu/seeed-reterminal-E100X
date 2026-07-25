#pragma once

#include <time.h>

#include "pcf8563_utc.h"

// Thin PCF8563 helpers used by every viewer app. These wrap pcf8563_utc
// with the standard "ensure I2C is up, log on failure" pattern that every
// app was repeating verbatim.
namespace rtc_sync {

// Read the current time out of the PCF8563 into `stored` and log the
// result. Returns false if the I2C bus can't be brought up or the read
// fails. Callers should still check `stored.voltageLow` before trusting
// the timestamp.
bool readAndLog(pcf8563::Reading& stored);

// After a successful NTP sync, push the new time into the PCF8563 and
// log a verification read. Silent (aside from log lines) if I2C is down.
void saveTime(time_t now);

// Restore the ESP32 wall clock from the PCF8563 on boot. Returns false
// if the RTC read failed or the stored timestamp was rejected as invalid
// (e.g. voltage-low flag set).
bool restoreSystemClock();

}  // namespace rtc_sync
