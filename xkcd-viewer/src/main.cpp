#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <TFT_eSPI.h>
#include <driver/rtc_io.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp32-hal-psram.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "secrets.h"
#include "app_logic.h"
#include "app_logger.h"
#include "battery_gauge.h"
#include "ntp_sync.h"
#include "board_pins.h"
#include "version.h"
#include "hardware.h"
#include "local_time.h"
#include "wake_report.h"
#include "rtc_sync.h"
#include "wifi_sta.h"
#include "climate_sensor.h"
#include "sd_card.h"
#include "sd_ota.h"
#include "epaper_setup.h"
#include "net_http.h"
#include "log_sd_sink.h"
#include "text_render.h"
#include "xkcd_index.h"
#include "xkcd_cache_schema.h"
#include "quiet_hours.h"
#include "repo_qr.h"
#include "sensors.h"
#include "low_battery.h"
#include "dither.h"
#include "image_loader.h"
#include "pcf8563_utc.h"
#include "screenshot_png.h"
#include "smooth_font_manager.h"
#include "usb_screen_capture.h"
#include "panel_watchdog.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "timestamped_logger.h"
#include "config_portal.h"
#include "config_portal_ui.h"
#include "sd_web_portal.h"
#include "wifi_schema.h"
#include "xkcd_config_runtime.h"
#include "xkcd_config_schema.h"
#include "xkcd_wifi_credentials.h"

// HTTPS, HTTPClient and the SD filesystem have a fairly deep combined call
// chain. E1003 exposed the default 8 KiB Arduino loop stack limit when the SD
// cache path added its download buffer to that stack.
SET_LOOP_TASK_STACK_SIZE(16U * 1024U);

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check common/include/driver.h"
#endif

TimestampedLogger appLog(Serial1);
// LOG is provided by app_logger.h; the definition above lives at namespace
// scope so shared screenshot helpers can extern-link.

namespace {

using namespace ::board;
constexpr int PIN_BUTTON_GREEN = board::PIN_BUTTON_0;
constexpr int PIN_BUTTON_RIGHT = board::PIN_BUTTON_1;
constexpr int PIN_BUTTON_LEFT = board::PIN_BUTTON_2;

#if RETERMINAL_MODEL == 1005
constexpr char PRIMARY_BUTTON_LABEL[] = "OK";
constexpr char PORTAL_EXIT_HINT[] = "Press OK to return to XKCD";
#else
constexpr char PRIMARY_BUTTON_LABEL[] = "green";
constexpr char PORTAL_EXIT_HINT[] = "Press green button to return to XKCD";
#endif

#if RETERMINAL_MODEL == 1001
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_GRAY_2;
constexpr bool PANEL_STATUS_DITHERED = false;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_GRAY_2;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 0;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_GRAY_1;
constexpr DitherPalette PANEL_PALETTE = PAL_GRAY4;
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
constexpr bool PANEL_STATUS_DITHERED = true;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 4;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_DARKGREY;
constexpr DitherPalette PANEL_PALETTE = PAL_E6;
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_GRAY_13;
constexpr bool PANEL_STATUS_DITHERED = false;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_GRAY_13;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 0;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_GRAY_5;
constexpr DitherPalette PANEL_PALETTE = PAL_GRAY16;
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
constexpr bool PANEL_STATUS_DITHERED = true;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 4;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_DARKGREY;
constexpr DitherPalette PANEL_PALETTE = PAL_E6;
#elif RETERMINAL_MODEL == 1005
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
constexpr bool PANEL_STATUS_DITHERED = false;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 0;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_BLACK;
constexpr DitherPalette PANEL_PALETTE = PAL_BW;
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, 1004, or 1005"
#endif

EPaper epaper;
smooth_fonts::Manager smoothFontManager(epaper);
usb_screen_capture::Server usbScreenCapture;
Adafruit_SHT4x sht4;

bool sdReady = false;
bool sdCacheWritable = true;
bool screenshotRequested = false;
bool framebufferReady = false;
sensors::Readings sensorReadings;

#if RETERMINAL_MODEL == 1003
float panelWaveformTemperatureC() {
  if (!sensorReadings.climateValid) return 16.0f;
  const int rounded = static_cast<int>(lroundf(sensorReadings.temperatureC));
  return static_cast<float>(constrain(rounded, 0, 50));
}
#endif

bool cacheStatsAvailable = false;
uint32_t cachedComicCountForDisplay = 0;
uint32_t totalComicCountForDisplay = 0;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
RTC_DATA_ATTR time_t lastArchiveRefreshEpoch = 0;
bool quietSleepNotice = false;
uint32_t networkOperationDeadlineMs = 0;
volatile uint8_t maintenanceButtonInterruptMask = 0;
bool maintenanceCancelled = false;

inline int panelWidth() {
  return xkcd_config::runtime::panelWidth();
}

inline int panelHeight() {
  return xkcd_config::runtime::panelHeight();
}

inline bool compactPortraitLayout() {
  return config::MODEL == 1005 && !xkcd_config::runtime::isLandscape();
}

inline int headerBottom() {
  return compactPortraitLayout() ? 72 : config::ui(44);
}

inline int contentTop() {
  return compactPortraitLayout() ? 78 : config::CONTENT_TOP;
}

inline int footerBottom() {
  return panelHeight() - config::ui(12);
}

inline int minRenderedWidth() {
  return panelWidth() / 3;
}

String configurationGestureHint(bool reconfigure) {
#if RETERMINAL_MODEL == 1005
  return "Hold OK 2s from sleep to " +
         String(reconfigure ? "reconfigure" : "configure");
#else
  if (reconfigure) {
    return "Keep the green button pressed for 2 seconds to reconfigure.";
  }
  return "To configure device - from sleep, hold green for 2 seconds";
#endif
}

void beginPanel() {
#if RETERMINAL_MODEL == 1003
  if (!sensorReadings.climateValid) {
    sensorReadings.climateValid = climate::readSht4x(
        sht4, sensorReadings.temperatureC, sensorReadings.humidityPct,
        config::SENSOR_READ_ATTEMPTS, config::SENSOR_RETRY_DELAY_MS);
  }
#endif
#if RETERMINAL_MODEL == 1005
  // E1005 SD shares SCK/MOSI with the panel on a separate power rail.
  // Power and deselect it before any display traffic.
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
#endif
  epaper_setup::begin(epaper);
#if RETERMINAL_MODEL == 1003
  epaper.setTemp(panelWaveformTemperatureC);
  LOG.printf("[panel] waveform temperature=%.0fC%s\n",
             panelWaveformTemperatureC(),
             sensorReadings.climateValid ? "" : " (fallback)");
#endif
#if RETERMINAL_MODEL == 1005
  epaper.setRotation(xkcd_config::runtime::panelRotation());
  LOG.printf("[panel] orientation=%s rotation=%d geometry=%dx%d\n",
             xkcd_config::runtime::isLandscape()
                 ? (xkcd_config::runtime::orientation() ==
                            xkcd_orientation::Orientation::RotateCW
                        ? "rotate-cw"
                        : "rotate-ccw")
                 : "portrait",
             xkcd_config::runtime::panelRotation(), panelWidth(),
             panelHeight());
#endif
}

constexpr uint8_t MAINTENANCE_BUTTON_GREEN = 1U << 0;
constexpr uint8_t MAINTENANCE_BUTTON_RIGHT = 1U << 1;
constexpr uint8_t MAINTENANCE_BUTTON_LEFT = 1U << 2;

struct Comic {
  int number = 0;
  int year = 0;
  int month = 0;
  int day = 0;
  String title;
  String alt;
  String imageUrl;
  String imagePath;
};

struct ImageLayout {
  int width = 0;
  int height = 0;
  int x = 0;
  int y = 0;
  int footerDividerY = 0;
  int footerLineCount = 0;
  int footerLineHeightPx = 0;
  int footerBandPaddingPx = 0;
  String footerLines[config::FOOTER_MAX_LINES];
  float scale = 0.0f;
};

void updatePanel();

void IRAM_ATTR onMaintenanceGreenButton() {
  maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_GREEN;
}

void IRAM_ATTR onMaintenanceRightButton() {
  maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_RIGHT;
}

void IRAM_ATTR onMaintenanceLeftButton() {
  maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_LEFT;
}

void armMaintenanceButtonCancellation() {
  maintenanceButtonInterruptMask = 0;
  maintenanceCancelled = false;
  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_GREEN),
                  onMaintenanceGreenButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_RIGHT),
                  onMaintenanceRightButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_LEFT),
                  onMaintenanceLeftButton, FALLING);
  if (!digitalRead(PIN_BUTTON_GREEN)) {
    maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_GREEN;
  }
  if (!digitalRead(PIN_BUTTON_RIGHT)) {
    maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_RIGHT;
  }
  if (!digitalRead(PIN_BUTTON_LEFT)) {
    maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_LEFT;
  }
}

void disarmMaintenanceButtonCancellation() {
  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON_GREEN));
  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON_RIGHT));
  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON_LEFT));
}

bool maintenanceCancellationRequested() {
  if (networkOperationDeadlineMs == 0) return false;
  if (!maintenanceCancelled && maintenanceButtonInterruptMask != 0) {
    maintenanceCancelled = true;
    LOG.printf("[precache] button press detected (mask=0x%x); "
               "cancelling maintenance\n",
               maintenanceButtonInterruptMask);
  }
  return maintenanceCancelled;
}

bool networkDeadlineReached() {
  return app_logic::deadlineReached(millis(), networkOperationDeadlineMs);
}

bool networkOperationShouldStop() {
  return maintenanceCancellationRequested() || networkDeadlineReached();
}

uint32_t boundedNetworkTimeout(uint32_t normalTimeoutMs) {
  if (networkOperationDeadlineMs == 0) return normalTimeoutMs;
  if (normalTimeoutMs > config::ARCHIVE_CANCEL_POLL_TIMEOUT_MS) {
    normalTimeoutMs = config::ARCHIVE_CANCEL_POLL_TIMEOUT_MS;
  }
  const int32_t remaining =
      static_cast<int32_t>(networkOperationDeadlineMs - millis());
  if (remaining <= 0) return 1;
  const uint32_t remainingMs = static_cast<uint32_t>(remaining);
  return remainingMs < normalTimeoutMs ? remainingMs : normalTimeoutMs;
}

bool archiveRefreshDue() {
  const time_t now = time(nullptr);
  return app_logic::refreshDue(
      false, local_time::clockIsValid(), now, lastArchiveRefreshEpoch,
      config::ARCHIVE_REFRESH_SECONDS);
}

void logMemory(const char* label) {
  LOG.printf("[mem] %-20s heap=%luK psram=%lu/%luK\n", label,
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
             static_cast<unsigned long>(ESP.getFreePsram() / 1024),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024));
}

// batteryPercentForVoltage() and the 16-sample averaging block used to be
// inline here; they now live in common/include/battery_gauge.h and are
// invoked via battery::measureBatteryFromAdc().

