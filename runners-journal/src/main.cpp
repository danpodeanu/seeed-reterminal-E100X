#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "config.h"
#include "dashboard_data.h"
#include "dashboard_fetch.h"
#include "dashboard_parse.h"
#include "secrets.h"
#include "wake_report.h"
#include "wifi_sta.h"

TimestampedLogger appLog(Serial1);

namespace {

void sleep(uint32_t seconds) {
  if (seconds == 0 || seconds > 24ULL * 60ULL * 60ULL) {
    seconds = config::FALLBACK_SLEEP_SECONDS;
  }
  LOG.printf("[sleep] %u s\n", seconds);
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const uint64_t wakePins =
      (cause == ESP_SLEEP_WAKEUP_EXT1) ? esp_sleep_get_ext1_wakeup_status() : 0;

  LOG.begin(115200);
  wake_report::logWakeEvent(cause, wakePins, true);

  String wifiFailure;
  const wifi_sta::ConnectResult wifiResult = wifi_sta::connectStation(
      WIFI_SSID, WIFI_PASSWORD, config::WIFI_CONNECT_TIMEOUT_MS, &wifiFailure);
  if (!wifiResult.connected) {
    LOG.printf("[wifi] connect failed: %s\n", wifiFailure.c_str());
    sleep(config::FALLBACK_SLEEP_SECONDS);
    return;
  }
  LOG.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());

  String body;
  String fetchFailure;
  if (!dashboard_fetch::fetch(body, fetchFailure)) {
    LOG.printf("[fetch] failed: %s\n", fetchFailure.c_str());
    wifi_sta::disable();
    sleep(config::FALLBACK_SLEEP_SECONDS);
    return;
  }
  LOG.printf("[fetch] %u bytes\n", body.length());

  dashboard::DashboardData data;
  if (!dashboard::parse(body, data)) {
    LOG.println("[parse] failed");
    wifi_sta::disable();
    sleep(config::FALLBACK_SLEEP_SECONDS);
    return;
  }
  LOG.printf("[parse] uke=%s total_km=%.1f maal_pct=%d neste_s=%u\n",
             data.uke.merkelapp.c_str(), data.uke.total_km, data.uke.maal_pct,
             data.neste_oppvakning_s);
  LOG.printf("[parse] siste_lop=%u journal=%u\n",
             static_cast<unsigned>(data.siste_lop.size()),
             static_cast<unsigned>(data.journal.size()));

  wifi_sta::disable();
  sleep(data.neste_oppvakning_s);
}

void loop() {
}
