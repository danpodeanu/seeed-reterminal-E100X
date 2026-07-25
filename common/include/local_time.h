#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

// System-clock helpers shared by every viewer app. All of these are pure
// time.h / <esp> operations with no per-app config baked in.
namespace local_time {

// Set the process timezone to the given TZ string (Posix format), then
// call tzset() so localtime() uses it. Call once per boot before reading
// any local time.
void configureTimezone(const char* tz);

// Returns true once the wall clock has been set past 2023-11-14 UTC. That
// is a low enough threshold to detect any successful NTP or RTC restore,
// yet high enough that a freshly booted ESP32 (starting at 1970) fails.
bool clockIsValid();

// Fill `out` with `localtime_r(time(nullptr), &out)` iff the clock is
// currently valid. Returns false if either check fails.
bool localClock(struct tm& out);

// Should we run NTP again this wake? True on cold boot, when the clock
// isn't set yet, when we've never synced, or when the retained epoch is
// older than `intervalSeconds`. Suitable to back an app's
// `ntpRefreshDue()` wrapper.
bool refreshDue(bool coldBoot, time_t lastSync, uint32_t intervalSeconds);

// Parse an ISO-8601-ish local-time string ("YYYY-MM-DDTHH:MM[:SS]"),
// interpreting it as wall-clock time in the currently configured
// timezone, and write the resulting `time_t` into `timestamp`.
// Seconds are optional (defaults to 0) but every other field must be
// present and in range. Also round-trips the result through
// localtime_r to reject values that mktime silently normalised (e.g.
// "2025-02-30"). Returns false on any parse or range failure.
bool parseIso8601Local(const String& value, time_t& timestamp);

}  // namespace local_time