// Smooth-font (VLW) pixel sizes chosen to visually match the previous
// bitmap FreeFonts.  DejaVu Sans is slightly taller per em than
// FreeSansBold, so the smooth-font pixel sizes are chosen so the
// cap-height visually matches the GFX FreeSansBold fallback at the
// same role.  Files are generated by tools/fonts/make_vlw.py as
// /fonts/sans_bold_<size>.vlw on SD.
#if RETERMINAL_MODEL == 1003
constexpr int SMOOTH_FONT_TITLE_PX = 40;   // matches FreeSansBold24pt7b
constexpr int SMOOTH_FONT_FOOTER_PX = 30;  // matches FreeSansBold18pt7b
#elif RETERMINAL_MODEL == 1004
constexpr int SMOOTH_FONT_TITLE_PX = 30;   // matches FreeSansBold18pt7b
constexpr int SMOOTH_FONT_FOOTER_PX = 20;  // matches FreeSansBold12pt7b
#elif RETERMINAL_MODEL == 1005
// Smaller antialiased cuts lose stroke weight on the one-bit panel.
constexpr int SMOOTH_FONT_TITLE_PX = 20;
constexpr int SMOOTH_FONT_FOOTER_PX = 20;
#else
constexpr int SMOOTH_FONT_TITLE_PX = 20;   // matches FreeSansBold12pt7b
constexpr int SMOOTH_FONT_FOOTER_PX = 16;  // matches FreeSansBold9pt7b
#endif

// Per-model fallback FreeFonts, also used directly for status/message
// screens (which keep the GFX bitmap fonts even when smooth fonts are
// available - .vlw load/unload adds visible delay to every boot screen).
#if RETERMINAL_MODEL == 1003
constexpr const GFXfont* TITLE_FALLBACK_FONT = &FreeSansBold24pt7b;
constexpr const GFXfont* FOOTER_FALLBACK_FONT = &FreeSansBold18pt7b;
#elif RETERMINAL_MODEL == 1004
constexpr const GFXfont* TITLE_FALLBACK_FONT = &FreeSansBold18pt7b;
constexpr const GFXfont* FOOTER_FALLBACK_FONT = &FreeSansBold12pt7b;
#elif RETERMINAL_MODEL == 1005
constexpr const GFXfont* TITLE_FALLBACK_FONT = &FreeSansBold12pt7b;
constexpr const GFXfont* FOOTER_FALLBACK_FONT = &FreeSansBold12pt7b;
#else
constexpr const GFXfont* TITLE_FALLBACK_FONT = &FreeSansBold12pt7b;
constexpr const GFXfont* FOOTER_FALLBACK_FONT = &FreeSansBold9pt7b;
#endif

void selectStatusFont() {
  smoothFontManager.selectGfx(
#if RETERMINAL_MODEL == 1003
      &FreeSansBold18pt7b
#elif RETERMINAL_MODEL == 1004
      &FreeSansBold12pt7b
#else
      &FreeSansBold9pt7b
#endif
  );
}

// One size below selectStatusFont on the larger E1003/E1004 panels so
// the publication date reads as a subtle annotation rather than
// competing with the battery percentage. On the smaller E1001/E1002
// panels the status font already sits at the smallest bold size we
// ship, so we drop to the non-bold 9pt cut for the same visual step
// down.
void selectComicDateFont() {
  if constexpr (panel_traits::IS_COMPACT) {
    smoothFontManager.selectGfx(nullptr);
    epaper.setTextFont(2);
  } else {
    smoothFontManager.selectGfx(
#if RETERMINAL_MODEL == 1003
        &FreeSansBold12pt7b
#elif RETERMINAL_MODEL == 1004
        &FreeSansBold9pt7b
#else
        &FreeSans9pt7b
#endif
    );
  }
}

// TFT_eSPI's MC/ML/MR datums center the smooth font's yAdvance box on
// the requested y, but DejaVu Sans Bold's ascent (~28 at 30px) is
// much larger than its descent (~8), so the visual cap-center sits a
// few pixels above the box center.  This helper returns the y offset
// (in pixels) needed to align the cap-center with the caller's y.
// Returns 0 when a smooth font is not loaded so GFX callers are
// unaffected.
static int smoothCenterYAdjust() {
  if (!smoothFontManager.smoothLoaded()) return 0;
  const int yA = static_cast<int>(epaper.gFont.yAdvance);
  const int mA = static_cast<int>(epaper.gFont.maxAscent);
  const int a  = static_cast<int>(epaper.gFont.ascent);
  // Approximate cap-height as ascent * 0.78 (DejaVu Sans Bold).
  return (yA / 2) - mA + (a * 78 / 200);
}

#if RETERMINAL_MODEL == 1005
bool drawLoadedSmoothTextMonochrome(const String& text,
                                    int centerX, int centerY) {
  if (!smoothFontManager.smoothLoaded() || !epaper.fontLoaded ||
      !epaper.fs_font || !epaper.fontFile) {
    return false;
  }

  constexpr uint8_t kSolidAlphaThreshold = 64;
  uint8_t row[256];
  int cursorX = centerX - epaper.textWidth(text, 1) / 2;
  const int cursorY = centerY - epaper.gFont.yAdvance / 2;
  uint16_t offset = 0;
  const uint16_t length = static_cast<uint16_t>(text.length());
  auto* utf8 = reinterpret_cast<uint8_t*>(
      const_cast<char*>(text.c_str()));

  while (offset < length) {
    const uint16_t code = epaper.decodeUTF8(utf8, &offset, length - offset);
    if (code == 0x20) {
      cursorX += epaper.gFont.spaceWidth;
      continue;
    }

    uint16_t glyph = 0;
    if (!epaper.getUnicodeIndex(code, &glyph)) {
      cursorX += epaper.gFont.spaceWidth;
      continue;
    }

    const uint8_t width = epaper.gWidth[glyph];
    const uint8_t height = epaper.gHeight[glyph];
    const int left = cursorX + epaper.gdX[glyph];
    const int top =
        cursorY + epaper.gFont.maxAscent - epaper.gdY[glyph];
    if (!epaper.fontFile.seek(epaper.gBitmap[glyph], fs::SeekSet)) {
      return false;
    }
    for (uint8_t y = 0; y < height; ++y) {
      if (epaper.fontFile.read(row, width) != width) return false;
      for (uint8_t x = 0; x < width; ++x) {
        if (row[x] >= kSolidAlphaThreshold) {
          epaper.drawPixel(left + x, top + y, PANEL_BLACK);
        }
      }
    }
    cursorX += epaper.gxAdvance[glyph];
  }
  return true;
}
#endif

void drawSelectedSmoothTextCentered(const String& text,
                                    int centerX, int centerY) {
#if RETERMINAL_MODEL == 1005
  if (drawLoadedSmoothTextMonochrome(text, centerX, centerY)) return;
#endif
  epaper.drawString(text, centerX, centerY, 1);
}

void selectCacheStatsFont() {
#if RETERMINAL_MODEL == 1003
  smoothFontManager.selectGfx(&FreeSans9pt7b);
#elif RETERMINAL_MODEL == 1004
  smoothFontManager.selectGfx(&FreeSans9pt7b);
#else
  smoothFontManager.selectGfx(nullptr);
  epaper.setTextFont(2);
#endif
}

smooth_fonts::Selection selectTitleFont() {
  return smoothFontManager.select(
      SMOOTH_FONT_TITLE_PX, TITLE_FALLBACK_FONT, sdReady);
}

smooth_fonts::Selection selectFooterFont() {
  return smoothFontManager.select(
      SMOOTH_FONT_FOOTER_PX, FOOTER_FALLBACK_FONT, sdReady);
}

// Message screens (boot / Wi-Fi status / errors) render short strings the
// firmware controls, so they use the bitmap GFX fonts directly.  Skipping
// the .vlw path keeps the fast-path fast and avoids leaving a smooth font
// loaded when the next screen is a comic that wants a different size.
void selectStatusMessageTitleFont() {
  smoothFontManager.selectGfx(TITLE_FALLBACK_FONT);
}
void selectStatusMessageDetailFont() {
  smoothFontManager.selectGfx(FOOTER_FALLBACK_FONT);
}

// Formats a comic's publication date as a fixed "AA-BB-CCCC" (or
// "CCCC-AA-BB" for YMD) string, honouring config::DATE_LOCALE. Returns an
// empty string when any component is zero (unknown), so callers can skip
// rendering silently on malformed metadata rather than showing "0-0-0".
String formatComicDate(const Comic& comic) {
  if (comic.year <= 0 || comic.month <= 0 || comic.day <= 0) return String();
  char buf[16];
  switch (xkcd_config::runtime::dateLocale()) {
    case config::DateLocale::MDY:
      snprintf(buf, sizeof(buf), "%02d-%02d-%04d", comic.month, comic.day,
               comic.year);
      break;
    case config::DateLocale::YMD:
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d", comic.year, comic.month,
               comic.day);
      break;
    case config::DateLocale::DMY:
    default:
      snprintf(buf, sizeof(buf), "%02d-%02d-%04d", comic.day, comic.month,
               comic.year);
      break;
  }
  return String(buf);
}

