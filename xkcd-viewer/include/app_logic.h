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

constexpr int64_t normalizeRefreshBaseline(bool resetInterval,
                                           bool clockValid, int64_t now,
                                           int64_t lastRefresh) {
  return clockValid &&
                 (resetInterval || lastRefresh <= 0 || now < lastRefresh)
             ? now
             : lastRefresh;
}

constexpr uint32_t publishedComicCount(int latestNumber) {
  if (latestNumber <= 0) return 0;
  // XKCD deliberately has no comic #404.
  return static_cast<uint32_t>(
      latestNumber > 404 ? latestNumber - 1 : latestNumber);
}

constexpr bool cacheOnly(bool sdReady, uint32_t cachedComics,
                         uint32_t minimumComics) {
  return sdReady && cachedComics >= minimumComics;
}

constexpr bool networkPlanned(bool cacheOnlyMode, bool ntpDue) {
  return !cacheOnlyMode || ntpDue;
}

constexpr bool archiveMaintenanceDue(bool sdReady, bool timerWake,
                                     bool refreshIsDue) {
  return sdReady && timerWake && refreshIsDue;
}

// Should the pre-clock-sync path short-sleep until end-of-quiet instead of
// proceeding to display and maintenance work?
//
// The wake proceeds when it is a cold boot, an NTP-refresh wake, a button
// wake, when the retained clock is not valid, when quiet hours are not
// active, or when archive maintenance is due (which is allowed to run
// silently during quiet hours).
constexpr bool suppressPreSyncForQuietHours(bool coldBoot, bool ntpDue,
                                            bool buttonWake, bool clockValid,
                                            bool quietActive,
                                            bool archiveDue) {
  return !coldBoot && !ntpDue && !buttonWake && clockValid && quietActive &&
         !archiveDue;
}

// Same idea for the post-clock-sync path, where an NTP-due wake has already
// been serviced and no longer forces a full refresh.
constexpr bool suppressPostSyncForQuietHours(bool coldBoot, bool buttonWake,
                                             bool clockValid, bool quietActive,
                                             bool archiveDue) {
  return !coldBoot && !buttonWake && clockValid && quietActive && !archiveDue;
}

// Are we running maintenance silently inside quiet hours? If so the caller
// must skip beeps, comic rotation, and any renderStatus/renderComic calls.
constexpr bool maintainSilentlyInQuietHours(bool quietActive, bool archiveDue) {
  return quietActive && archiveDue;
}

constexpr bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return deadlineMs != 0 &&
         static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

}  // namespace app_logic
