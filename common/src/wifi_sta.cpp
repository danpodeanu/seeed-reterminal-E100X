#include "wifi_sta.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sntp.h>
#include <lwip/opt.h>

#include "app_logger.h"

namespace wifi_sta {
namespace {

volatile uint8_t g_lastDisconnectReason = 0;

const char* statusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:     return "idle";
    case WL_NO_SSID_AVAIL:   return "network not found";
    case WL_SCAN_COMPLETED:  return "scan completed";
    case WL_CONNECTED:       return "connected";
    case WL_CONNECT_FAILED:  return "authentication or association failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    default:                 return "unknown status";
  }
}

}  // namespace

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

ConnectResult connectStation(const char* ssid, const char* password,
                             uint32_t timeoutMs,
                             String* failureReason,
                             ShouldAbortFn shouldAbort) {
  if (failureReason) *failureReason = "";
  if (!ssid || strcmp(ssid, "YOUR_WIFI_NAME") == 0) {
    LOG.println("[wifi] edit include/secrets.h first");
    if (failureReason) *failureReason = "Wi-Fi is not configured";
    return {ConnectOutcome::NotConfigured, false,
            static_cast<uint8_t>(WiFi.status()), 0};
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
  g_lastDisconnectReason = 0;
  const wifi_event_id_t disconnectEventId = WiFi.onEvent(
      [](WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
        g_lastDisconnectReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  LOG.printf("[wifi] connecting to \"%s\" (timeout=%lu ms)\n", ssid,
             static_cast<unsigned long>(timeoutMs));
  WiFi.begin(ssid, password);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < timeoutMs) {
    if (shouldAbort && shouldAbort()) {
      const uint8_t status = static_cast<uint8_t>(WiFi.status());
      const uint8_t reason = g_lastDisconnectReason;
      WiFi.removeEvent(disconnectEventId);
      LOG.println("[wifi] connection cancelled");
      if (failureReason) *failureReason = "Wi-Fi connection cancelled";
      return {ConnectOutcome::Cancelled, false, status, reason};
    }
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    const wl_status_t status = WiFi.status();
    const uint8_t reason = g_lastDisconnectReason;
    WiFi.removeEvent(disconnectEventId);
    if (reason != 0) {
      LOG.printf("[wifi] connection failed: %s (reason=%u, status=%s)\n",
                 WiFi.STA.disconnectReasonName(
                     static_cast<wifi_err_reason_t>(reason)),
                 static_cast<unsigned>(reason), statusName(status));
    } else {
      LOG.printf("[wifi] connection failed: %s (no disconnect reason received)\n",
                 statusName(status));
    }
    if (failureReason) *failureReason = "Wi-Fi connection timed out";
    return {ConnectOutcome::Failed, false, static_cast<uint8_t>(status),
            reason};
  }
  WiFi.removeEvent(disconnectEventId);
  LOG.printf("[wifi] connected, IP=%s RSSI=%d\n",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return {ConnectOutcome::Connected, true,
          static_cast<uint8_t>(WiFi.status()), 0};
}

}  // namespace wifi_sta
