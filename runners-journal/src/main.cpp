#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "config.h"
#include "dashboard_data.h"
#include "dashboard_fetch.h"
#include "dashboard_parse.h"
#include "dashboard_render.h"
#include "board_pins.h"
#include "epaper_setup.h"
#include "hardware.h"
#include "panel_traits.h"
#include "panel_watchdog.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "sd_card.h"
#include "secrets.h"
#include "wake_report.h"
#include "wifi_sta.h"

TimestampedLogger appLog(Serial1);

EPaper epaper;
dashboard_render::SmoothFont smoothFont(epaper);

namespace {

bool panelStarted = false;
bool sdReady = false;

void beginPanel() {
  if (panelStarted) return;
#if RETERMINAL_MODEL == 1005
  if (sdReady) {
    SD.end();
    sdReady = false;
  }
  // Sticky shares panel SPI signals with the separately powered SD slot.
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
#endif
  epaper_setup::begin(epaper);
  epaper.setRotation(config::PANEL_ROTATION);
  panelStarted = true;
}

bool mountSdForFonts() {
#if RETERMINAL_MODEL == 1005
  // The SD card shares the e-paper's SPI bus. epaper_setup::begin() must
  // have run first so the bus is configured; we then mount SD on the same
  // instance and pull CS high so it doesn't fight the panel controller.
  beginPanel();
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  const bool ok = sd_card::mount(epaper.getSPIinstance(), "/runners-journal");
  if (!ok) {
    LOG.println("[sd] mount failed; smooth fonts unavailable, using GFX fallback");
    peripheral_power::disableSd();
  } else {
    sdReady = true;
  }
  return ok;
#else
  return false;
#endif
}

void refreshPanel() {
  panel_watchdog::refresh(epaper);
}

void sleep(uint32_t seconds) {
  if (seconds == 0 || seconds > 24ULL * 60ULL * 60ULL) {
    seconds = config::FALLBACK_SLEEP_SECONDS;
  }
  LOG.printf("[sleep] %u s\n", seconds);
  if (sdReady) {
    SD.end();
    sdReady = false;
  }
  if (panelStarted) {
    peripheral_power::disableSd();
    peripheral_power::disable();
  }
  power_latch::holdDuringDeepSleep();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  esp_deep_sleep_start();
}

void renderStatusThenSleep(const String& title, const String& detail,
                           uint32_t seconds) {
  beginPanel();
  dashboard_render::renderStatus(epaper, smoothFont, title, detail);
  refreshPanel();
  sleep(seconds);
}

}  // namespace

void setup() {
  power_latch::holdOn();
  hardware::setStatusLed(true);

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const uint64_t wakePins =
      (cause == ESP_SLEEP_WAKEUP_EXT1) ? esp_sleep_get_ext1_wakeup_status() : 0;

  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  wake_report::logWakeEvent(cause, wakePins, true);

  // Mount SD early so the smooth font is available for the dashboard.
  // The dashboard renders Norwegian text (søndag, løp) via sans_bold_*.vlw.
  // When SD is unavailable the renderer falls back to ASCII GFX fonts.
  mountSdForFonts();

  String wifiFailure;
  const wifi_sta::ConnectResult wifiResult = wifi_sta::connectStation(
      WIFI_SSID, WIFI_PASSWORD, config::WIFI_CONNECT_TIMEOUT_MS, &wifiFailure);
  if (!wifiResult.connected) {
    LOG.printf("[wifi] connect failed: %s\n", wifiFailure.c_str());
    renderStatusThenSleep("Ingen WiFi", wifiFailure,
                          config::FALLBACK_SLEEP_SECONDS);
    return;
  }
  LOG.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());

  String body;
  String fetchFailure;
  if (!dashboard_fetch::fetch(body, fetchFailure)) {
    LOG.printf("[fetch] failed: %s\n", fetchFailure.c_str());
    wifi_sta::disable();
    renderStatusThenSleep("Henting feilet", fetchFailure,
                          config::FALLBACK_SLEEP_SECONDS);
    return;
  }
  LOG.printf("[fetch] %u bytes\n", body.length());

  dashboard::DashboardData data;
  if (!dashboard::parse(body, data)) {
    LOG.println("[parse] failed");
    wifi_sta::disable();
    renderStatusThenSleep("Parsing feilet", "Ugyldig dashboard-svar",
                          config::FALLBACK_SLEEP_SECONDS);
    return;
  }
  LOG.printf("[parse] uke=%s total_km=%.1f maal_pct=%d neste_s=%u\n",
             data.uke.merkelapp.c_str(), data.uke.total_km, data.uke.maal_pct,
             data.neste_oppvakning_s);
  LOG.printf("[parse] siste_lop=%u journal=%u\n",
             static_cast<unsigned>(data.siste_lop.size()),
             static_cast<unsigned>(data.journal.size()));

  wifi_sta::disable();

  beginPanel();
  dashboard_render::renderDashboard(epaper, smoothFont, data);
  refreshPanel();

  sleep(data.neste_oppvakning_s);
}

void loop() {
}
