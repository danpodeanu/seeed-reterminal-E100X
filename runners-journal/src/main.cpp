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

void configureButtonWake();  // forward decl — used by deepSleep()

void deepSleep(uint32_t seconds) {
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
  // Also wake on any front button press so the paging view can be
  // brought up without waiting for the next scheduled timer wake.
  configureButtonWake();
  esp_deep_sleep_start();
}

void renderStatusThenSleep(const String& title, const String& detail,
                           uint32_t seconds) {
  beginPanel();
  dashboard_render::renderStatus(epaper, smoothFont, title, detail);
  refreshPanel();
  deepSleep(seconds);
}

// Configure EXT1 wake on all three front buttons (active LOW).
// PIN_BUTTON_0 = OK/power, PIN_BUTTON_1 = UP, PIN_BUTTON_2 = DOWN.
void configureButtonWake() {
  const uint64_t wakeMask =
      (1ULL << board::PIN_BUTTON_0) |
      (1ULL << board::PIN_BUTTON_1) |
      (1ULL << board::PIN_BUTTON_2);
  for (int p : {board::PIN_BUTTON_0, board::PIN_BUTTON_1,
                board::PIN_BUTTON_2}) {
    hardware::configureWakePin(p);
  }
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
}

bool isButtonWake() {
  return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
}

// Debounce: wait for all buttons to be released after a button wake.
void waitForButtonRelease() {
  delay(50);
  while (digitalRead(board::PIN_BUTTON_0) == LOW ||
         digitalRead(board::PIN_BUTTON_1) == LOW ||
         digitalRead(board::PIN_BUTTON_2) == LOW) {
    delay(10);
  }
  delay(50);
}

// Wait for a button press with timeout. Returns the button index
// (0=OK, 1=UP, 2=DOWN) or -1 on timeout.
int waitForButton(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (digitalRead(board::PIN_BUTTON_0) == LOW) {
      delay(50);  // debounce
      return 0;
    }
    if (digitalRead(board::PIN_BUTTON_1) == LOW) {
      delay(50);
      return 1;
    }
    if (digitalRead(board::PIN_BUTTON_2) == LOW) {
      delay(50);
      return 2;
    }
    delay(20);
  }
  return -1;
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

  const bool buttonWake = isButtonWake();

  // Mount SD early so the smooth font is available for the dashboard.
  // The dashboard renders Norwegian text (søndag, løp) via sans_bold_*.vlw.
  // When SD is unavailable the renderer falls back to ASCII GFX fonts.
  mountSdForFonts();

  // --- Fetch dashboard data (always, even on button wake, so paging ---
  // --- shows fresh data rather than a stale cached frame).           ---
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
  LOG.printf("[parse] aar=%.1f uke_elev=%d uke_tid=%s siste_lop=%u journal=%u\n",
             data.aar.total_km, data.uke.elevation_m,
             data.uke.total_tid.c_str(),
             static_cast<unsigned>(data.siste_lop.size()),
             static_cast<unsigned>(data.journal.size()));

  wifi_sta::disable();

  beginPanel();

  if (buttonWake) {
    // Button wake: show selector/paging. Start on Uke screen.
    // Wait for the buttons that triggered the wake to be released.
    waitForButtonRelease();

    dashboard_render::Screen current = dashboard_render::Screen::Uke;
    dashboard_render::renderScreen(epaper, smoothFont, current, data);
    refreshPanel();
    LOG.printf("[paging] screen=%d\n", static_cast<int>(current));

    // Paging loop: UP/DOWN navigates, OK (button 0) sleeps.
    // Timeout after 5 minutes of inactivity -> deep sleep.
    while (true) {
      const int btn = waitForButton(config::PAGING_TIMEOUT_MS);
      if (btn < 0) {
        LOG.println("[paging] timeout, sleeping");
        break;
      }
      if (btn == 0) {
        // OK / power button -> sleep.
        LOG.println("[paging] OK pressed, sleeping");
        break;
      }
      int next = static_cast<int>(current);
      if (btn == 1) {
        // UP -> previous screen (wrap).
        next = (next + dashboard_render::SCREEN_COUNT - 1) %
               dashboard_render::SCREEN_COUNT;
      } else if (btn == 2) {
        // DOWN -> next screen (wrap).
        next = (next + 1) % dashboard_render::SCREEN_COUNT;
      }
      current = static_cast<dashboard_render::Screen>(next);
      dashboard_render::renderScreen(epaper, smoothFont, current, data);
      refreshPanel();
      LOG.printf("[paging] screen=%d\n", static_cast<int>(current));
      // Debounce: let the pressed button release before polling again.
      waitForButtonRelease();
    }
  } else {
    // Timer wake (or cold boot): render Uke screen (default), then sleep.
    dashboard_render::renderScreen(epaper, smoothFont,
                                   dashboard_render::Screen::Uke, data);
    refreshPanel();
  }

  // Enter deep sleep. Configure both timer + button wake so a button
  // press during the timer-sleep interval brings up the paging view.
  deepSleep(data.neste_oppvakning_s);
}

void loop() {
}
