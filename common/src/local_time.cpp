#include "local_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "app_logic_core.h"

namespace local_time {

void configureTimezone(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

bool clockIsValid() { return time(nullptr) >= 1700000000; }

bool localClock(struct tm& out) {
  const time_t now = time(nullptr);
  return clockIsValid() && localtime_r(&now, &out) != nullptr;
}

bool refreshDue(bool coldBoot, time_t lastSync, uint32_t intervalSeconds) {
  const time_t now = time(nullptr);
  return app_logic::refreshDue(coldBoot, clockIsValid(), now, lastSync,
                               intervalSeconds);
}

bool parseIso8601Local(const String& value, time_t& timestamp) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  const int fields = sscanf(value.c_str(), "%d-%d-%dT%d:%d:%d",
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
