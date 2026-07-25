#include "wifi_sta.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sntp.h>
#include <lwip/opt.h>

#include "app_logger.h"

namespace wifi_sta {

String stationMacAddress() {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return "unavailable";
  char formatted[18] = {};
  snprintf(formatted, sizeof(formatted),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(formatted);
}

void disable() {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  LOG.println("[wifi] powered down");
}

bool connectStation(const char* ssid, const char* password,
                    uint32_t timeoutMs,
                    String* failureReason,
                    ShouldAbortFn shouldAbort) {
  if (failureReason) *failureReason = "";
  if (strcmp(ssid, "YOUR_WIFI_NAME") == 0) {
    LOG.println("[wifi] edit include/secrets.h first");
    if (failureReason) *failureReason = "Wi-Fi is not configured";
    return false;
  }
  WiFi.persistent(false);
  WiFi.setSleep(true);
  WiFi.mode(WIFI_STA);
#if LWIP_DHCP_GET_NTP_SRV
  // DHCP option 42 must be enabled before the station acquires its
  // lease. Clear any server names left by an earlier attempt so
  // synchronizeClock() can tell whether DHCP supplied one.
  if (esp_sntp_enabled()) esp_sntp_stop();
  for (uint8_t index = 0; index < SNTP_MAX_SERVERS; ++index)
    esp_sntp_setservername(index, nullptr);
  esp_sntp_servermode_dhcp(true);
#endif
  WiFi.begin(ssid, password);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < timeoutMs) {
    if (shouldAbort && shouldAbort()) {
      LOG.println("[wifi] connection cancelled");
      if (failureReason) *failureReason = "Wi-Fi connection cancelled";
      return false;
    }
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG.println("[wifi] connection timed out");
    if (failureReason) *failureReason = "Wi-Fi connection timed out";
    return false;
  }
  LOG.printf("[wifi] connected, IP=%s RSSI=%d\n",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

}  // namespace wifi_sta
