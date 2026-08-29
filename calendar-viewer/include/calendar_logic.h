#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <string>

#include "calendar_data.h"
#include "calendar_options.h"
#include "local_time.h"

namespace calendar_logic {

inline bool hasConfiguredCalendarProvider(
    config::CalendarProvider provider, const char* icalUrl,
    bool googleCredentialsConfigured) {
  if (provider == config::CalendarProvider::Google) {
    return googleCredentialsConfigured;
  }
  return icalUrl != nullptr && icalUrl[0] != '\0';
}

inline time_t localMidnight(time_t value) {
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return 0;
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return mktime(&local);
}

inline time_t addLocalDays(time_t value, int days) {
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return 0;
  local.tm_mday += days;
  local.tm_isdst = -1;
  return mktime(&local);
}

inline time_t startOfWeek(time_t value, config::WeekStart firstDay) {
  const time_t midnight = localMidnight(value);
  struct tm local = {};
  if (midnight == 0 || localtime_r(&midnight, &local) == nullptr) return 0;
  const int first = firstDay == config::WeekStart::Sunday ? 0 : 1;
  const int daysSinceFirst = (local.tm_wday - first + 7) % 7;
  return addLocalDays(midnight, -daysSinceFirst);
}

inline time_t startOfMonth(time_t value) {
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return 0;
  local.tm_mday = 1;
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  return mktime(&local);
}

inline time_t addLocalMonths(time_t value, int months) {
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return 0;
  local.tm_mon += months;
  local.tm_isdst = -1;
  return mktime(&local);
}

inline calendar::Window displayWindow(config::CalendarView view, time_t now,
                                      config::WeekStart firstDay) {
  calendar::Window result;
  if (view == config::CalendarView::Today) {
    result.start = localMidnight(now);
    result.end = addLocalDays(result.start, 1);
  } else if (view == config::CalendarView::Week) {
    result.start = startOfWeek(now, firstDay);
    result.end = addLocalDays(result.start, 7);
  } else {
    const time_t monthStart = startOfMonth(now);
    result.start = startOfWeek(monthStart, firstDay);
    result.end = addLocalDays(result.start, 42);
  }
  return result;
}

inline bool overlaps(time_t start, time_t end,
                     time_t windowStart, time_t windowEnd) {
  return start < windowEnd && end > windowStart;
}

inline bool sameLocalDate(time_t left, time_t right) {
  struct tm a = {};
  struct tm b = {};
  return localtime_r(&left, &a) != nullptr &&
         localtime_r(&right, &b) != nullptr &&
         a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
}

inline uint32_t parseRgb(const char* value, uint32_t fallback = 0x4A6FA5) {
  if (value == nullptr) return fallback;
  if (*value == '#') ++value;
  if (strlen(value) != 6) return fallback;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 16);
  return end != nullptr && *end == '\0'
             ? static_cast<uint32_t>(parsed)
             : fallback;
}

inline uint8_t red(uint32_t rgb) {
  return static_cast<uint8_t>((rgb >> 16) & 0xFF);
}
inline uint8_t green(uint32_t rgb) {
  return static_cast<uint8_t>((rgb >> 8) & 0xFF);
}
inline uint8_t blue(uint32_t rgb) {
  return static_cast<uint8_t>(rgb & 0xFF);
}
inline uint8_t luminance(uint32_t rgb) {
  return static_cast<uint8_t>(
      (red(rgb) * 54U + green(rgb) * 183U + blue(rgb) * 19U) / 256U);
}

class Fingerprint {
 public:
  void add(const void* data, size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
      value_ ^= bytes[i];
      value_ *= UINT64_C(1099511628211);
    }
  }

  void add(const std::string& value) {
    add(value.data(), value.size());
    const uint8_t separator = 0;
    add(&separator, 1);
  }

  template <typename T>
  void addValue(const T& value) {
    add(&value, sizeof(value));
  }

  uint64_t value() const { return value_; }

 private:
  uint64_t value_ = UINT64_C(14695981039346656037);
};

inline uint64_t dataFingerprint(const calendar::Data& data,
                                const calendar::Window& window) {
  Fingerprint hash;
  hash.addValue(window.start);
  hash.addValue(window.end);
  for (const auto& source : data.sources) {
    hash.add(source.id);
    hash.add(source.name);
    hash.addValue(source.colorRgb);
  }
  for (const auto& event : data.events) {
    if (!overlaps(event.start, event.end, window.start, window.end)) continue;
    hash.add(event.uid);
    hash.add(event.title);
    hash.add(event.location);
    hash.addValue(event.start);
    hash.addValue(event.end);
    hash.addValue(event.allDay);
    hash.addValue(event.colorRgb);
    hash.addValue(event.sourceIndex);
  }
  return hash.value();
}

}  // namespace calendar_logic
