#pragma once

#include <Arduino.h>
#include <esp_sntp.h>
#include <lwip/ip_addr.h>
#include <sys/time.h>
#include <time.h>

#include "app_logger.h"

// Shared NTP synchronization helpers used by all three viewer apps. Callers
// keep their own RTC_DATA_ATTR storage for the last-sync epoch and hand it in
// via the callback so the value can survive deep sleep.
namespace ntp {

inline volatile bool syncCompleted = false;

inline void onTimeSync(struct timeval*) { syncCompleted = true; }

inline bool waitForSync(uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (!syncCompleted && millis() - started < timeoutMs) delay(50);
  return syncCompleted;
}

inline bool startDhcpIfAvailable(uint32_t timeoutMs) {
#if LWIP_DHCP_GET_NTP_SRV
  const ip_addr_t* server = esp_sntp_getserver(0);
  if (server == nullptr || ip_addr_isany(server)) return false;
  char address[48] = {};
  ipaddr_ntoa_r(server, address, sizeof(address));
  LOG.printf("[ntp] trying DHCP server %s\n", address);
  syncCompleted = false;
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_init();
  if (waitForSync(timeoutMs)) return true;
  LOG.println("[ntp] DHCP server timed out; trying configured servers");
#else
  (void)timeoutMs;
#endif
  return false;
}

// Runs the full sync: DHCP-supplied server first, configured servers second.
// On success invokes `onSynced(now)` so the caller can update its own
// RTC_DATA_ATTR epoch and mirror the time to a hardware RTC.
template <typename OnSynced>
bool synchronizeClock(const char* timezone, const char* primary,
                      const char* secondary, uint32_t dhcpTimeoutMs,
                      uint32_t syncTimeoutMs, OnSynced&& onSynced) {
  syncCompleted = false;
  sntp_set_time_sync_notification_cb(onTimeSync);
  bool synchronized = startDhcpIfAvailable(dhcpTimeoutMs);
  if (!synchronized) {
#if LWIP_DHCP_GET_NTP_SRV
    const ip_addr_t* server = esp_sntp_getserver(0);
    if (server == nullptr || ip_addr_isany(server)) {
      LOG.println("[ntp] DHCP supplied no NTP server; using configured servers");
    }
#else
    LOG.println("[ntp] DHCP NTP unavailable; using configured servers");
#endif
    syncCompleted = false;
    configTzTime(timezone, primary, secondary);
    synchronized = waitForSync(syncTimeoutMs);
  }
  if (!synchronized) {
    LOG.println("[ntp] synchronization timed out; continuing");
    return false;
  }

  const time_t now = time(nullptr);
  struct tm localTime = {};
  if (now <= 0 || localtime_r(&now, &localTime) == nullptr) {
    LOG.println("[ntp] local time conversion failed");
    return false;
  }
  char formatted[40] = {};
  strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z", &localTime);
  LOG.printf("[ntp] synchronized: %s\n", formatted);
  onSynced(now);
  return true;
}

}  // namespace ntp
