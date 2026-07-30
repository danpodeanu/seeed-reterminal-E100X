// SD-card Wi-Fi portal for the reTerminal E100X.
//
// Boot flow:
//   1. Bring up epaper + SPI, check for SD card, mount it.
//   2. If no card: draw an error screen and stop (deep sleep so we
//      don't chew battery running an AP nobody can use).
//   3. Otherwise: start a soft-AP + web server, render the two QR
//      codes (Wi-Fi + URL) on the panel, and loop() the HTTP server.
//
// Everything portal-specific lives in the `sd_web_portal` library
// (common-sd-web/), so an app that wants to bolt this on later can do
// so by adding the same library dependency and calling
// `sd_web_portal::begin()` from its own setup.

#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "board_pins.h"
#include "driver.h"
#include "epaper_setup.h"
#include "hardware.h"
#include "sd_card.h"
#include "sd_web_portal.h"
#include "sd_web_portal_ui.h"
#include "wifi_sta.h"

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check common/include/driver.h"
#endif

TimestampedLogger appLog(Serial1);

EPaper epaper;

namespace {

#if RETERMINAL_MODEL == 1001
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr const char* PANEL_LABEL = "reTerminal E1001";
#elif RETERMINAL_MODEL == 1002
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr const char* PANEL_LABEL = "reTerminal E1002";
#elif RETERMINAL_MODEL == 1003
constexpr int PANEL_WIDTH = 1872;
constexpr int PANEL_HEIGHT = 1404;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr const char* PANEL_LABEL = "reTerminal E1003";
#elif RETERMINAL_MODEL == 1004
constexpr int PANEL_WIDTH = 1200;
constexpr int PANEL_HEIGHT = 1600;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr const char* PANEL_LABEL = "reTerminal E1004";
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, or 1004"
#endif

constexpr const char* kCacheDir = "/portal";

// Return true when the SD card detect switch reports a card present.
// Active-low: PIN_SD_DETECT is pulled up; a card inserted shorts it to
// ground. Called before we try SD.begin() so a missing card produces a
// clear on-screen error rather than a mysterious mount failure.
bool sdCardInserted() {
  pinMode(board::PIN_SD_DETECT, INPUT_PULLUP);
  // Give the pull-up a moment to settle after the pinMode() call, so a
  // brief bounce from just-inserted cards doesn't misread as absent.
  delayMicroseconds(50);
  return digitalRead(board::PIN_SD_DETECT) == LOW;
}

void renderNoSdCardAndStop() {
  epaper.fillSprite(PANEL_WHITE);
  sd_web_portal::ui::renderErrorScreen<EPaper>(
      epaper, PANEL_WIDTH, PANEL_HEIGHT, PANEL_BLACK, PANEL_WHITE,
      "No SD card",
      "Insert a FAT32 / exFAT card and press reset",
      PANEL_LABEL);
  epaper.update();
  LOG.println("[sd-web] no SD card detected; halting");
  LOG.flush();
  // Deep sleep with the reset button (GPIO 3) as wake source, matching
  // panel-test's convention. The reTerminal has no dedicated EN button.
  constexpr int kButtons[] = {3, 4, 5};
  for (const int pin : kButtons) {
    hardware::configureWakePin(pin);
  }
  constexpr uint64_t kWakeMask =
      (1ULL << 3) | (1ULL << 4) | (1ULL << 5);
  esp_sleep_enable_ext1_wakeup(kWakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  delay(50);
  esp_deep_sleep_start();
}

void renderPortalScreen() {
  const String ssid = sd_web_portal::currentSsid();
  const IPAddress ip = sd_web_portal::currentIp();
  const uint16_t port = sd_web_portal::currentPort();
  const String url = sd_web_portal::urlQrPayload(ip, port);
  const String wifiPayload = sd_web_portal::wifiQrPayload(ssid, nullptr);

  sd_web_portal::ui::RenderInfo info;
  info.modelLabel = PANEL_LABEL;
  info.ssid = ssid;
  info.url = url;
  info.macAddress = wifi_sta::stationMacAddress();
  info.wifiPayload = wifiPayload;
  info.urlPayload = url;

  sd_web_portal::ui::renderPortalScreen<EPaper>(
      epaper, PANEL_WIDTH, PANEL_HEIGHT, PANEL_BLACK, PANEL_WHITE, info);
  epaper.update();
}

}  // namespace

void setup() {
  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  delay(50);
  LOG.println();
  LOG.printf("[sd-web] booting on %s\n", PANEL_LABEL);

  hardware::beep();

  epaper.begin();
  epaper_setup::finalize(epaper.getSPIinstance());
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  // E1002 / E1004 six-colour panels expose their palette directly and
  // don't have an initGrayMode method - fall through with default mode.

  // Cheap physical check before we hit SPI: no card in the socket ->
  // draw the error screen and stop. The mount routine below has its
  // own retry loop for SPI hiccups, but there's no point retrying if
  // the detect switch says nothing is there.
  if (!sdCardInserted()) {
    LOG.println("[sd-web] card detect: no card");
    renderNoSdCardAndStop();
    return;  // unreachable - deep sleep
  }

  if (!sd_card::mount(epaper.getSPIinstance(), kCacheDir)) {
    LOG.println("[sd-web] SD mount failed");
    renderNoSdCardAndStop();
    return;
  }

  sd_web_portal::Config cfg;
  cfg.apSsidPrefix = "ReTerminal ";
  cfg.apPassword = nullptr;  // open network - QR spec is nopass
  cfg.apIp = IPAddress(192, 168, 1, 1);
  cfg.apGateway = IPAddress(192, 168, 1, 1);
  cfg.apNetmask = IPAddress(255, 255, 255, 0);
  cfg.httpPort = 80;
  cfg.maxConnections = 4;

  if (!sd_web_portal::begin(cfg)) {
    epaper.fillSprite(PANEL_WHITE);
    sd_web_portal::ui::renderErrorScreen<EPaper>(
        epaper, PANEL_WIDTH, PANEL_HEIGHT, PANEL_BLACK, PANEL_WHITE,
        "Wi-Fi start failed",
        "Reset the device and try again",
        PANEL_LABEL);
    epaper.update();
    LOG.println("[sd-web] portal begin failed");
    delay(1000);
    esp_deep_sleep_start();
    return;
  }

  renderPortalScreen();
  hardware::beep();
  LOG.println("[sd-web] ready; serving HTTP");
}

void loop() {
  sd_web_portal::loop();
  // Small yield so the SoftAP + LWIP tasks make progress even when no
  // HTTP client is talking to us. WebServer::handleClient() returns
  // immediately when there's nothing to do, so we'd otherwise spin.
  delay(2);
}
