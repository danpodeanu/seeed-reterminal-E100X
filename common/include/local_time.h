#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#if defined(_WIN32) && !defined(HAVE_LOCALTIME_R_SHIM)
#define HAVE_LOCALTIME_R_SHIM 1
static inline struct tm* localtime_r(const time_t* t, struct tm* out) {
  return localtime_s(out, t) == 0 ? out : nullptr;
}
static inline int setenv(const char* name, const char* value, int) {
  return _putenv_s(name, value);
}
#endif

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
// timezone, and write the resulting `time_t` into `timestamp`. Seconds
// are optional (defaults to 0) but every other field must be present
// and in range. Also round-trips the result through localtime_r to
// reject values that mktime silently normalised (e.g. "2025-02-30").
// Returns false on any parse or range failure. Inline so unit tests can
// call it on the native platform without linking the common library.
inline bool parseIso8601Local(const char* value, time_t& timestamp) {
  if (value == nullptr) return false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  const int fields = sscanf(value, "%d-%d-%dT%d:%d:%d",
                            &year, &month, &day, &hour, &minute, &second);
  if (fields < 5 || year < 1970 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }

  struct tm parsed = {};
  parsed.tm_year = year - 1900;
  parsed.tm_mon = month - 1;
  parsed.tm_mday = day;
  parsed.tm_hour = hour;
  parsed.tm_min = minute;
  parsed.tm_sec = second;
  parsed.tm_isdst = -1;
  timestamp = mktime(&parsed);
  if (timestamp <= 0) return false;

  struct tm roundTrip = {};
  if (localtime_r(&timestamp, &roundTrip) == nullptr) return false;
  return roundTrip.tm_year == year - 1900 &&
         roundTrip.tm_mon == month - 1 &&
         roundTrip.tm_mday == day &&
         roundTrip.tm_hour == hour &&
         roundTrip.tm_min == minute &&
         roundTrip.tm_sec == second;
}

}  // namespace local_time