void drawBadges(uint32_t background = PANEL_WHITE,
                bool fillTextBackground = true,
                const String* lastRefreshTime = nullptr) {
  epaper.setTextColor(PANEL_BLACK, background, fillTextBackground);
  selectStatusFont();

  const int statusCenterY = config::ui(24);
  const int edgeInset = config::ui(6);
  epaper.setTextDatum(ML_DATUM);
  String climate = "--.-C  --%";
  if (sensorReadings.climateValid) {
    climate = String(sensorReadings.temperatureC, 1) + "C  " + String(sensorReadings.humidityPct, 0) + "%";
  }
  epaper.drawString(climate, edgeInset, statusCenterY, 1);

  String percent = sensorReadings.batteryPct >= 0 ? String(sensorReadings.batteryPct) + "%" : "--%";

  // Keep the whole battery group clear of the bezel. A fixed two-pixel optical
  // offset aligns the gauge with the percentage on the high-DPI E1003 without
  // over-scaling that adjustment.
  const int w = config::ui(22);
  const int h = config::ui(12);
  const int terminalWidth = max(3, config::ui(5));
  const int x = panelWidth() - edgeInset - terminalWidth - w;
  const int gaugeCenterY = statusCenterY + 2;
  const int y = gaugeCenterY - h / 2;
  const int outline = max(1, config::ui(1));
  const int terminalHeight = max(3, config::ui(5));

  epaper.setTextDatum(MR_DATUM);
  const int percentRightX = x - config::ui(9);
  epaper.drawString(percent, percentRightX, statusCenterY, 1);

  // The two-line "MM-DD / HH:MM" refresh timestamp sits directly to the
  // left of the battery percentage, in the same greyed-out cache-stats
  // colour as the comic counts to its left, so the eye reads the whole
  // right-hand cluster as a single "status" block.
  int nextRightX =
      percentRightX - epaper.textWidth(percent, 1) - config::ui(10);
  const bool showExtendedStatus = !compactPortraitLayout();
  if (showExtendedStatus &&
      xkcd_config::runtime::debugShowStatusBadges() &&
      lastRefreshTime != nullptr &&
      !lastRefreshTime->isEmpty()) {
    const int separator = lastRefreshTime->indexOf('T');
    if (separator >= 10 &&
        lastRefreshTime->length() >= static_cast<size_t>(separator + 6)) {
      const String updateDate = lastRefreshTime->substring(5, 10);
      const String updateClock =
          lastRefreshTime->substring(separator + 1, separator + 6);
      selectCacheStatsFont();
      epaper.setTextColor(PANEL_CACHE_STATS_COLOR, background,
                          fillTextBackground);
      const int lineCenterDistance = epaper.fontHeight(1) + 1;
      const int dateY = statusCenterY - lineCenterDistance / 2;
      const int timeY = dateY + lineCenterDistance;
      epaper.drawString(updateDate, nextRightX, dateY, 1);
      epaper.drawString(updateClock, nextRightX, timeY, 1);
      const int refreshWidth =
          max(epaper.textWidth(updateDate, 1), epaper.textWidth(updateClock, 1));
      nextRightX -= refreshWidth + config::ui(10);
      // Restore the status font so cache-stats layout below sees the same
      // font height it always did.
      selectStatusFont();
    }
  }

  if (showExtendedStatus &&
      xkcd_config::runtime::debugShowStatusBadges() &&
      cacheStatsAvailable) {
    const int statsRightX = nextRightX;
    selectCacheStatsFont();
    epaper.setTextColor(PANEL_CACHE_STATS_COLOR, background,
                        fillTextBackground);
    const int statsCenterDistance = epaper.fontHeight(1) + 1;
    const int upperStatsY = statusCenterY - statsCenterDistance / 2;
    const int lowerStatsY = upperStatsY + statsCenterDistance;
    epaper.drawString(String(cachedComicCountForDisplay), statsRightX,
                      upperStatsY, 1);
    epaper.drawString(totalComicCountForDisplay > 0
                          ? String(totalComicCountForDisplay)
                          : "--",
                      statsRightX, lowerStatsY, 1);
  }
  text_render::drawBatteryGauge(epaper, x, y, w, h, sensorReadings.batteryPct, outline,
                                terminalWidth, terminalHeight, PANEL_BLACK, PANEL_WHITE,
                                sensorReadings.externalPowerValid && sensorReadings.externalPower);

  smoothFontManager.selectGfx(nullptr);
  epaper.setTextFont(2);
}

void renderStatus(const String& message, const String& detail = "",
                  const String& lineAbove = "",
                  const String& helpBelow = "",
                  const String& subHelpBelow = "",
                  bool showRepositoryQr = false) {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  if (!lineAbove.isEmpty()) {
    selectStatusMessageDetailFont();
    epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(lineAbove),
                                panelWidth() - config::ui(60), 1),
                      panelWidth() / 2,
                      panelHeight() / 2 - config::ui(55), 1);
  }
  selectStatusMessageTitleFont();
  epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(message),
                              panelWidth() - config::ui(60), 1),
                    panelWidth() / 2,
                    panelHeight() / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectStatusMessageDetailFont();
    epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(detail),
                                panelWidth() - config::ui(60), 1),
                      panelWidth() / 2,
                      panelHeight() / 2 + config::ui(22), 1);
  }
  if (!subHelpBelow.isEmpty()) {
    // Small ASCII sub-line (MAC + firmware) drawn just above the bottom
    // help hint so it stays informative without competing with the main
    // "Connecting to..." message.
    selectStatusMessageDetailFont();
    epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(subHelpBelow),
                                panelWidth() - config::ui(60), 1),
                      panelWidth() / 2,
                      panelHeight() - config::ui(46), 1);
  }
  if (!helpBelow.isEmpty()) {
    // Bottom-of-panel hint: used for the "hold green at boot to
    // reconfigure Wi-Fi" hint on the "Connecting to..." splash and
    // the same hint on the "XKCD refresh failed" screen.
    selectStatusMessageDetailFont();
    epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(helpBelow),
                                panelWidth() - config::ui(60), 1),
                      panelWidth() / 2,
                      panelHeight() - config::ui(24), 1);
  }
  smoothFontManager.selectGfx(nullptr);
  epaper.setTextFont(2);
  drawBadges();
  if (showRepositoryQr) {
    repo_qr::drawBottomRight(epaper, panelWidth(), panelHeight(),
                             max(2, config::ui(2)), config::ui(12),
                             PANEL_BLACK, PANEL_WHITE);
  }
  updatePanel();
}

// The shared PNG encoder is invoked through screenshot::saveScreenshotPng().

void updatePanel() {
  if (screenshotRequested && sdReady) {
    screenshot::saveScreenshotPng(epaper, panelWidth(), panelHeight());
    screenshotRequested = false;
  }
  const uint32_t heap = ESP.getFreeHeap();
  const uint32_t psram = ESP.getFreePsram();
  LOG.printf("[mem] before epd update  heap=%luK psram=%luK\n",
             static_cast<unsigned long>(heap / 1024),
             static_cast<unsigned long>(psram / 1024));
  const uint32_t start = millis();
  LOG.println("[render] epaper.update() start");
  panel_watchdog::refresh(epaper);
  framebufferReady = true;
  LOG.printf("[render] epaper.update() returned after %lu ms\n",
             static_cast<unsigned long>(millis() - start));
}

bool parseComic(const String& json, Comic& comic) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, json);
  if (error) {
    LOG.printf("[json] %s\n", error.c_str());
    return false;
  }
  JsonVariantConst schemaField = document[xkcd_cache::SCHEMA_FIELD];
  if (!schemaField.isNull()) {
    const char* tag = schemaField.as<const char*>();
    if (!tag || strcmp(tag, xkcd_cache::SCHEMA_TAG) != 0) {
      LOG.printf("[cache] schema mismatch: %s\n", tag ? tag : "(null)");
      return false;
    }
  }
  comic.number = document["num"] | 0;
  // xkcd's info.0.json stores year/month/day as JSON strings (e.g. "2025",
  // "3", "14") but occasionally as bare numbers on very old comics. Read
  // them permissively as ints; zeros mean "unknown" and suppress the date.
  comic.year = document["year"].is<int>()
                   ? document["year"].as<int>()
                   : atoi(document["year"] | "0");
  comic.month = document["month"].is<int>()
                    ? document["month"].as<int>()
                    : atoi(document["month"] | "0");
  comic.day = document["day"].is<int>()
                  ? document["day"].as<int>()
                  : atoi(document["day"] | "0");
  const char* title = document["safe_title"];
  if (!title || !*title) title = document["title"];
  if (!title || !*title) title = "xkcd";
  comic.title = text_render::displayText(String(title));
  comic.alt = text_render::displayText(String(document["alt"] | ""));
  comic.imageUrl = String(document["img"] | "");
  return comic.number > 0 && !comic.imageUrl.isEmpty();
}

bool imageFileExists(int number, const String& extension) {
  if (!sdReady || number <= 0 || extension.isEmpty()) return false;
  return sd_card::fileExists(String(config::CACHE_DIR) + "/" + number + extension);
}

bool comicFullyCached(int number) {
  if (!sdReady || number <= 0 || number == 404) return false;
  const auto* meta = xkcd_index::metadata(number);
  if (meta == nullptr || meta->extension.isEmpty()) return false;
  return sd_card::fileExists(String(config::CACHE_DIR) + "/" + number + meta->extension);
}

bool getComic(int number, bool networkAvailable, Comic& comic) {
  if (xkcd_index::skipped(number)) return false;

  // Manifest hit: title/alt/extension/url are already in memory.
  // The fileExists() check here is the ONLY SD lookup on the cached
  // pick path; the historical per-comic .json read and separate
  // existence call are gone.
  if (const auto* meta = xkcd_index::metadata(number)) {
    comic.number = number;
    comic.title = meta->title;
    comic.alt = meta->alt;
    comic.imageUrl = meta->url;
    comic.year = meta->year;
    comic.month = meta->month;
    comic.day = meta->day;
    comic.imagePath =
        String(config::CACHE_DIR) + "/" + comic.number + meta->extension;
    if (sd_card::fileExists(comic.imagePath)) {
      LOG.printf("[cache] using %s\n", comic.imagePath.c_str());
      return true;
    }
    // Image file went missing since the manifest was written.
    if (!networkAvailable || !sdCacheWritable || meta->url.isEmpty()) {
      return false;
    }
    LOG.printf("[comic] cached #%d image missing; redownloading from %s\n",
               comic.number, comic.imageUrl.c_str());
    if (!net_http::downloadToSd(comic.imageUrl, comic.imagePath, config::HTTP_TIMEOUT_MS, config::DOWNLOAD_IDLE_TIMEOUT_MS, config::MAX_IMAGE_BYTES, networkOperationShouldStop, boundedNetworkTimeout)) {
      sdCacheWritable = false;
      return false;
    }
    return true;
  }

  // Manifest miss: we've never seen this comic. Requires network to
  // learn title/alt/url.
  if (!networkAvailable || number == 404) return false;
  String json;
  const String url = "https://xkcd.com/" + String(number) + "/info.0.json";
  if (!net_http::getString(url, json, config::HTTP_TIMEOUT_MS, networkOperationShouldStop, boundedNetworkTimeout) || !parseComic(json, comic)) return false;

  const String extension = net_http::imageExtension(comic.imageUrl);
  if (extension.isEmpty()) {
    LOG.printf("[comic] #%d uses an unsupported image format\n", comic.number);
    xkcd_index::markSkipped(number);
    return false;
  }
  comic.imagePath = String(config::CACHE_DIR) + "/" + comic.number + extension;
  if (!sdCacheWritable) {
    LOG.printf("[cache] skipping image write for #%d after an SD write failure\n",
               comic.number);
    return false;
  }
  LOG.printf("[comic] downloading #%d from %s\n", comic.number, comic.imageUrl.c_str());
  if (!net_http::downloadToSd(comic.imageUrl, comic.imagePath, config::HTTP_TIMEOUT_MS, config::DOWNLOAD_IDLE_TIMEOUT_MS, config::MAX_IMAGE_BYTES, networkOperationShouldStop, boundedNetworkTimeout)) {
    sdCacheWritable = false;
    LOG.printf("[cache] image #%d not stored; PSRAM fallback available\n",
               comic.number);
    return false;
  }

  xkcd_index::ComicMeta newMeta;
  newMeta.title = comic.title;
  newMeta.alt = comic.alt;
  newMeta.extension = extension;
  newMeta.url = comic.imageUrl;
  newMeta.year = static_cast<int16_t>(comic.year);
  newMeta.month = static_cast<uint8_t>(comic.month);
  newMeta.day = static_cast<uint8_t>(comic.day);
  xkcd_index::addComic(comic.number, newMeta);
  return true;
}

bool getLatestNumber(bool networkAvailable, int& latest,
                     bool refreshOnline = false) {
  const int cached = xkcd_index::latest();
  const bool shouldCheckOnline = networkAvailable &&
      (refreshOnline || cached <= 0);
  if (shouldCheckOnline) {
    String json;
    Comic comic;
    if (net_http::getString(config::XKCD_LATEST_URL, json, config::HTTP_TIMEOUT_MS, networkOperationShouldStop, boundedNetworkTimeout) && parseComic(json, comic)) {
      latest = comic.number;
      xkcd_index::setLatest(latest);
      LOG.printf("[xkcd] latest is #%d\n", latest);
      totalComicCountForDisplay =
          app_logic::publishedComicCount(latest);
      return true;
    }
  }
  if (cached > 0) {
    latest = cached;
    totalComicCountForDisplay =
        app_logic::publishedComicCount(latest);
    LOG.printf("[xkcd] cached latest is #%d\n", latest);
    return true;
  }
  return false;
}

