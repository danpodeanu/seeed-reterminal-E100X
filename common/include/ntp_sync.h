#pragma once

#include <stdint.h>
#include <time.h>

// Shared NTP synchronization helpers used by all three viewer apps. Callers
// keep their own RTC_DATA_ATTR storage for the last-sync epoch and hand it
// in via `onSynced`, so the value survives deep sleep on the caller's side.
namespace ntp {

using OnSyncedFn = void (*)(time_t now);

// Runs the full sync: DHCP-supplied server first, configured servers second.
// dhcpTimeoutMs must cover the framework's randomized SNTP startup delay in
// addition to the server response time.
// On success invokes `onSynced(now)` so the caller can update its own
// RTC_DATA_ATTR epoch and mirror the time to a hardware RTC.
bool synchronizeClock(const char* timezone, const char* primary,
                      const char* secondary, uint32_t dhcpTimeoutMs,
                      uint32_t syncTimeoutMs, OnSyncedFn onSynced);

// Convenience wrapper for the common case: run synchronizeClock, and on
// success update `*lastSyncOut` (typically an RTC_DATA_ATTR time_t in
// the caller) and mirror the new time to the hardware PCF8563 via
// rtc_sync::saveTime. Returns whatever synchronizeClock returned.
bool synchronizeAndPersist(const char* timezone, const char* primary,
                           const char* secondary, uint32_t dhcpTimeoutMs,
                           uint32_t syncTimeoutMs, time_t* lastSyncOut);

}  // namespace ntp
