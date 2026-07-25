#pragma once

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

}  // namespace local_time