void refreshArchiveCache() {
  if (!sdReady || WiFi.status() != WL_CONNECTED) return;

  // Maintenance is the authoritative integrity pass. Validate the complete
  // in-memory index against on-disk images before downloads, then add
  // successful downloads to the clean index and persist it.
  if (!xkcd_index::rebuild(sdReady, imageFileExists, networkOperationShouldStop)) {
    if (maintenanceCancellationRequested()) {
      LOG.println("[precache] index maintenance cancelled by button");
    } else if (networkDeadlineReached()) {
      LOG.println("[precache] index maintenance reached the five-minute deadline");
    }
    return;
  }

  const int previousLatest = xkcd_index::latest();
  int latest = 0;
  if (!getLatestNumber(true, latest, true)) {
    if (maintenanceCancellationRequested()) {
      LOG.println("[precache] maintenance cancelled by button");
    } else if (networkDeadlineReached()) {
      LOG.println("[precache] five-minute maintenance deadline reached");
    } else {
      LOG.println("[precache] latest XKCD lookup failed");
    }
    return;
  }

  uint8_t latestAdded = 0;
  if ((latest > previousLatest || !comicFullyCached(latest)) &&
      latest != 404) {
    Comic newest;
    if (getComic(latest, true, newest) && comicFullyCached(latest)) {
      latestAdded = 1;
      LOG.printf("[precache] cached newest comic #%d\n", latest);
    }
  }

  uint8_t oldAdded = 0;
  uint16_t attempts = 0;
  const uint16_t requestedAttempts =
      static_cast<uint16_t>(config::ARCHIVE_OLD_COMICS_PER_REFRESH) * 20U;
  const uint16_t maxAttempts =
      requestedAttempts > 80U ? requestedAttempts : 80U;
  while (oldAdded < config::ARCHIVE_OLD_COMICS_PER_REFRESH &&
         attempts++ < maxAttempts && latest > 1 && sdCacheWritable &&
         !networkOperationShouldStop()) {
    const int number = random(1, latest);
    if (number == 404 || xkcd_index::skipped(number) ||
        comicFullyCached(number)) continue;
    Comic historical;
    if (getComic(number, true, historical) && comicFullyCached(number)) {
      ++oldAdded;
      LOG.printf("[precache] cached historical comic %u/%u: #%d\n",
                 oldAdded, config::ARCHIVE_OLD_COMICS_PER_REFRESH, number);
    }
  }

  if (maintenanceCancellationRequested()) {
    LOG.println("[precache] maintenance cancelled; remaining downloads deferred");
  } else if (networkDeadlineReached()) {
    LOG.println("[precache] five-minute maintenance deadline reached; "
                "remaining downloads deferred");
  }
  if (!xkcd_index::persist()) {
    LOG.println("[cache] updated comic index could not be stored");
  }
  LOG.printf("[precache] refill complete: %u newest, %u historical, "
             "%lu total cached\n",
             latestAdded, oldAdded,
             static_cast<unsigned long>(xkcd_index::count()));
}

bool getLatestNumberWithoutSd(bool networkAvailable, int& latest) {
  if (!networkAvailable) return false;

  String json;
  Comic latestComic;
  if (net_http::getString(config::XKCD_LATEST_URL, json, config::HTTP_TIMEOUT_MS, networkOperationShouldStop, boundedNetworkTimeout) && parseComic(json, latestComic)) {
    latest = latestComic.number;
    LOG.printf("[xkcd] live latest is #%d (not cached)\n", latest);
    return true;
  }
  return false;
}

bool getComicWithoutSd(int number, Comic& comic, uint8_t*& compressed,
                       size_t& compressedLength) {
  compressed = nullptr;
  compressedLength = 0;
  if (number <= 0 || number == 404) return false;
  if (xkcd_index::skipped(number)) return false;

  String json;
  const String metadataUrl = "https://xkcd.com/" + String(number) + "/info.0.json";
  if (!net_http::getString(metadataUrl, json, config::HTTP_TIMEOUT_MS, networkOperationShouldStop, boundedNetworkTimeout) || !parseComic(json, comic)) return false;
  if (net_http::imageExtension(comic.imageUrl).isEmpty()) {
    LOG.printf("[comic] #%d uses an unsupported image format\n", comic.number);
    xkcd_index::markSkipped(number);
    return false;
  }
  LOG.printf("[comic] downloading #%d directly to PSRAM\n", comic.number);
  return net_http::downloadToMemory(comic.imageUrl, compressed, compressedLength, config::HTTP_TIMEOUT_MS, config::DOWNLOAD_IDLE_TIMEOUT_MS, config::MAX_LIVE_IMAGE_BYTES, networkOperationShouldStop, boundedNetworkTimeout);
}

int pickRandomCachedNumber() {
  if (!xkcd_index::ready() || xkcd_index::entries().empty()) return 0;
  // Trust the manifest. If the file is missing on disk, the decode step
  // will fail and the caller retries with a fresh pick.
  return xkcd_index::entries()[random(static_cast<long>(xkcd_index::entries().size()))];
}

ImageLayout calculateLayout(const Comic& comic, int sourceWidth, int sourceHeight) {
  ImageLayout layout;
  const String footer = comic.alt.isEmpty() ? comic.title : comic.alt;
  selectFooterFont();
  layout.footerLineCount = text_render::wrapText(epaper, footer, layout.footerLines,
                                    config::FOOTER_MAX_LINES,
                                    panelWidth() - config::ui(24), 1);
  // Size the footer band from the smooth font's actual yAdvance so the
  // strip hugs the text.  Fall back to the compile-time constants when
  // the smooth font failed to load (GFX path preserves legacy sizing).
  const int smoothYA = static_cast<int>(epaper.gFont.yAdvance);
  layout.footerLineHeightPx =
      (smoothFontManager.smoothLoaded() && smoothYA > 0)
          ? smoothYA + config::ui(2)
          : config::FOOTER_LINE_HEIGHT;
  layout.footerBandPaddingPx =
      (smoothFontManager.smoothLoaded() && smoothYA > 0)
          ? config::ui(4)
          : config::FOOTER_VERTICAL_PADDING;
  // Leave the smooth footer font loaded so renderComic can reuse it
  // without paying another ~2 s SD read for the same size.
  layout.footerDividerY =
      footerBottom() -
      layout.footerLineCount * layout.footerLineHeightPx -
      2 * layout.footerBandPaddingPx;
  const int maxWidth = panelWidth() - 2 * config::CONTENT_MARGIN_X;
  const int maxHeight = layout.footerDividerY - contentTop() - config::ui(6);
  // Contain the comic in this model's actual content rectangle. Unlike the
  // original E1001-only layout, this also enlarges small source comics so they
  // remain readable on high-resolution E1003/E1004 panels.
  layout.scale = min(static_cast<float>(maxWidth) / sourceWidth,
                     static_cast<float>(maxHeight) / sourceHeight);
  layout.width = max(2, static_cast<int>(sourceWidth * layout.scale));
  layout.height = max(2, static_cast<int>(sourceHeight * layout.scale));
  // Packed 4bpp requires an even row width (two pixels per byte). The E1005
  // 1bpp path supports arbitrary widths and pads only the final byte of a row.
#if RETERMINAL_MODEL != 1005
  if (layout.width & 1) --layout.width;
#endif
  layout.x = (panelWidth() - layout.width) / 2;
#if RETERMINAL_MODEL != 1005
  if (layout.x & 1) --layout.x;
#endif
  layout.y = contentTop() + (maxHeight - layout.height) / 2;
  return layout;
}

bool layoutIsSuitable(const ImageLayout& layout, int comicNumber) {
  if (layout.scale < xkcd_config::runtime::minDisplayScale()) {
    LOG.printf("[comic] #%d skipped: it needs reduction below %.0f%%\n",
               comicNumber, xkcd_config::runtime::minDisplayScale() * 100.0f);
    return false;
  }
  const int minimumWidth = minRenderedWidth();
  if (layout.width < minimumWidth) {
    LOG.printf("[comic] #%d skipped: rendered width %d is below the "
               "%d-pixel minimum for this panel\n",
               comicNumber, layout.width, minimumWidth);
    return false;
  }
  const size_t renderPixels =
      static_cast<size_t>(layout.width) * static_cast<size_t>(layout.height);
  if (renderPixels > config::MAX_RENDER_PIXELS) {
    LOG.printf("[comic] #%d skipped: %lu render pixels exceeds %lu-pixel budget\n",
               comicNumber, static_cast<unsigned long>(renderPixels),
               static_cast<unsigned long>(config::MAX_RENDER_PIXELS));
    return false;
  }
  return true;
}

bool loadUsableComic(int number, bool networkAvailable, Comic& comic,
                     RgbImage& image, ImageLayout& layout) {
  comic = Comic{};
  const bool sdImageReady = getComic(number, networkAvailable, comic);
  bool decoded = false;

  if (sdImageReady) {
    logMemory("before decode");
    decoded = load_image_from_sd(comic.imagePath.c_str(), 0, 0, &image);
    if (!decoded) {
      LOG.printf("[comic] cached #%d could not be decoded\n", number);
      image_free(&image);
    }
  }

  // A mounted SD card can still be read-only, full, or have a failed/corrupt
  // cache entry. Keep the live PSRAM path available instead of making the
  // mounted-card path an all-or-nothing choice.
  if (!decoded && networkAvailable && comic.number > 0 &&
      !comic.imageUrl.isEmpty() && !net_http::imageExtension(comic.imageUrl).isEmpty()) {
    LOG.printf("[comic] loading #%d live into PSRAM without caching\n", number);
    uint8_t* compressed = nullptr;
    size_t compressedLength = 0;
    if (net_http::downloadToMemory(comic.imageUrl, compressed, compressedLength, config::HTTP_TIMEOUT_MS, config::DOWNLOAD_IDLE_TIMEOUT_MS, config::MAX_LIVE_IMAGE_BYTES, networkOperationShouldStop, boundedNetworkTimeout)) {
      decoded = load_image_from_memory(
          compressed, compressedLength, comic.imageUrl.c_str(), 0, 0, &image);
      free(compressed);
    }
  }

  if (!decoded) {
    if (!sdImageReady && !networkAvailable) {
      LOG.printf("[comic] #%d is not fully cached\n", number);
    } else if (comic.number <= 0 || comic.imageUrl.isEmpty()) {
      LOG.printf("[comic] #%d metadata or image is unavailable\n", number);
    } else {
      LOG.printf("[comic] #%d could not be decoded from cache or live data\n",
                 number);
    }
    return false;
  }
  layout = calculateLayout(comic, image.width, image.height);
  LOG.printf("[layout] #%d source=%dx%d target=%dx%d scale=%.3f\n",
             comic.number, image.width, image.height, layout.width, layout.height,
             layout.scale);
  const size_t sourcePixels =
      static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  if (sourcePixels > config::MAX_DECODED_PIXELS) {
    LOG.printf("[comic] #%d skipped: %lu source pixels exceeds %lu-pixel budget\n",
               comic.number, static_cast<unsigned long>(sourcePixels),
               static_cast<unsigned long>(config::MAX_DECODED_PIXELS));
    image_free(&image);
    return false;
  }
  if (!layoutIsSuitable(layout, comic.number)) {
    image_free(&image);
    return false;
  }
  return true;
}

