#pragma once

#include <stdint.h>

#include "app_logic_core.h"

namespace app_logic {

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

// After the initial acquireComic pass, should we attempt one live
// recovery (bring Wi-Fi up and try to download a comic)? Fires whenever
// SD is present, we already tried once and failed, and the radio isn't
// already on. This includes cache-only mode: if the local pool is
// unreadable (bad card, corrupt manifest, dangling pointers, ...) we'd
// otherwise render the "no usable" status forever, so trading one
// battery-costly wake for a real comic is the better failure mode.
constexpr bool liveRecoveryAllowed(bool sdReady, bool alreadyAcquired,
                                   bool networkAlreadyUp) {
  return sdReady && !alreadyAcquired && !networkAlreadyUp;
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
