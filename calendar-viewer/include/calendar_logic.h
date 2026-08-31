#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "calendar_data.h"
#include "calendar_options.h"
#include "local_time.h"

namespace calendar_logic {

enum class GoogleAuthFailure {
  None,
  Transport,
  ScopeOrAuthorization,
  Other,
};

enum class PrimaryButtonAction {
  None,
  Refresh,
  Portal,
  Screenshot,
};

// FrameComponents is persisted as an NVS blob; bump this when its layout or
// comparison semantics change.
inline constexpr uint32_t kFrameComponentsVersion = 4;
inline constexpr float kIndoorTemperatureRefreshThresholdC = 1.0f;
inline constexpr float kIndoorHumidityRefreshThresholdPct = 5.0f;
inline constexpr int kBatteryRefreshThresholdPct = 5;

struct FrameComponents {
  uint32_t version = kFrameComponentsVersion;
  int32_t batteryPct = -1;
  uint64_t renderer = 0;
  uint64_t calendar = 0;
  uint64_t presentation = 0;
  uint64_t date = 0;
  uint64_t weather = 0;
  float indoorTemperatureC = 0.0f;
  float indoorHumidityPct = 0.0f;
  uint32_t indoorClimateValid = 0;
  uint32_t batteryValid = 0;
  uint32_t externalPowerValid = 0;
  uint32_t externalPower = 0;
};
static_assert(sizeof(FrameComponents) == 72,
              "FrameComponents NVS layout changed without a version bump");

enum class FrameComponentChange : uint16_t {
  Renderer = 1U << 0,
  Calendar = 1U << 1,
  Presentation = 1U << 2,
  Date = 1U << 3,
  IndoorClimate = 1U << 4,
  Power = 1U << 5,
  Weather = 1U << 6,
};

inline constexpr uint16_t frameComponentBit(FrameComponentChange component) {
  return static_cast<uint16_t>(component);
}

inline constexpr uint16_t kAllFrameComponentChanges =
    frameComponentBit(FrameComponentChange::Renderer) |
    frameComponentBit(FrameComponentChange::Calendar) |
    frameComponentBit(FrameComponentChange::Presentation) |
    frameComponentBit(FrameComponentChange::Date) |
    frameComponentBit(FrameComponentChange::IndoorClimate) |
    frameComponentBit(FrameComponentChange::Power) |
    frameComponentBit(FrameComponentChange::Weather);

inline bool frameComponentsCompatible(const FrameComponents& components) {
  return components.version == kFrameComponentsVersion;
}

inline bool hasValidIndoorClimate(const FrameComponents& components) {
  return components.indoorClimateValid != 0 &&
         std::isfinite(components.indoorTemperatureC) &&
         std::isfinite(components.indoorHumidityPct);
}

inline bool indoorClimateChanged(const FrameComponents& previous,
                                 const FrameComponents& current) {
  const bool previousValid = hasValidIndoorClimate(previous);
  const bool currentValid = hasValidIndoorClimate(current);
  if (previousValid != currentValid) return true;
  if (!currentValid) return false;

  return std::fabs(current.indoorTemperatureC -
                   previous.indoorTemperatureC) >=
             kIndoorTemperatureRefreshThresholdC ||
         std::fabs(current.indoorHumidityPct -
                   previous.indoorHumidityPct) >=
             kIndoorHumidityRefreshThresholdPct;
}

inline constexpr bool hasValidBattery(const FrameComponents& components) {
  return components.batteryValid != 0 && components.batteryPct >= 0 &&
         components.batteryPct <= 100;
}

inline constexpr bool batteryPercentageChanged(
    const FrameComponents& previous, const FrameComponents& current) {
  const bool previousValid = hasValidBattery(previous);
  const bool currentValid = hasValidBattery(current);
  if (previousValid != currentValid) return true;
  if (!currentValid) return false;

  const int delta = current.batteryPct - previous.batteryPct;
  return delta <= -kBatteryRefreshThresholdPct ||
         delta >= kBatteryRefreshThresholdPct;
}

inline constexpr bool externalPowerChanged(
    const FrameComponents& previous, const FrameComponents& current) {
  const bool previousValid = previous.externalPowerValid != 0;
  const bool currentValid = current.externalPowerValid != 0;
  if (previousValid != currentValid) return true;
  if (!currentValid) return false;
  return (previous.externalPower != 0) != (current.externalPower != 0);
}

inline constexpr bool powerChanged(const FrameComponents& previous,
                                   const FrameComponents& current) {
  return batteryPercentageChanged(previous, current) ||
         externalPowerChanged(previous, current);
}

inline uint16_t changedFrameComponents(const FrameComponents& previous,
                                       const FrameComponents& current) {
  if (!frameComponentsCompatible(previous) ||
      !frameComponentsCompatible(current)) {
    return kAllFrameComponentChanges;
  }

  uint16_t changes = 0;
  if (previous.renderer != current.renderer) {
    changes |= frameComponentBit(FrameComponentChange::Renderer);
  }
  if (previous.calendar != current.calendar) {
    changes |= frameComponentBit(FrameComponentChange::Calendar);
  }
  if (previous.presentation != current.presentation) {
    changes |= frameComponentBit(FrameComponentChange::Presentation);
  }
  if (previous.date != current.date) {
    changes |= frameComponentBit(FrameComponentChange::Date);
  }
  if (indoorClimateChanged(previous, current)) {
    changes |= frameComponentBit(FrameComponentChange::IndoorClimate);
  }
  if (powerChanged(previous, current)) {
    changes |= frameComponentBit(FrameComponentChange::Power);
  }
  if (previous.weather != current.weather) {
    changes |= frameComponentBit(FrameComponentChange::Weather);
  }
  return changes;
}

inline bool frameComponentChanged(uint16_t changes,
                                  FrameComponentChange component) {
  return (changes & frameComponentBit(component)) != 0;
}

inline constexpr uint32_t kConfigPortalHoldMs = 2000;
inline constexpr uint32_t kScreenshotHoldMs = 5000;

inline constexpr bool validCalendarView(config::CalendarView view) {
  return view == config::CalendarView::Today ||
         view == config::CalendarView::Week ||
         view == config::CalendarView::Month;
}

inline constexpr config::CalendarView calendarViewForButtons(
    bool todayPressed, bool weekPressed, bool monthPressed,
    config::CalendarView retained) {
  if (todayPressed) return config::CalendarView::Today;
  if (weekPressed) return config::CalendarView::Week;
  if (monthPressed) return config::CalendarView::Month;
  return validCalendarView(retained) ? retained : config::CalendarView::Today;
}

inline constexpr const char* calendarViewName(config::CalendarView view) {
  switch (view) {
    case config::CalendarView::Week:
      return "Week";
    case config::CalendarView::Month:
      return "Month";
    case config::CalendarView::Today:
    default:
      return "Today";
  }
}

inline PrimaryButtonAction classifyPrimaryButtonHold(
    uint32_t heldMilliseconds, bool screenshotEnabled) {
  if (screenshotEnabled &&
      heldMilliseconds >= kScreenshotHoldMs) {
    return PrimaryButtonAction::Screenshot;
  }
  if (heldMilliseconds >= kConfigPortalHoldMs) {
    return PrimaryButtonAction::Portal;
  }
  return PrimaryButtonAction::Refresh;
}

inline constexpr bool shouldShowInitialConnectionStatus(
    bool coldBoot, bool portalRequested) {
  return coldBoot && !portalRequested;
}

inline constexpr bool suppressPostSyncForQuietHours(
    bool coldBoot, bool buttonWake, bool quietHoursActive) {
  return !coldBoot && !buttonWake && quietHoursActive;
}

inline bool isGoogleTransportFailure(int httpStatus) {
  return httpStatus <= 0;
}

inline GoogleAuthFailure classifyGoogleAuthFailure(
    int httpStatus, std::string_view oauthError) {
  if (isGoogleTransportFailure(httpStatus)) {
    return GoogleAuthFailure::Transport;
  }
  if (oauthError == "invalid_scope" ||
      oauthError == "unauthorized_client" ||
      oauthError == "access_denied") {
    return GoogleAuthFailure::ScopeOrAuthorization;
  }
  return GoogleAuthFailure::Other;
}

inline bool shouldUseGoogleEventOnlyFallback(int httpStatus) {
  return httpStatus == 401 || httpStatus == 403;
}

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

inline calendar::Window dashboardWindow(time_t now,
                                        config::WeekStart firstDay) {
  calendar::Window result =
      displayWindow(config::CalendarView::Month, now, firstDay);
  const time_t upcomingEnd = addLocalDays(localMidnight(now), 43);
  if (result.end < upcomingEnd) result.end = upcomingEnd;
  return result;
}

inline time_t selectedDayForWake(config::CalendarView view,
                                 time_t retainedDay, time_t now,
                                 const calendar::Window& fetchedWindow,
                                 bool resetToToday) {
  const time_t today = localMidnight(now);
  if (view != config::CalendarView::Today || resetToToday) return today;

  const time_t retainedMidnight = localMidnight(retainedDay);
  if (retainedMidnight >= fetchedWindow.start &&
      retainedMidnight < fetchedWindow.end) {
    return retainedMidnight;
  }
  return today;
}

inline time_t touchSelectionWindowEnd(
    const calendar::Window& monthWindow) {
  constexpr int kLastMonthCellIndex = 41;
  constexpr int kUpcomingDayCount = 43;
  return addLocalDays(
      monthWindow.start, kLastMonthCellIndex + kUpcomingDayCount);
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

inline bool singleGoogleCalendarColor(const calendar::Data& data,
                                     uint32_t& color) {
  if (data.sources.size() != 1 ||
      !data.sources.front().googleColorAvailable) {
    return false;
  }
  color = data.sources.front().colorRgb;
  return true;
}

struct AgendaLayout {
  int visibleRows = 0;
  int rowHeight = 0;
  bool showMore = false;
};

inline AgendaLayout agendaLayout(int eventCount, int contentHeight,
                                 int preferredRowHeight,
                                 int minimumRowHeight,
                                 int moreLineHeight) {
  AgendaLayout result;
  if (eventCount <= 0 || contentHeight <= 0 || preferredRowHeight <= 0) {
    return result;
  }

  result.rowHeight = std::min(preferredRowHeight, contentHeight);
  result.visibleRows =
      std::min(eventCount, contentHeight / result.rowHeight);
  if (result.visibleRows >= eventCount || moreLineHeight <= 0) return result;

  const int availableForRows = contentHeight - moreLineHeight;
  const int compactRowHeight =
      std::min(result.rowHeight, std::max(1, minimumRowHeight));
  const int compactCapacity =
      availableForRows > 0 ? availableForRows / compactRowHeight : 0;
  const int compactRows = std::min(eventCount - 1, compactCapacity);
  if (compactRows <= 0) return result;

  result.visibleRows = compactRows;
  result.rowHeight =
      std::min(result.rowHeight, availableForRows / compactRows);
  result.showMore = true;
  return result;
}

inline std::string formatClockTime(time_t value, config::TimeFormat format,
                                  bool includeMeridiem = true) {
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return "--:--";

  char buffer[16] = {};
  if (format == config::TimeFormat::TwentyFourHour) {
    snprintf(buffer, sizeof(buffer), "%02d:%02d", local.tm_hour, local.tm_min);
    return buffer;
  }

  const int hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
  if (includeMeridiem) {
    snprintf(buffer, sizeof(buffer), "%d:%02d%s", hour, local.tm_min,
            local.tm_hour < 12 ? "am" : "pm");
  } else {
    snprintf(buffer, sizeof(buffer), "%d:%02d", hour, local.tm_min);
  }
  return buffer;
}

inline std::string formatClockRange(time_t start, time_t end,
                                   config::TimeFormat format) {
  if (format == config::TimeFormat::TwentyFourHour) {
    const std::string startText = formatClockTime(start, format);
    const std::string endText = formatClockTime(end, format);
    return startText == "--:--" || endText == "--:--"
               ? std::string("--:--")
               : startText + " - " + endText;
  }

  struct tm endLocal = {};
  const std::string startText = formatClockTime(start, format);
  if (startText == "--:--" ||
      localtime_r(&end, &endLocal) == nullptr) {
    return "--:--";
  }
  char endBuffer[16] = {};
  const int endHour =
      endLocal.tm_hour % 12 == 0 ? 12 : endLocal.tm_hour % 12;
  if (endLocal.tm_min == 0) {
    snprintf(endBuffer, sizeof(endBuffer), "%d%s", endHour,
             endLocal.tm_hour < 12 ? "am" : "pm");
  } else {
    snprintf(endBuffer, sizeof(endBuffer), "%d:%02d%s", endHour,
             endLocal.tm_min, endLocal.tm_hour < 12 ? "am" : "pm");
  }
  return startText + " - " + endBuffer;
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

inline uint64_t frameRefreshFingerprint(
    const FrameComponents& components,
    std::string_view diagnosticFooter) {
  // Diagnostic text and thresholded sensor values are intentionally excluded.
  // Their component comparators decide whether the visible change is large
  // enough to refresh.
  static_cast<void>(diagnosticFooter);
  Fingerprint hash;
  hash.addValue(components.version);
  hash.addValue(components.renderer);
  hash.addValue(components.calendar);
  hash.addValue(components.presentation);
  hash.addValue(components.date);
  hash.addValue(components.weather);
  return hash.value();
}

inline bool shouldRefreshCalendarFrame(bool havePreviousFrame,
                                       bool previousWasCalendar,
                                       uint64_t previousFingerprint,
                                       uint64_t currentFingerprint,
                                       uint16_t componentChanges) {
  return !havePreviousFrame || !previousWasCalendar ||
         previousFingerprint != currentFingerprint || componentChanges != 0;
}

inline uint64_t dataFingerprint(const calendar::Data& data,
                                const calendar::Window& window) {
  Fingerprint hash;
  hash.addValue(window.start);
  hash.addValue(window.end);
  for (const auto& source : data.sources) {
    hash.add(source.id);
    hash.add(source.name);
    hash.addValue(source.colorRgb);
    hash.addValue(source.googleColorAvailable);
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