bool acquireComic(bool networkAvailable, Comic& comic, RgbImage& image,
                  ImageLayout& layout) {
  const int32_t forcedComic = xkcd_config::runtime::debugForceComic();
  if (forcedComic > 0) {
    LOG.printf("[debug] DEBUG_FORCE_COMIC=%d, bypassing selection\n",
               forcedComic);
    if (loadUsableComic(forcedComic, networkAvailable, comic,
                        image, layout)) {
      return true;
    }
    LOG.printf("[debug] forced comic #%d could not be loaded; falling back\n",
               forcedComic);
    image_free(&image);
  }

  if (networkAvailable) {
    int latest = 0;
    if (getLatestNumber(true, latest)) {
      for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS;
           ++attempt) {
        int number = random(1, latest + 1);
        if (number == 404 || xkcd_index::skipped(number)) continue;
        LOG.printf("[comic] random attempt %u: #%d\n", attempt + 1, number);
        if (loadUsableComic(number, true, comic, image, layout)) return true;
        image_free(&image);
      }
    }
    LOG.println("[comic] live selection failed; trying the local SD cache");
  } else {
    LOG.println("[comic] offline wake; selecting from the local SD cache");
  }

  for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS; ++attempt) {
    const int number = pickRandomCachedNumber();
    if (number <= 0 || number == 404) continue;
    LOG.printf("[cache] random local attempt %u: #%d\n", attempt + 1, number);
    if (loadUsableComic(number, false, comic, image, layout)) return true;
    image_free(&image);
  }
  return false;
}

bool acquireComicWithoutSd(bool networkAvailable, Comic& comic, RgbImage& image,
                           ImageLayout& layout) {
  int latest = 0;
  if (!getLatestNumberWithoutSd(networkAvailable, latest) || !networkAvailable) {
    return false;
  }

  for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS; ++attempt) {
    const int number = random(1, latest + 1);
    if (number == 404) continue;
    LOG.printf("[comic] live attempt %u: #%d\n", attempt + 1, number);

    uint8_t* compressed = nullptr;
    size_t compressedLength = 0;
    if (!getComicWithoutSd(number, comic, compressed, compressedLength)) continue;
    const bool decoded = load_image_from_memory(
        compressed, compressedLength, comic.imageUrl.c_str(), 0, 0, &image);
    free(compressed);
    if (!decoded) {
      LOG.printf("[comic] live #%d could not be decoded\n", number);
      continue;
    }

    layout = calculateLayout(comic, image.width, image.height);
    LOG.printf("[layout] live #%d source=%dx%d target=%dx%d scale=%.3f\n",
               comic.number, image.width, image.height, layout.width, layout.height,
               layout.scale);
    if (layoutIsSuitable(layout, comic.number)) return true;
    image_free(&image);
  }
  return false;
}

bool renderComic(const Comic& comic, RgbImage& image, ImageLayout layout) {
  const bool scaling = layout.width != image.width || layout.height != image.height;
  const size_t pixelCount = static_cast<size_t>(layout.width) * layout.height;
  uint8_t* indices = static_cast<uint8_t*>(ps_malloc(pixelCount));
  if (!indices) indices = static_cast<uint8_t*>(malloc(pixelCount));
  if (!indices) return false;

#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004 || \
    RETERMINAL_MODEL == 1005
  // A full high-resolution RGB888 resize can consume most or all of the
  // 8 MB PSRAM. Resample and Bayer-dither directly into the indexed panel
  // buffer. E1005 uses this path to leave room for the decoded source image.
  if (scaling) {
    LOG.printf("[render] %s direct indexed resize %dx%d -> %dx%d, %lu pixels\n",
               COLOR_MODE_NAME, image.width, image.height,
               layout.width, layout.height,
               static_cast<unsigned long>(pixelCount));
    const bool rendered = dither_resized_image(
        image.pixels, image.width, image.height, layout.width, layout.height,
        PANEL_PALETTE, config::DITHER_GAMMA, false, indices);
    image_free(&image);
    if (!rendered) {
      free(indices);
      return false;
    }
  } else
#endif
  {
    if (scaling) {
      logMemory("before resize");
      if (!resize_image(&image, layout.width, layout.height)) {
        free(indices);
        return false;
      }
    }
    LOG.printf("[render] %s Floyd-Steinberg dither, %lu pixels\n",
               COLOR_MODE_NAME, static_cast<unsigned long>(pixelCount));
    if (!dither_image(image.pixels, layout.width, layout.height, PANEL_PALETTE,
                      DITHER_FS, config::DITHER_GAMMA, false, indices)) {
      free(indices);
      return false;
    }
    image_free(&image);
  }

  epaper.fillSprite(PANEL_WHITE);
#if RETERMINAL_MODEL == 1005
  // TFT_eSPI drawBitmap treats a set bit as the foreground color. PAL_BW
  // uses index 0 for black, so pack black pixels as set bits.
  const size_t packedBytes =
      static_cast<size_t>((layout.width + 7) / 8) * layout.height;
  uint8_t* packed = static_cast<uint8_t*>(ps_malloc(packedBytes));
  if (!packed) packed = static_cast<uint8_t*>(malloc(packedBytes));
  if (!packed) {
    free(indices);
    return false;
  }
  pack_1bpp_msb(indices, packed, layout.width, layout.height, true);
  free(indices);
  epaper.drawBitmap(layout.x, layout.y, packed, layout.width, layout.height,
                    PANEL_BLACK, PANEL_WHITE);
  free(packed);
#else
  xkcd_index::pack4bppInPlace(indices, layout.width, layout.height);
  epaper.pushImage(layout.x, layout.y, layout.width, layout.height,
                   reinterpret_cast<uint16_t*>(indices));
  free(indices);
#endif

  epaper.fillRect(0, 0, panelWidth(), headerBottom(), PANEL_WHITE);
  text_render::fillStatusBackground(
      epaper, layout.footerDividerY, panelHeight() - layout.footerDividerY,
      panelWidth(), panelHeight(), PANEL_STATUS_BACKGROUND,
      PANEL_STATUS_DITHERED, PANEL_STATUS_DITHER_COLOR,
      PANEL_STATUS_DITHER_THRESHOLD);
  epaper.drawFastHLine(config::CONTENT_MARGIN_X, headerBottom() - 1,
                       panelWidth() - 2 * config::CONTENT_MARGIN_X,
                       PANEL_BLACK);
  epaper.drawFastHLine(config::CONTENT_MARGIN_X, layout.footerDividerY,
                       panelWidth() - 2 * config::CONTENT_MARGIN_X,
                       PANEL_BLACK);

  // Render the footer first: calculateLayout left the footer smooth
  // font loaded, so this reuses it (no SD reload).  Loading the title
  // font afterwards costs one load instead of two.
  const smooth_fonts::Selection footerFont = selectFooterFont();
  if (PANEL_STATUS_DITHERED &&
      footerFont == smooth_fonts::Selection::GfxFallback) {
    // TFT_eSPI's bgfill flag applies only to smooth fonts. GFX FreeFont
    // fallbacks still fill their full bounding box whenever fg != bg, so
    // use the one-colour overload to make the fallback truly transparent.
    epaper.setTextColor(PANEL_BLACK);
  } else {
    epaper.setTextColor(PANEL_BLACK, PANEL_STATUS_BACKGROUND,
                        !PANEL_STATUS_DITHERED);
  }
  epaper.setTextDatum(MC_DATUM);
  int footerY =
      (layout.footerDividerY + panelHeight()) / 2 -
      (layout.footerLineCount - 1) * layout.footerLineHeightPx / 2;
  const int footerYAdjust = smoothCenterYAdjust();
  for (int i = 0; i < layout.footerLineCount; ++i) {
    drawSelectedSmoothTextCentered(
        layout.footerLines[i], panelWidth() / 2, footerY + footerYAdjust);
    footerY += layout.footerLineHeightPx;
  }

  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  String heading;
  String compactSubheading;
  if (compactPortraitLayout()) {
    heading = "XKCD #" + String(comic.number);
    compactSubheading =
        quietSleepNotice ? "Sleeping until " + quiet_hours::endLabel()
                         : text_render::displayText(comic.title);
  } else {
    heading =
        quietSleepNotice
            ? "XKCD #" + String(comic.number) + " - sleeping until " +
                  quiet_hours::endLabel()
            : "XKCD #" + String(comic.number) + " - " + comic.title;
  }
  selectTitleFont();
  const int headingMaxWidth =
      compactPortraitLayout() ? panelWidth() - 240
                              : panelWidth() - config::ui(380);
  const String headingLabel = text_render::ellipsize(
      epaper, text_render::displayText(heading), headingMaxWidth, 1);
  drawSelectedSmoothTextCentered(
      headingLabel, panelWidth() / 2,
      (compactPortraitLayout() ? 20 : config::ui(24)) +
          smoothCenterYAdjust());
  if (!compactSubheading.isEmpty()) {
    const String subheadingLabel = text_render::ellipsize(
        epaper, compactSubheading, panelWidth() - config::ui(24), 1);
    drawSelectedSmoothTextCentered(
        subheadingLabel, panelWidth() / 2, 50 + smoothCenterYAdjust());
  }
  smoothFontManager.selectGfx(nullptr);
  epaper.setTextFont(2);

  // Publication date: bottom-right of the image area, just above the
  // footer divider, in a compact sans font tuned per device so it reads
  // as a small annotation rather than competing with the battery
  // percentage or comic art.
  const String publishedDate = formatComicDate(comic);
  if (!publishedDate.isEmpty()) {
    selectComicDateFont();
    epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
    epaper.setTextDatum(BR_DATUM);
    epaper.drawString(publishedDate,
                      panelWidth() - config::CONTENT_MARGIN_X,
                      layout.footerDividerY - config::ui(2), 1);
    smoothFontManager.selectGfx(nullptr);
    epaper.setTextFont(2);
  }

  String refreshTime;
  if (local_time::clockIsValid()) {
    struct tm now = {};
    if (local_time::localClock(now)) {
      char buf[20];
      strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M", &now);
      refreshTime = buf;
    }
  }
  drawBadges(PANEL_WHITE, true,
             refreshTime.isEmpty() ? nullptr : &refreshTime);

  LOG.println("[render] refreshing panel");
  updatePanel();
  LOG.println("[render] complete");
  return true;
}

