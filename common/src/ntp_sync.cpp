#include "ntp_sync.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <lwip/ip_addr.h>
#include <sys/time.h>
#include <time.h>

#include "app_logger.h"
#include "rtc_sync.h"

namespace ntp {
namespace {

volatile bool syncCompleted = false;

// Set by synchronizeAndPersist so the shared onSynced callback can find
// the caller's storage from a plain (non-capturing) function pointer.
time_t* g_lastSyncOut = nullptr;

void onTimeSync(struct timeval*) { syncCompleted = true; }

void persistSyncedTime(time_t now) {
  if (g_lastSyncOut != nullptr) *g_lastSyncOut = now;
  rtc_sync::saveTime(now);
}

bool waitForSync(uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (!syncCompleted && millis() - started < timeoutMs) delay(50);
  return syncCompleted;
}

bool startDhcpIfAvailable(uint32_t timeoutMs) {
#if LWIP_DHCP_GET_NTP_SRV
  const ip_addr_t* server = esp_sntp_getserver(0);
  if (server == nullptr || ip_addr_isany(server)) return false;
  char address[48] = {};
  ipaddr_ntoa_r(server, address, sizeof(address));
  LOG.printf("[ntp] starting SNTP with DHCP server %s\n", address);
  syncCompleted = false;
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_init();
  if (waitForSync(timeoutMs)) return true;
  LOG.printf(
      "[ntp] DHCP server %s timed out after %lu ms; trying configured servers\n",
      address, static_cast<unsigned long>(timeoutMs));
#else
  (void)timeoutMs;
#endif
  return false;
}

}  // namespace

bool synchronizeClock(const char* timezone, const char* primary,
                      const char* secondary, uint32_t dhcpTimeoutMs,
                      uint32_t syncTimeoutMs, OnSyncedFn onSynced) {
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
  // Report which server (or IP) fed us. Slot 0 holds the DHCP-supplied
  // server when that path succeeded; when we fell through to
  // configTzTime, slot 0 holds the primary hostname and its IP is any.
  char sourceLabel[64] = {};
  const char* servername = esp_sntp_getservername(0);
  if (servername != nullptr && servername[0] != '\0') {
    snprintf(sourceLabel, sizeof(sourceLabel), "%s", servername);
  } else {
    const ip_addr_t* addr = esp_sntp_getserver(0);
    if (addr != nullptr && !ip_addr_isany(addr)) {
      ipaddr_ntoa_r(addr, sourceLabel, sizeof(sourceLabel));
    }
  }
  if (sourceLabel[0] == '\0' && primary != nullptr && primary[0] != '\0') {
    snprintf(sourceLabel, sizeof(sourceLabel), "%s", primary);
  }
  if (sourceLabel[0] != '\0') {
    LOG.printf("[ntp] synchronized (%s): %s\n", sourceLabel, formatted);
  } else {
    LOG.printf("[ntp] synchronized: %s\n", formatted);
  }
  if (onSynced != nullptr) onSynced(now);
  return true;
}

bool synchronizeAndPersist(const char* timezone, const char* primary,
                           const char* secondary, uint32_t dhcpTimeoutMs,
                           uint32_t syncTimeoutMs, time_t* lastSyncOut) {
  g_lastSyncOut = lastSyncOut;
  const bool ok =
      synchronizeClock(timezone, primary, secondary, dhcpTimeoutMs,
                       syncTimeoutMs, persistSyncedTime);
  g_lastSyncOut = nullptr;
  return ok;
}

}  // namespace ntp
