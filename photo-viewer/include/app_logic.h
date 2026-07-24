#pragma once

#include <stdint.h>

namespace app_logic {

constexpr int SECONDS_PER_DAY = 24 * 60 * 60;

constexpr bool startupBeepRequired(bool coldBoot, bool buttonWake) {
  return coldBoot || buttonWake;
}

constexpr int secondsOfDay(int hour, int minute, int second) {
  return hour * 3600 + minute * 60 + second;
}

constexpr bool quietHoursActive(bool enabled, int nowSecond,
                                int startSecond, int endSecond) {
  if (!enabled) return false;
  if (startSecond == endSecond) return true;
  if (startSecond < endSecond) {
    return nowSecond >= startSecond && nowSecond < endSecond;
  }
  return nowSecond >= startSecond || nowSecond < endSecond;
}

constexpr uint64_t secondsUntilTimeOfDay(int targetSecond, int nowSecond) {
  const int delta = targetSecond - nowSecond;
  return static_cast<uint64_t>(
      delta <= 0 ? delta + SECONDS_PER_DAY : delta);
}

constexpr bool nextWakeFallsInQuietHours(bool enabled, int nowSecond,
                                         int startSecond, int endSecond,
                                         uint64_t sleepSeconds) {
  return enabled &&
         !quietHoursActive(enabled, nowSecond, startSecond, endSecond) &&
         secondsUntilTimeOfDay(startSecond, nowSecond) <= sleepSeconds;
}

constexpr bool refreshDue(bool coldBoot, bool clockValid, int64_t now,
                          int64_t lastRefresh, uint32_t intervalSeconds) {
  return coldBoot || !clockValid || lastRefresh <= 0 || now < lastRefresh ||
         static_cast<uint64_t>(now - lastRefresh) >= intervalSeconds;
}

constexpr int photoDirection(bool previousButtonWake) {
  return previousButtonWake ? -1 : 1;
}

constexpr int32_t normalizePhotoIndex(int32_t index, uint32_t count) {
  if (count == 0) return -1;
  const int32_t signedCount = static_cast<int32_t>(count);
  const int32_t remainder = index % signedCount;
  return remainder < 0 ? remainder + signedCount : remainder;
}

}  // namespace app_logic