// NTP sync helpers now live in common/include/ntp_sync.h. The wrapper below
void powerDownAndSleep(uint64_t sleepSeconds = xkcd_config::runtime::sleepSeconds()) {
  wifi_sta::disable();
  // Close the log file before SD.end() so its FAT/directory update
  // hits disk cleanly. Safe to call unconditionally -- no-ops when no
  // sink is attached.
  appLog.detachSdSink();
  if (sdReady) SD.end();
#if RETERMINAL_MODEL == 1005
  epaper.getSPIinstance().end();
  pinMode(board::PIN_SD_CS, INPUT);
  pinMode(board::PIN_SD_SCK, INPUT);
  pinMode(board::PIN_SD_MOSI, INPUT);
  pinMode(board::PIN_SD_MISO, INPUT);
  peripheral_power::disableSd();
#endif
  epaper_setup::shutdownPanelPower();
  peripheral_power::disable();
  if (PIN_BATTERY_ENABLE >= 0) {
    pinMode(PIN_BATTERY_ENABLE, OUTPUT);
    digitalWrite(PIN_BATTERY_ENABLE, LOW);
  }

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const uint32_t releaseStarted = millis();
  while ((!digitalRead(PIN_BUTTON_GREEN) || !digitalRead(PIN_BUTTON_RIGHT) ||
          !digitalRead(PIN_BUTTON_LEFT)) &&
         millis() - releaseStarted < 2000) {
    delay(10);
  }

  const int wakePins[] = {
      PIN_BUTTON_GREEN, PIN_BUTTON_RIGHT, PIN_BUTTON_LEFT};
  bool rtcPinsReady = true;
  for (const int pin : wakePins) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    rtc_gpio_hold_dis(gpio);
    rtcPinsReady =
        rtc_gpio_init(gpio) == ESP_OK &&
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK &&
        rtc_gpio_pullup_en(gpio) == ESP_OK &&
        rtc_gpio_pulldown_dis(gpio) == ESP_OK &&
        rtcPinsReady;
  }

  const uint64_t wakeMask =
      (1ULL << PIN_BUTTON_GREEN) | (1ULL << PIN_BUTTON_RIGHT) |
      (1ULL << PIN_BUTTON_LEFT);
  const esp_err_t buttonWakeResult =
      rtcPinsReady
          ? esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  const esp_err_t timerWakeResult =
      esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  LOG.printf("[sleep] wake config: buttons=%s timer=%s levels=%d/%d/%d\n",
             esp_err_to_name(buttonWakeResult),
             esp_err_to_name(timerWakeResult),
             digitalRead(PIN_BUTTON_GREEN),
             digitalRead(PIN_BUTTON_RIGHT),
             digitalRead(PIN_BUTTON_LEFT));
  LOG.printf("[sleep] %llu seconds; GPIO%d/%d/%d wake enabled\n",
             static_cast<unsigned long long>(sleepSeconds),
             PIN_BUTTON_GREEN, PIN_BUTTON_RIGHT, PIN_BUTTON_LEFT);
  if (buttonWakeResult != ESP_OK && timerWakeResult != ESP_OK) {
    LOG.println("[sleep] no wake source could be configured; restarting");
    LOG.flush();
    delay(250);
    ESP.restart();
  }
  LOG.flush();
  delay(50);
  hardware::setStatusLed(false);
  power_latch::holdDuringDeepSleep();
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  power_latch::holdOn();
  hardware::setStatusLed(true);
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  const uint64_t wakePins = wakeCause == ESP_SLEEP_WAKEUP_EXT1
                                ? esp_sleep_get_ext1_wakeup_status()
                                : 0;
  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const bool greenWokeDevice =
      (wakePins & (1ULL << PIN_BUTTON_GREEN)) != 0;
  const bool rightWokeDevice =
      (wakePins & (1ULL << PIN_BUTTON_RIGHT)) != 0;
  const bool leftWokeDevice =
      (wakePins & (1ULL << PIN_BUTTON_LEFT)) != 0;
  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  const bool buttonWake = wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool timerWake = wakeCause == ESP_SLEEP_WAKEUP_TIMER;

  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  usbScreenCapture.begin(Serial1);

  // Load NVS-backed settings so every config::runtime accessor returns a
  // consistent value for the rest of this boot.
  xkcd_config::runtime::load();
  xkcd_wifi::load();

  // Unified primary-button boot gesture. Applies on both cold boot and
  // deep-sleep primary-button wakes. A first beep marks the press;
  // a second beep 5 s later marks the switch to screenshot:
  //   * Released before 1 s          -> accidental tap, ignored.
  //   * Released between 1 s and 5 s -> enter config portal.
  //   * Held past 5 s                -> capture a screenshot.
  //   * Not pressed                  -> normal boot.
  // On cold boot we also trigger the portal automatically when no
  // Wi-Fi credentials are available (NVS + secrets.h both empty).
  enum class GreenGesture { None, PortalRequest, ScreenshotRequest };
  GreenGesture gesture = GreenGesture::None;
  const bool greenPressedAtBoot =
      (coldBoot && !digitalRead(PIN_BUTTON_GREEN)) || greenWokeDevice;
  if (greenPressedAtBoot) {
    hardware::beep();  // first beep: gesture registered.
    const uint32_t gestureStartMs = millis();
    constexpr uint32_t kPortalMinMs = 1000;
    constexpr uint32_t kPortalDecisionMs = 5000;
    bool releasedBeforeDecision = false;
    uint32_t releasedAtMs = 0;
    while (millis() - gestureStartMs < kPortalDecisionMs) {
      if (digitalRead(PIN_BUTTON_GREEN)) {
        releasedBeforeDecision = true;
        releasedAtMs = millis() - gestureStartMs;
        break;
      }
      delay(5);
    }
    if (releasedBeforeDecision) {
      if (releasedAtMs < kPortalMinMs) {
        LOG.printf("[gesture] %s released after %u ms (< %u ms min) -> ignored\n",
                   PRIMARY_BUTTON_LABEL, static_cast<unsigned>(releasedAtMs),
                   static_cast<unsigned>(kPortalMinMs));
        gesture = GreenGesture::None;
      } else {
        LOG.printf("[gesture] %s released after %u ms -> portal\n",
                   PRIMARY_BUTTON_LABEL, static_cast<unsigned>(releasedAtMs));
        gesture = GreenGesture::PortalRequest;
      }
    } else {
      LOG.printf("[gesture] %s still held at %u ms -> screenshot\n",
                 PRIMARY_BUTTON_LABEL,
                 static_cast<unsigned>(kPortalDecisionMs));
      hardware::beep();  // second beep: past portal window, screenshot armed.
      gesture = GreenGesture::ScreenshotRequest;
      uint32_t releaseStarted = 0;
      const uint32_t releaseWaitDeadlineMs =
          gestureStartMs + config::SCREENSHOT_LONG_PRESS_MS + 3000;
      while (millis() < releaseWaitDeadlineMs) {
        if (!digitalRead(PIN_BUTTON_GREEN)) {
          releaseStarted = 0;
        } else if (releaseStarted == 0) {
          releaseStarted = millis();
        } else if (millis() - releaseStarted >=
                   config::BUTTON_RELEASE_DEBOUNCE_MS) {
          break;
        }
        delay(5);
      }
    }
  }
  const bool wifiUnconfigured = coldBoot && !xkcd_wifi::haveCredentials();
  const bool portalRequested =
      gesture == GreenGesture::PortalRequest || wifiUnconfigured;
  screenshotRequested = gesture == GreenGesture::ScreenshotRequest;

  if (portalRequested) {
    LOG.printf("[portal] entering config portal (no_wifi=%d gesture=%s)\n",
               wifiUnconfigured,
               gesture == GreenGesture::PortalRequest ? "button-hold" : "auto");
    // Restore the wall clock from the battery-backed PCF8563 before we
    // start the portal. NTP isn't available here (Wi-Fi is almost
    // certainly unconfigured - that's why we're in the portal), and
    // the ESP32's own RTC drifts several percent per sleep cycle. The
    // main app path does this too, further down; the portal branch
    // returns via ESP.restart() so it never reaches that call.
    rtc_sync::restoreSystemClock();
    // Bring the panel up FIRST so the QR splash renders before we start
    // the Wi-Fi AP + web server. If the panel refresh ever hangs, the
    // AP won't be advertising anyway, so this gives a clearer failure
    // mode than "AP up, panel blank".
    LOG.println("[portal] panel: begin");
    beginPanel();
#if RETERMINAL_MODEL == 1001
    epaper.initGrayMode(GRAY_LEVEL4);
    const GFXfont* titleFont    = &FreeSansBold18pt7b;
    const GFXfont* subtitleFont = &FreeSans12pt7b;
    const GFXfont* captionFont  = &FreeSansBold9pt7b;
    const GFXfont* detailFont   = &FreeSans9pt7b;
#elif RETERMINAL_MODEL == 1003
    epaper.initGrayMode(GRAY_LEVEL16);
    const GFXfont* titleFont    = &FreeSansBold24pt7b;
    const GFXfont* subtitleFont = &FreeSans18pt7b;
    const GFXfont* captionFont  = &FreeSansBold12pt7b;
    const GFXfont* detailFont   = &FreeSans12pt7b;
#else
    const GFXfont* titleFont    = &FreeSansBold18pt7b;
    const GFXfont* subtitleFont = &FreeSans12pt7b;
    const GFXfont* captionFont  = &FreeSansBold9pt7b;
    const GFXfont* detailFont   = &FreeSans9pt7b;
#endif
    LOG.println("[portal] panel: grayMode initialised");
    // Defer mounting SD until after the panel refresh so SD.begin() is the
    // last configurator to touch the shared SPI bus. epaper_setup::begin()
    // has already applied the E1001 MISO fix needed for Gray4 transfers.
    bool portalSdReady = false;

    config_portal::Config portalCfg;
    portalCfg.wifiSchema = &config_portal::kWifiSchema;
    portalCfg.appSchema = &xkcd_config::kSchema;
    portalCfg.appName = "xkcd viewer";
    portalCfg.repositoryUrl = repo_qr::kUrl;
    // Persistent per-device SoftAP password: generated on first boot,
    // stored in NVS, reused thereafter. Encrypts the portal as WPA2-PSK
    // without any compile-time shared secret. The password is embedded
    // in the QR splash so phones autofill it.
    portalCfg.useAutoApPassword = true;
    // Nav tab that jumps to the SD browser served by sd_web_portal.
    static const config_portal::NavTab kExtraTabs[] = {
        {"SD", "/browse?path=%2F", "sd"},
    };
    portalCfg.extraTabs = kExtraTabs;
    portalCfg.extraTabCount = sizeof(kExtraTabs) / sizeof(kExtraTabs[0]);
    // Feed the current-resolved credentials back into the portal so the
    // Wi-Fi form shows what the device would connect to on the next
    // boot. The portal will redact the password with the __saved__
    // sentinel before serving it over HTTP.
    portalCfg.wifiFallback = [](const char* key) -> String {
      if (strcmp(key, "ssid") == 0) return String(xkcd_wifi::ssid());
      if (strcmp(key, "password") == 0) return String(xkcd_wifi::password());
      return String();
    };
    portalCfg.sdFormat = [](String& error) -> bool {
      return sd_card::formatCard(epaper.getSPIinstance(), config::CACHE_DIR, error);
    };
    portalCfg.sdFormatWarning = "the cached comic archive and index";
    if (config_portal::begin(portalCfg)) {
      // Wire the SD browser routes onto config_portal's WebServer.
      // Browser-only config: no photo uploader.
      sd_web_portal::Config sdCfg;
      static String s_headerHtml;
      s_headerHtml = config_portal::renderHeaderHtml(portalCfg, "sd");
      sdCfg.headerHtml = s_headerHtml.c_str();
      if (WebServer* server = config_portal::webServer()) {
        sd_web_portal::attachRoutes(*server, sdCfg);
      } else {
        LOG.println("[portal] webServer() null; SD tab disabled");
      }
      config_portal::ui::RenderInfo info;
      info.modelLabel = MODEL_NAME;
      info.title = "Configure";
      info.tagline = "Join the AP to set Wi-Fi + settings";
      info.ssid = config_portal::currentSsid();
      info.wifiPassword = config_portal::currentApPassword();
      info.url = String("http://") + config_portal::currentIp().toString();
      info.macAddress = wifi_sta::stationMacAddress();
      info.firmwareVersion = board::FIRMWARE_VERSION;
      info.wifiPayload = config_portal::wifiQrPayload(
          info.ssid,
          info.wifiPassword.length() ? info.wifiPassword.c_str() : nullptr);
      info.urlPayload = config_portal::urlQrPayload(
          config_portal::currentIp(), config_portal::currentPort(), "/wifi");
      info.footerHint = PORTAL_EXIT_HINT;
      info.fonts.titleFont = titleFont;
      info.fonts.subtitleFont = subtitleFont;
      info.fonts.captionFont = captionFont;
      info.fonts.detailFont = detailFont;
      LOG.println("[portal] rendering QR splash");
      const uint32_t drawStart = millis();
      config_portal::ui::renderPortalScreen<EPaper>(
          epaper, panelWidth(), panelHeight(), PANEL_BLACK,
          PANEL_WHITE, info);
      LOG.printf("[portal] splash drawn in %u ms; committing to panel\n",
                 static_cast<unsigned>(millis() - drawStart));
      const uint32_t updateStart = millis();
      panel_watchdog::refresh(epaper);
      framebufferReady = true;
      LOG.printf("[portal] panel refresh complete in %u ms\n",
                 static_cast<unsigned>(millis() - updateStart));

      // Now that the panel is done, mount SD so SD.begin() is the last
      // caller to configure the shared SPI bus. Mounting earlier left
      // SD in a state that epaper.update() would then break, causing
      // every /browse to fail with 'bus reset ... failed'.
      portalSdReady =
          sd_card::mount(epaper.getSPIinstance(), config::CACHE_DIR);
      if (!portalSdReady) {
        LOG.println("[portal] SD mount failed; browser tab will be empty");
      }

      uint32_t lastHeartbeatMs = millis();
      uint32_t greenLowSinceMs = 0;
      // Drop any TWDT subscription the panel refresh above installed
      // before entering the infinite HTTP loop - it never returns to
      // Arduino's loop() where the WDT would otherwise be fed. No-op
      // outside E1003.
      panel_watchdog::disarmCurrentTask();
      while (!config_portal::rebootRequested() &&
             !sd_web_portal::exitRequested()) {
        config_portal::loop();
        usbScreenCapture.poll(epaper, panelWidth(), panelHeight());
        const uint32_t nowMs = millis();
        // Primary button in the portal = reboot the device. Convenient exit
        // once you've saved settings on your phone, matching the "Reboot"
        // button on /reset. Debounced at 50 ms.
        if (!digitalRead(PIN_BUTTON_GREEN)) {
          if (greenLowSinceMs == 0) {
            greenLowSinceMs = nowMs;
          } else if (nowMs - greenLowSinceMs >= 50) {
            LOG.printf("[portal] %s button pressed -> reboot\n",
                       PRIMARY_BUTTON_LABEL);
            hardware::beep();
            break;
          }
        } else {
          greenLowSinceMs = 0;
        }
        if (nowMs - lastHeartbeatMs >= 15000) {
          const String& pass = config_portal::currentApPassword();
          if (pass.length()) {
            LOG.printf("[portal] waiting for client on http://%s "
                       "(SSID \"%s\" pass \"%s\")\n",
                       config_portal::currentIp().toString().c_str(),
                       config_portal::currentSsid().c_str(),
                       pass.c_str());
          } else {
            LOG.printf("[portal] waiting for client on http://%s (SSID \"%s\")\n",
                       config_portal::currentIp().toString().c_str(),
                       config_portal::currentSsid().c_str());
          }
          lastHeartbeatMs = nowMs;
        }
        delay(5);
      }
      // If the "Reboot to viewer" button was clicked, give the HTTP
      // server ~400 ms to flush the response before we tear the AP
      // down. Otherwise the browser sees a hung request.
      if (sd_web_portal::exitRequested()) {
        LOG.println("[portal] web exit requested -> reboot");
        const uint32_t drainStart = millis();
        while (millis() - drainStart < 400) {
          config_portal::loop();
          delay(10);
        }
      }
      sd_web_portal::end();
      config_portal::end();
    }
    if (portalSdReady) SD.end();
    LOG.println("[portal] rebooting to apply configuration");
    delay(200);
    ESP.restart();
  }

  local_time::configureTimezone(xkcd_config::runtime::timezone());
  quiet_hours::configure({xkcd_config::runtime::quietHoursEnabled(),
                          xkcd_config::runtime::quietStartHour(),
                          xkcd_config::runtime::quietStartMinute(),
                          xkcd_config::runtime::quietEndHour(),
                          xkcd_config::runtime::quietEndMinute()});
  if (app_logic::startupBeepRequired(coldBoot, buttonWake)) {
    // Acknowledge cold boots and button wakes immediately. The unified
    // green-button gesture handler above has already decided whether
    // this boot is a portal request or a screenshot request.
    hardware::beep();
  }

  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone XKCD / %s\n", MODEL_NAME, COLOR_MODE_NAME);
  LOG.printf(" Firmware v%s\n", board::FIRMWARE_VERSION);
  LOG.println("============================================");

  LOG.printf("[boot] wake cause=%d pins=0x%llx, PSRAM=%luK, "
             "GPIO%d=%s GPIO%d=%s GPIO%d=%s\n",
             wakeCause, static_cast<unsigned long long>(wakePins),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024),
             PIN_BUTTON_GREEN,
             greenWokeDevice
                 ? (screenshotRequested ? "long-press" : "short-press")
                 : "idle",
             PIN_BUTTON_RIGHT,
             rightWokeDevice ? "wake" : "idle",
             PIN_BUTTON_LEFT,
             leftWokeDevice ? "wake" : "idle");
  if (screenshotRequested) {
    LOG.printf("[screenshot] %s-button long press requested export\n",
               PRIMARY_BUTTON_LABEL);
  }

  const time_t startupTime = time(nullptr);
  const bool schedulingClockSuspicious =
      !local_time::clockIsValid() ||
      (lastNtpSyncEpoch > 0 && startupTime < lastNtpSyncEpoch) ||
      (lastArchiveRefreshEpoch > 0 &&
       startupTime < lastArchiveRefreshEpoch);
  // The ESP32's deep-sleep RTC drifts several percent (internal 150 kHz RC
  // oscillator), so read the PCF8563 on every wake and reset the system
  // clock from it. PCF8563 is battery-backed and stays accurate to ~20 ppm.
  const bool hardwareRtcCheckedEarly = true;
  if (schedulingClockSuspicious) {
    LOG.println("[rtc] schedule clock is invalid or behind retained state; "
                "trying PCF8563");
  }
  rtc_sync::restoreSystemClock();

  bool wakeEventLogged = wake_report::logWakeEvent(wakeCause, wakePins, false);
  const bool ntpDue = local_time::refreshDue(coldBoot, lastNtpSyncEpoch, config::NTP_REFRESH_SECONDS);
  struct tm localTime = {};
  {
    const bool haveLocalClock = local_time::localClock(localTime);
    const bool quietNow = haveLocalClock && quiet_hours::active(localTime);
    // archiveRefreshDue() needs a valid clock; guard the read so we don't
    // treat a bogus retained epoch as "maintenance overdue".
    const bool archiveDuePreSync =
        sdReady && local_time::clockIsValid() && archiveRefreshDue();
    if (app_logic::suppressPreSyncForQuietHours(
            coldBoot, ntpDue, buttonWake, haveLocalClock, quietNow,
            archiveDuePreSync)) {
      const uint64_t quietSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
      LOG.printf("[quiet] refresh suppressed; sleeping until %s\n",
                 quiet_hours::endLabel().c_str());
      powerDownAndSleep(quietSleepSeconds);
      return;
    }
  }

  randomSeed(esp_random());
  sensors::readAll(PIN_BATTERY_ENABLE, PIN_BATTERY_ADC, sht4, config::SENSOR_READ_ATTEMPTS, config::SENSOR_RETRY_DELAY_MS, sensorReadings);

  if (coldBoot && !hardwareRtcCheckedEarly) {
    pcf8563::Reading storedRtc;
    rtc_sync::readAndLog(storedRtc);
  }

  beginPanel();
  if (low_battery::shouldWarn(xkcd_config::runtime::lowBatteryWarn(),
                              sensorReadings.batteryValid,
                              sensorReadings.externalPower,
                              sensorReadings.batteryPct)) {
    LOG.printf("[battery] %d%% (%.3fV) below %d%% -- rendering recharge screen\n",
               sensorReadings.batteryPct, sensorReadings.batteryVoltage,
               low_battery::kThresholdPct);
    renderStatus("Please recharge",
                 "Plug in USB-C, then press " +
                     String(PRIMARY_BUTTON_LABEL) + " to continue.",
                 "Battery low");
    powerDownAndSleep(xkcd_config::runtime::sleepSeconds());
    return;
  }
  sdReady = sd_card::mount(epaper.getSPIinstance(), config::CACHE_DIR);
  if (sdReady && xkcd_config::runtime::logToSd()) {
    log_sd_sink::install(appLog);
  }
  // SD-driven firmware update. See common/src/sd_ota.cpp for the shared
  // implementation; if anything fails the current firmware is untouched.
  if (sdReady && sd_ota::hasUpdate()) {
    renderStatus("Updating firmware",
                 "Please wait, do not power off the device.",
                 "Firmware update");
    const auto otaResult = sd_ota::apply();
    if (otaResult == sd_ota::Result::Applied) {
      delay(1000);
      ESP.restart();
    }
    renderStatus("Firmware update failed",
                 "The old firmware is still running. See serial log for details.",
                 "Firmware update");
    delay(3000);
  }
  if (screenshotRequested && !sdReady) {
    LOG.println("[screenshot] request ignored: SD card is unavailable");
    screenshotRequested = false;
  }

  if (sdReady && !xkcd_index::load()) {
    LOG.printf("[cache] %s; run tools/preload_sd.py to rebuild the manifest\n",
               coldBoot ? "cold boot found no usable index"
                        : "wake found no usable index");
    // rebuild() cannot reconstruct title/alt/url from image files alone;
    // the app runs in network-only mode until the manifest exists.
  }
  const uint32_t cachedComicCount = sdReady ? xkcd_index::count() : 0;
  cacheStatsAvailable = sdReady;
  cachedComicCountForDisplay = cachedComicCount;
  if (sdReady) {
    int cachedLatest = 0;
    getLatestNumber(false, cachedLatest);
  }
  const bool cacheOnly = app_logic::cacheOnly(
      sdReady, cachedComicCount, config::MIN_COMICS_FOR_CACHE_ONLY);

  // Archive maintenance is deliberately limited to normal timer wakes. Cold
  // boots only report cache progress. Once the cache has a useful local pool,
  // every kind of wake selects from it and button wakes do not start Wi-Fi.
  const bool networkPlanned =
      app_logic::networkPlanned(cacheOnly, ntpDue);
  if (sdReady && coldBoot) {
    LOG.printf("[cache] %lu complete comics available; display mode=%s\n",
               static_cast<unsigned long>(cachedComicCount),
               cacheOnly ? "local only" : "live until cache reaches threshold");
  }
  const bool compactConnectionStatus =
      config::MODEL == 1005 && !xkcd_config::runtime::isLandscape();
  String connectionDetail;
  if (sdReady) {
    connectionDetail = String(cachedComicCount) +
                       (compactConnectionStatus ? " cached"
                                                : " comics cached");
    if (ntpDue) {
      connectionDetail += compactConnectionStatus
                              ? " - syncing clock"
                              : " - synchronizing clock";
    } else if (!cacheOnly) {
      connectionDetail += compactConnectionStatus
                              ? " - caching comics"
                              : " - building local cache";
    }
  } else {
    connectionDetail = compactConnectionStatus
                           ? "No cache - downloading"
                           : "No SD cache - downloading live";
  }

  const bool showConnectionStatus = coldBoot && networkPlanned;
  const String stationMac = wifi_sta::stationMacAddress();
  LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());
  // Firmware version appended so a device on the connecting splash
  // shows both its MAC (for identification on the network) and the
  // running build (for confirming an SD-driven update landed).
  const String macAndVersion =
      compactConnectionStatus
          ? String("MAC ") + stationMac + "  FW " + board::FIRMWARE_VERSION
          : String("MAC: ") + stationMac + "  Firmware: " +
                board::FIRMWARE_VERSION;

  // Cold-boot "Connecting to Wi-Fi" splash. On gray panels this is pushed
  // before initGrayMode() for a faster monochrome refresh; six-color panels
  // use their native controller mode. renderStatus() completes the update
  // before Wi-Fi connection blocks.
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(xkcd_wifi::ssid()), connectionDetail,
                 "",
                 configurationGestureHint(false),
                 macAndVersion, true);
  }
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

  bool networkAvailable = false;
  if (networkPlanned) {
    networkAvailable = wifi_sta::connectStation(xkcd_wifi::ssid(), xkcd_wifi::password(), config::WIFI_TIMEOUT_MS, nullptr, networkOperationShouldStop).connected;
  } else {
    LOG.println("[wifi] skipped; using the local XKCD cache");
  }
  const bool ntpSynchronized =
      networkAvailable && ntpDue && ntp::synchronizeAndPersist(xkcd_config::runtime::timezone(), xkcd_config::runtime::ntpPrimary(), xkcd_config::runtime::ntpSecondary(), config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
  if (ntpDue && !ntpSynchronized && !coldBoot) {
    LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
    rtc_sync::restoreSystemClock();
  }
  local_time::configureTimezone(xkcd_config::runtime::timezone());
  quiet_hours::configure({xkcd_config::runtime::quietHoursEnabled(),
                          xkcd_config::runtime::quietStartHour(),
                          xkcd_config::runtime::quietStartMinute(),
                          xkcd_config::runtime::quietEndHour(),
                          xkcd_config::runtime::quietEndMinute()});
  if (!wakeEventLogged) {
    wake_report::logWakeEvent(wakeCause, wakePins, true);
  }

  // Establish or repair the six-hour baseline only after NTP/PCF recovery.
  // This prevents an invalid pre-sync clock from latching maintenance due.
  const time_t scheduleTime = time(nullptr);
  const time_t normalizedArchiveBaseline =
      static_cast<time_t>(app_logic::normalizeRefreshBaseline(
          coldBoot, local_time::clockIsValid(), scheduleTime,
          lastArchiveRefreshEpoch));
  if (normalizedArchiveBaseline != lastArchiveRefreshEpoch) {
    lastArchiveRefreshEpoch = normalizedArchiveBaseline;
    LOG.println("[cache] archive maintenance scheduled in six hours");
  }
  const bool archiveDue = app_logic::archiveMaintenanceDue(
      sdReady, timerWake, local_time::clockIsValid() && archiveRefreshDue());

  bool inQuietHours = false;
  {
    const bool haveLocalClock = local_time::localClock(localTime);
    const bool quietNow = haveLocalClock && quiet_hours::active(localTime);
    if (app_logic::suppressPostSyncForQuietHours(
            coldBoot, buttonWake, haveLocalClock, quietNow, archiveDue)) {
      const uint64_t quietSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
      LOG.printf("[quiet] refresh suppressed after clock sync; sleeping until %s\n",
                 quiet_hours::endLabel().c_str());
      wifi_sta::disable();
      powerDownAndSleep(quietSleepSeconds);
      return;
    }
    if (app_logic::maintainSilentlyInQuietHours(quietNow, archiveDue)) {
      inQuietHours = true;
      LOG.println("[quiet] running archive maintenance only; display suppressed");
    }
  }

  Comic comic;
  RgbImage image;
  ImageLayout layout;
  bool displayed = false;
  bool acquired =
      sdReady
          ? acquireComic(cacheOnly ? false : networkAvailable,
                         comic, image, layout)
          : acquireComicWithoutSd(networkAvailable, comic, image, layout);

  if (sdReady && networkAvailable && !cacheOnly) {
    // acquireComic may have added a new entry to the in-memory manifest via
    // getComic; flush it so the next boot sees it. Skip the integrity
    // rebuild (which would SD.exists every entry) since scheduled
    // maintenance owns that pass.
    xkcd_index::persist();
    cachedComicCountForDisplay = xkcd_index::count();
  }

  // If we have SD but no usable comic yet, spend one Wi-Fi wake trying
  // to download something. Runs even in cache-only mode: a corrupt
  // manifest or bad SD is the exact case where the local pool is
  // unreadable but the network can save the refresh, and rendering
  // "no comic" is the worse failure. Cost is one extra radio-on cycle
  // per broken wake, which is bounded to once because a successful
  // download flips `acquired`.
  if (app_logic::liveRecoveryAllowed(sdReady, acquired, networkAvailable)) {
    LOG.println("[cache] no usable local comic; trying one live refresh");
    networkAvailable = wifi_sta::connectStation(xkcd_wifi::ssid(), xkcd_wifi::password(), config::WIFI_TIMEOUT_MS, nullptr, networkOperationShouldStop).connected;
    if (networkAvailable) {
      if (local_time::refreshDue(coldBoot, lastNtpSyncEpoch, config::NTP_REFRESH_SECONDS)) ntp::synchronizeAndPersist(xkcd_config::runtime::timezone(), xkcd_config::runtime::ntpPrimary(), xkcd_config::runtime::ntpSecondary(), config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
      acquired = acquireComic(true, comic, image, layout);
    }
  }

  // PNG decoding is complete at this point. Keep the radio off during
  // dithering and the comparatively slow e-paper update.
  wifi_sta::disable();

  uint64_t nextSleepSeconds = xkcd_config::runtime::sleepSeconds();
  if (local_time::localClock(localTime)) {
    if (quiet_hours::active(localTime) ||
        quiet_hours::nextWakeFallsInside(localTime, xkcd_config::runtime::sleepSeconds())) {
      quietSleepNotice = true;
      nextSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
      LOG.printf("[quiet] this is the final refresh; sleeping until %s\n",
                 quiet_hours::endLabel().c_str());
    }
  }

  if (acquired && !inQuietHours) {
    displayed = renderComic(comic, image, layout);
  }
  image_free(&image);

  if (!displayed && !inQuietHours) {
    const String reason = sdReady
                              ? "No usable cached or downloadable comic"
                              : "Live download failed; check Wi-Fi or insert an SD card";
    const uint64_t retryMinutes = (nextSleepSeconds + 59ULL) / 60ULL;
    const String detail =
        "Retrying in " + String(static_cast<unsigned long>(retryMinutes)) +
        " minutes. Press " + String(PRIMARY_BUTTON_LABEL) +
        " to retry now.";
    const String reconfigureHint = configurationGestureHint(true);
    renderStatus("XKCD refresh failed", detail, reason, reconfigureHint);
  }

  // The panel retains the newly rendered frame without power. Do scheduled
  // cache maintenance only now, so downloads never delay or replace the UI.
  if (archiveDue) {
    LOG.println("[cache] starting scheduled maintenance after panel refresh");
    armMaintenanceButtonCancellation();
    networkOperationDeadlineMs =
        millis() + config::ARCHIVE_MAINTENANCE_DEADLINE_MS;
    if (wifi_sta::connectStation(xkcd_wifi::ssid(), xkcd_wifi::password(), config::WIFI_TIMEOUT_MS, nullptr, networkOperationShouldStop).connected) {
      refreshArchiveCache();
      if (local_time::clockIsValid()) lastArchiveRefreshEpoch = time(nullptr);
    } else if (maintenanceCancellationRequested()) {
      LOG.println("[cache] scheduled maintenance cancelled by button");
    } else {
      LOG.println("[cache] scheduled maintenance deferred: Wi-Fi unavailable");
    }
    const bool maintenanceWasCancelled = maintenanceCancellationRequested();
    if (maintenanceWasCancelled && local_time::clockIsValid()) {
      // A user cancellation is intentional; do not retry maintenance on every
      // 15-minute timer wake.
      lastArchiveRefreshEpoch = time(nullptr);
    }
    disarmMaintenanceButtonCancellation();
    wifi_sta::disable();
    networkOperationDeadlineMs = 0;

    if (maintenanceWasCancelled) {
      LOG.println("[button] maintenance stopped; displaying another cached comic");
      if (!inQuietHours) {
        hardware::beep();
        Comic replacement;
        RgbImage replacementImage;
        ImageLayout replacementLayout;
        if (acquireComic(false, replacement, replacementImage,
                         replacementLayout)) {
          renderComic(replacement, replacementImage, replacementLayout);
        } else {
          LOG.println("[button] no replacement cached comic was usable; "
                      "keeping the current frame");
        }
        image_free(&replacementImage);
      } else {
        LOG.println("[quiet] not switching comics during quiet hours");
      }
    }
  }

  // Reached the successful sleep path: mark the running image valid so
  // ESP-IDF's rollback watchdog does not revert an SD-OTA install on the
  // next boot. Safe no-op if this isn't a pending-verify image.
  sd_ota::confirmRunningImage();
  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  if (framebufferReady) {
    usbScreenCapture.poll(epaper, panelWidth(), panelHeight());
  } else {
    usbScreenCapture.pollUnavailable();
  }
  delay(1000);
}
