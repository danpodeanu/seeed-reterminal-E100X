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

// Portable "days since 1970-01-01" for a Gregorian date via Howard
// Hinnant's civil-days formula (public-domain). Used to compute UTC
// epochs without depending on timegm (which ESP32 newlib omits) and
// without touching the process TZ. Range: any y/m/d representable by
// int; the caller must supply a real Gregorian date.
inline int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + doe - 719468;
}

// Compute the UTC epoch seconds for a Gregorian (y, m, d, h, m, s)
// tuple that is *already* in UTC. Side-effect-free (no TZ swap).
inline time_t utcEpochSeconds(int year, int month, int day,
                              int hour, int minute, int second) {
  const int64_t days = daysFromCivil(year, month, day);
  return static_cast<time_t>(days * 86400LL + hour * 3600LL +
                             minute * 60LL + second);
}

// Parse "YYYY-MM-DDTHH:MM[:SS]{Z|±HH:MM}" into a UTC epoch. Returns
// false if the string is missing required fields, is out of range, or
// carries no timezone marker at all. Use parseIso8601Local() instead
// when the source is known to be device-local wall clock without an
// offset.
//
// The offset is REQUIRED and interpreted correctly. This is the right
// entry point for provider timestamps that arrive with an explicit
// offset attached (QWeather obsTime "+01:00", RFC 3339 in general).
inline bool parseIso8601Utc(const char* value, time_t& out) {
  if (value == nullptr) return false;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  const int fields = sscanf(value, "%d-%d-%dT%d:%d:%d",
                            &year, &month, &day, &hour, &minute, &second);
  if (fields < 5 || year < 1970 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }

  // Skip past "YYYY-MM-DD" to find the offset marker.
  size_t i = 0;
  while (value[i] && i < 10) ++i;
  while (value[i] && value[i] != '+' && value[i] != '-' && value[i] != 'Z') ++i;

  int offsetMinutes = 0;
  if (value[i] == 'Z') {
    offsetMinutes = 0;
  } else if (value[i] == '+' || value[i] == '-') {
    const int sign = (value[i] == '+') ? 1 : -1;
    int offH = 0, offM = 0;
    if (sscanf(value + i + 1, "%d:%d", &offH, &offM) != 2) return false;
    offsetMinutes = sign * (offH * 60 + offM);
  } else {
    return false;  // no offset -> caller should use parseIso8601Local
  }

  out = utcEpochSeconds(year, month, day, hour, minute, second) -
        static_cast<time_t>(offsetMinutes) * 60;
  return out > 0;
}

// Format a UTC epoch as the device-local wall clock string
// "YYYY-MM-DDTHH:MM:SS". Returns an empty string when epoch is 0 or
// localtime_r fails. Prints to the caller's buffer to avoid heap.
inline size_t formatLocalIso(time_t epoch, char* out, size_t outLen) {
  if (epoch == 0 || out == nullptr || outLen == 0) {
    if (out != nullptr && outLen > 0) out[0] = '\0';
    return 0;
  }
  struct tm tm = {};
  if (localtime_r(&epoch, &tm) == nullptr) {
    out[0] = '\0';
    return 0;
  }
  const int written = snprintf(
      out, outLen, "%04d-%02d-%02dT%02d:%02d:%02d",
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
      tm.tm_hour, tm.tm_min, tm.tm_sec);
  return (written > 0 && static_cast<size_t>(written) < outLen)
             ? static_cast<size_t>(written)
             : 0;
}

}  // namespace local_time
