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
#include "hardware.h"
#include "local_time.h"
#include "wake_report.h"
#include "rtc_sync.h"
#include "wifi_sta.h"
#include "climate_sensor.h"
#include "sd_card.h"
#include "net_http.h"
#include "log_sd_sink.h"
#include "text_render.h"
#include "xkcd_index.h"
#include "xkcd_cache_schema.h"
#include "quiet_hours.h"
#include "sensors.h"
#include "dither.h"
#include "image_loader.h"
#include "pcf8563_utc.h"
#include "screenshot_bmp.h"
#include "timestamped_logger.h"

// HTTPS, HTTPClient and the SD filesystem have a fairly deep combined call
// chain. E1003 exposed the default 8 KiB Arduino loop stack limit when the SD
// cache path added its download buffer to that stack.
SET_LOOP_TASK_STACK_SIZE(16U * 1024U);

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check common/include/driver.h"
#endif

TimestampedLogger appLog(Serial1);
// LOG is provided by app_logger.h; the definition above lives at namespace
// scope so shared translation units (screenshot_bmp.h) can extern-link.

namespace {

using namespace ::board;
constexpr int PIN_BUTTON_GREEN = 3;
constexpr int PIN_BUTTON_RIGHT = 4;
constexpr int PIN_BUTTON_LEFT = 5;

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
#endif

EPaper epaper;
Adafruit_SHT4x sht4;

bool sdReady = false;
bool sdCacheWritable = true;
bool screenshotRequested = false;
sensors::Readings sensorReadings;

bool cacheStatsAvailable = false;
uint32_t cachedComicCountForDisplay = 0;
uint32_t totalComicCountForDisplay = 0;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
RTC_DATA_ATTR time_t lastArchiveRefreshEpoch = 0;
bool quietSleepNotice = false;
uint32_t networkOperationDeadlineMs = 0;
volatile uint8_t maintenanceButtonInterruptMask = 0;
bool maintenanceCancelled = false;

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
#else
constexpr const GFXfont* TITLE_FALLBACK_FONT = &FreeSansBold12pt7b;
constexpr const GFXfont* FOOTER_FALLBACK_FONT = &FreeSansBold9pt7b;
#endif

// Non-zero when a smooth font of that pixel size is currently loaded via
// TFT_eSPI::loadFont; zero when we're back on GFX fonts / built-ins.
static int g_currentSmoothSize = 0;
// Set to true when a .vlw load has already failed once so we don't
// re-check the SD or spam the log on every subsequent select*.
static bool g_smoothFontsUnavailable = false;

static bool smoothFontFileExists(int size) {
  if (!sdReady) return false;
  return sd_card::fileExists(String("/fonts/sans_bold_") + size + ".vlw");
}

static void unloadSmoothFontIfLoaded() {
  if (g_currentSmoothSize != 0) {
    epaper.unloadFont();
    g_currentSmoothSize = 0;
  }
}

// Install a GFX FreeFont (or the built-in font when passed nullptr),
// releasing any smooth-font resources first.  Callers that want to
// stay on the built-in font 2 after this should call setTextFont(2)
// themselves - matches the previous behavior.
static void applyGfxFont(const GFXfont* font) {
  unloadSmoothFontIfLoaded();
  epaper.setFreeFont(font);
}

// Install the smooth font at `size` (pixels).  Falls back to `fallback`
// (a GFX FreeFont at roughly the same visual size) when the SD/.vlw is
// unavailable or has already failed once this boot.  Loading is
// idempotent when the requested size is already active.
static void applySmoothFont(int size, const GFXfont* fallback) {
  if (g_smoothFontsUnavailable || !sdReady) {
    unloadSmoothFontIfLoaded();
    epaper.setFreeFont(fallback);
    return;
  }
  if (g_currentSmoothSize == size) return;
  if (g_currentSmoothSize != 0) {
    epaper.unloadFont();
    g_currentSmoothSize = 0;
  }
  if (!smoothFontFileExists(size)) {
    LOG.printf("[font] /fonts/sans_bold_%d.vlw probe failed; falling back to GFX font for this call\n",
               size);
    // Do NOT set g_smoothFontsUnavailable: a probe miss on the SPI SD is
    // often transient. Latching would kill Unicode for the whole boot.
    epaper.setFreeFont(fallback);
    return;
  }
  epaper.setFreeFont(nullptr);
  const uint32_t t0 = millis();
  // TFT_eSPI::loadFont builds "/" + name + ".vlw" internally, so pass
  // the subdir as part of the name to get "/fonts/sans_bold_XX.vlw".
  epaper.loadFont(String("fonts/sans_bold_") + size, SD);
  g_currentSmoothSize = size;
  LOG.printf("[font] loaded sans_bold_%d in %lu ms (yAdvance=%u ascent=%u descent=%u)\n",
             size, (unsigned long)(millis() - t0),
             (unsigned)epaper.gFont.yAdvance,
             (unsigned)epaper.gFont.ascent,
             (unsigned)epaper.gFont.descent);
}

void selectStatusFont() {
  applyGfxFont(
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
  applyGfxFont(
#if RETERMINAL_MODEL == 1003
      &FreeSansBold12pt7b
#elif RETERMINAL_MODEL == 1004
      &FreeSansBold9pt7b
#else
      &FreeSans9pt7b
#endif
  );
}

// TFT_eSPI's MC/ML/MR datums center the smooth font's yAdvance box on
// the requested y, but DejaVu Sans Bold's ascent (~28 at 30px) is
// much larger than its descent (~8), so the visual cap-center sits a
// few pixels above the box center.  This helper returns the y offset
// (in pixels) needed to align the cap-center with the caller's y.
// Returns 0 when a smooth font is not loaded so GFX callers are
// unaffected.
static int smoothCenterYAdjust() {
  if (g_currentSmoothSize == 0) return 0;
  const int yA = static_cast<int>(epaper.gFont.yAdvance);
  const int mA = static_cast<int>(epaper.gFont.maxAscent);
  const int a  = static_cast<int>(epaper.gFont.ascent);
  // Approximate cap-height as ascent * 0.78 (DejaVu Sans Bold).
  return (yA / 2) - mA + (a * 78 / 200);
}

void selectCacheStatsFont() {
#if RETERMINAL_MODEL == 1003
  applyGfxFont(&FreeSans9pt7b);
#elif RETERMINAL_MODEL == 1004
  applyGfxFont(&FreeSans9pt7b);
#else
  applyGfxFont(nullptr);
  epaper.setTextFont(2);
#endif
}

void selectTitleFont() {
  applySmoothFont(SMOOTH_FONT_TITLE_PX, TITLE_FALLBACK_FONT);
}

void selectFooterFont() {
  applySmoothFont(SMOOTH_FONT_FOOTER_PX, FOOTER_FALLBACK_FONT);
}

// Message screens (boot / Wi-Fi status / errors) render short strings the
// firmware controls, so they use the bitmap GFX fonts directly.  Skipping
// the .vlw path keeps the fast-path fast and avoids leaving a smooth font
// loaded when the next screen is a comic that wants a different size.
void selectStatusMessageTitleFont() { applyGfxFont(TITLE_FALLBACK_FONT); }
void selectStatusMessageDetailFont() { applyGfxFont(FOOTER_FALLBACK_FONT); }

// Formats a comic's publication date as a fixed "AA-BB-CCCC" (or
// "CCCC-AA-BB" for YMD) string, honouring config::DATE_LOCALE. Returns an
// empty string when any component is zero (unknown), so callers can skip
// rendering silently on malformed metadata rather than showing "0-0-0".
String formatComicDate(const Comic& comic) {
  if (comic.year <= 0 || comic.month <= 0 || comic.day <= 0) return String();
  char buf[16];
  switch (config::DATE_LOCALE) {
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
  const int x = config::PANEL_WIDTH - edgeInset - terminalWidth - w;
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
  if (config::DEBUG_SHOW_STATUS_BADGES && lastRefreshTime != nullptr &&
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

  if (config::DEBUG_SHOW_STATUS_BADGES && cacheStatsAvailable) {
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
                                sensorReadings.chargerValid && sensorReadings.externalPower);

  applyGfxFont(nullptr);
  epaper.setTextFont(2);
}

void renderStatus(const String& message, const String& detail = "",
                  const String& lineAbove = "") {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  if (!lineAbove.isEmpty()) {
    selectStatusMessageDetailFont();
    epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(lineAbove),
                                config::PANEL_WIDTH - config::ui(60), 1),
                      config::PANEL_WIDTH / 2,
                      config::PANEL_HEIGHT / 2 - config::ui(55), 1);
  }
  selectStatusMessageTitleFont();
  epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(message),
                              config::PANEL_WIDTH - config::ui(60), 1),
                    config::PANEL_WIDTH / 2,
                    config::PANEL_HEIGHT / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectStatusMessageDetailFont();
    epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(detail),
                                config::PANEL_WIDTH - config::ui(60), 1),
                      config::PANEL_WIDTH / 2,
                      config::PANEL_HEIGHT / 2 + config::ui(22), 1);
  }
  applyGfxFont(nullptr);
  epaper.setTextFont(2);
  drawBadges();
  updatePanel();
}

// writeLittleEndian16/32, screenshotPaletteColor, and saveScreenshotBmp now
// live in common/include/screenshot_bmp.h and are invoked via the template
// screenshot::saveScreenshotBmp<EPaper>().

void updatePanel() {
  if (screenshotRequested && sdReady) {
    screenshot::saveScreenshotBmp(epaper, config::PANEL_WIDTH,
                                  config::PANEL_HEIGHT);
    screenshotRequested = false;
  }
  const uint32_t heap = ESP.getFreeHeap();
  const uint32_t psram = ESP.getFreePsram();
  LOG.printf("[mem] before epd update  heap=%luK psram=%luK\n",
             static_cast<unsigned long>(heap / 1024),
             static_cast<unsigned long>(psram / 1024));
  const uint32_t start = millis();
  LOG.println("[render] epaper.update() start");
  epaper.update();
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
                                    config::PANEL_WIDTH - config::ui(24), 1);
  // Size the footer band from the smooth font's actual yAdvance so the
  // strip hugs the text.  Fall back to the compile-time constants when
  // the smooth font failed to load (GFX path preserves legacy sizing).
  const int smoothYA = static_cast<int>(epaper.gFont.yAdvance);
  layout.footerLineHeightPx =
      (g_currentSmoothSize > 0 && smoothYA > 0)
          ? smoothYA + config::ui(2)
          : config::FOOTER_LINE_HEIGHT;
  layout.footerBandPaddingPx =
      (g_currentSmoothSize > 0 && smoothYA > 0)
          ? config::ui(4)
          : config::FOOTER_VERTICAL_PADDING;
  // Leave the smooth footer font loaded so renderComic can reuse it
  // without paying another ~2 s SD read for the same size.
  layout.footerDividerY =
      config::FOOTER_BOTTOM -
      layout.footerLineCount * layout.footerLineHeightPx -
      2 * layout.footerBandPaddingPx;
  const int maxWidth = config::PANEL_WIDTH - 2 * config::CONTENT_MARGIN_X;
  const int maxHeight = layout.footerDividerY - config::CONTENT_TOP - config::ui(6);
  // Contain the comic in this model's actual content rectangle. Unlike the
  // original E1001-only layout, this also enlarges small source comics so they
  // remain readable on high-resolution E1003/E1004 panels.
  layout.scale = min(static_cast<float>(maxWidth) / sourceWidth,
                     static_cast<float>(maxHeight) / sourceHeight);
  layout.width = max(2, static_cast<int>(sourceWidth * layout.scale));
  layout.height = max(2, static_cast<int>(sourceHeight * layout.scale));
  // Packed 4bpp requires an even row width (two pixels per byte), but the
  // number of rows may be odd. Preserving an odd height also preserves source
  // images whose final rule or border is drawn on their last row.
  if (layout.width & 1) --layout.width;
  layout.x = (config::PANEL_WIDTH - layout.width) / 2;
  if (layout.x & 1) --layout.x;
  layout.y = config::CONTENT_TOP + (maxHeight - layout.height) / 2;
  return layout;
}

bool layoutIsSuitable(const ImageLayout& layout, int comicNumber) {
  if (layout.scale < config::MIN_DISPLAY_SCALE) {
    LOG.printf("[comic] #%d skipped: it needs reduction below %.0f%%\n",
               comicNumber, config::MIN_DISPLAY_SCALE * 100.0f);
    return false;
  }
  if (layout.width < config::MIN_RENDERED_WIDTH) {
    LOG.printf("[comic] #%d skipped: rendered width %d is below the "
               "%d-pixel minimum for this panel\n",
               comicNumber, layout.width, config::MIN_RENDERED_WIDTH);
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
  if (config::DEBUG_FORCE_COMIC > 0) {
    LOG.printf("[debug] DEBUG_FORCE_COMIC=%d, bypassing selection\n",
               config::DEBUG_FORCE_COMIC);
    if (loadUsableComic(config::DEBUG_FORCE_COMIC, networkAvailable, comic,
                        image, layout)) {
      return true;
    }
    LOG.printf("[debug] forced comic #%d could not be loaded; falling back\n",
               config::DEBUG_FORCE_COMIC);
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

#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
  // A full E1003/E1004 RGB888 resize can consume most or all of the 8 MB
  // PSRAM. Resample and Bayer-dither directly into the indexed panel buffer.
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

  xkcd_index::pack4bppInPlace(indices, layout.width, layout.height);

  epaper.fillSprite(PANEL_WHITE);
  epaper.pushImage(layout.x, layout.y, layout.width, layout.height,
                   reinterpret_cast<uint16_t*>(indices));
  free(indices);

  epaper.fillRect(0, 0, config::PANEL_WIDTH, config::ui(44), PANEL_WHITE);
  text_render::fillStatusBackground(epaper, layout.footerDividerY, config::PANEL_HEIGHT - layout.footerDividerY, config::PANEL_WIDTH, config::PANEL_HEIGHT, PANEL_STATUS_BACKGROUND, PANEL_STATUS_DITHERED, PANEL_STATUS_DITHER_COLOR, PANEL_STATUS_DITHER_THRESHOLD);
  epaper.drawFastHLine(config::CONTENT_MARGIN_X, config::ui(43),
                       config::PANEL_WIDTH - 2 * config::CONTENT_MARGIN_X,
                       PANEL_BLACK);
  epaper.drawFastHLine(config::CONTENT_MARGIN_X, layout.footerDividerY,
                       config::PANEL_WIDTH - 2 * config::CONTENT_MARGIN_X,
                       PANEL_BLACK);

  // Render the footer first: calculateLayout left the footer smooth
  // font loaded, so this reuses it (no SD reload).  Loading the title
  // font afterwards costs one load instead of two.
  selectFooterFont();
  epaper.setTextColor(PANEL_BLACK, PANEL_STATUS_BACKGROUND,
                      !PANEL_STATUS_DITHERED);
  epaper.setTextDatum(MC_DATUM);
  int footerY =
      (layout.footerDividerY + config::PANEL_HEIGHT) / 2 -
      (layout.footerLineCount - 1) * layout.footerLineHeightPx / 2;
  const int footerYAdjust = smoothCenterYAdjust();
  for (int i = 0; i < layout.footerLineCount; ++i) {
    epaper.drawString(layout.footerLines[i], config::PANEL_WIDTH / 2, footerY + footerYAdjust, 1);
    footerY += layout.footerLineHeightPx;
  }

  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  const String heading =
      quietSleepNotice
          ? "XKCD #" + String(comic.number) + " - sleeping until " +
                quiet_hours::endLabel()
          : "XKCD #" + String(comic.number) + " - " + comic.title;
  selectTitleFont();
  epaper.drawString(text_render::ellipsize(epaper, text_render::displayText(heading), config::PANEL_WIDTH - config::ui(380), 1),
                    config::PANEL_WIDTH / 2, config::ui(24) + smoothCenterYAdjust(), 1);
  applyGfxFont(nullptr);
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
                      config::PANEL_WIDTH - config::CONTENT_MARGIN_X,
                      layout.footerDividerY - config::ui(2), 1);
    applyGfxFont(nullptr);
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
void powerDownAndSleep(uint64_t sleepSeconds = config::SLEEP_SECONDS) {
  wifi_sta::disable();
  // Close the log file before SD.end() so its FAT/directory update
  // hits disk cleanly. Safe to call unconditionally -- no-ops when no
  // sink is attached.
  appLog.detachSdSink();
  if (sdReady) SD.end();
  pinMode(PIN_SD_ENABLE, OUTPUT);
  digitalWrite(PIN_SD_ENABLE, LOW);
  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);

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
  LOG.printf("[sleep] %llu seconds; GPIO3/GPIO4/GPIO5 wake enabled\n",
             static_cast<unsigned long long>(sleepSeconds));
  if (buttonWakeResult != ESP_OK && timerWakeResult != ESP_OK) {
    LOG.println("[sleep] no wake source could be configured; restarting");
    LOG.flush();
    delay(250);
    ESP.restart();
  }
  LOG.flush();
  delay(50);
  hardware::setStatusLed(false);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
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

  local_time::configureTimezone(config::TIMEZONE);
  quiet_hours::configure({config::QUIET_HOURS_ENABLED,
                          config::QUIET_START_HOUR,
                          config::QUIET_START_MINUTE,
                          config::QUIET_END_HOUR,
                          config::QUIET_END_MINUTE});
  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  if (app_logic::startupBeepRequired(coldBoot, buttonWake)) {
    // Acknowledge cold boots and button wakes immediately. Holding the green
    // button through this beep and the interval below requests a screenshot.
    hardware::beep();
  }

  bool greenHeldLongEnough = false;
  if (greenWokeDevice) {
    const uint32_t holdStarted = millis();
    uint32_t releaseStarted = 0;
    while (millis() - holdStarted < config::SCREENSHOT_LONG_PRESS_MS) {
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
    greenHeldLongEnough =
        millis() - holdStarted >= config::SCREENSHOT_LONG_PRESS_MS;
  }
  screenshotRequested = greenHeldLongEnough;
  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone XKCD / %s\n", MODEL_NAME, COLOR_MODE_NAME);
  LOG.println("============================================");

  LOG.printf("[boot] wake cause=%d pins=0x%llx, PSRAM=%luK, "
             "GPIO3=%s GPIO4=%s GPIO5=%s\n",
             wakeCause, static_cast<unsigned long long>(wakePins),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024),
             greenWokeDevice
                 ? (greenHeldLongEnough ? "long-press" : "short-press")
                 : "idle",
             rightWokeDevice ? "wake" : "idle",
             leftWokeDevice ? "wake" : "idle");
  if (screenshotRequested) {
    LOG.println("[screenshot] green-button long press requested export");
    delay(80);
    hardware::beep();
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

  epaper.begin();
  sdReady = sd_card::mount(epaper.getSPIinstance(), config::CACHE_DIR);
  if (sdReady && config::LOG_TO_SD) {
    log_sd_sink::install(appLog);
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
  String connectionDetail;
  if (sdReady) {
    connectionDetail = String(cachedComicCount) + " comics cached";
    if (ntpDue) {
      connectionDetail += " - synchronizing clock";
    } else if (!cacheOnly) {
      connectionDetail += " - building local cache";
    }
  } else {
    connectionDetail = "No SD cache - downloading live";
  }

  const bool showConnectionStatus = coldBoot && networkPlanned;
  const String stationMac = wifi_sta::stationMacAddress();
  LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());

#if RETERMINAL_MODEL == 1001
  // On a cold boot show a "Connecting to Wi-Fi" splash before switching
  // into Gray4, so the ~30 s network phase isn't a black hole. This is
  // pure UX - it used to be documented as a workaround for a UC8179
  // Gray4 driver quirk, but the actual driver issue was the SPI bus
  // being brought up without a real MISO pin (see common/include/
  // epaper_setup.h). Kept here just so the first frame isn't blank.
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(WIFI_SSID), connectionDetail,
                 stationMac);
  }
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

#if RETERMINAL_MODEL != 1001
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(WIFI_SSID), connectionDetail,
                 stationMac);
  }
#endif

  bool networkAvailable = false;
  if (networkPlanned) {
    networkAvailable = wifi_sta::connectStation(WIFI_SSID, WIFI_PASSWORD, config::WIFI_TIMEOUT_MS, nullptr, networkOperationShouldStop);
  } else {
    LOG.println("[wifi] skipped; using the local XKCD cache");
  }
  const bool ntpSynchronized =
      networkAvailable && ntpDue && ntp::synchronizeAndPersist(config::TIMEZONE, config::NTP_SERVER_PRIMARY, config::NTP_SERVER_SECONDARY, config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
  if (ntpDue && !ntpSynchronized && !coldBoot) {
    LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
    rtc_sync::restoreSystemClock();
  }
  local_time::configureTimezone(config::TIMEZONE);
  quiet_hours::configure({config::QUIET_HOURS_ENABLED,
                          config::QUIET_START_HOUR,
                          config::QUIET_START_MINUTE,
                          config::QUIET_END_HOUR,
                          config::QUIET_END_MINUTE});
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
    networkAvailable = wifi_sta::connectStation(WIFI_SSID, WIFI_PASSWORD, config::WIFI_TIMEOUT_MS, nullptr, networkOperationShouldStop);
    if (networkAvailable) {
      if (local_time::refreshDue(coldBoot, lastNtpSyncEpoch, config::NTP_REFRESH_SECONDS)) ntp::synchronizeAndPersist(config::TIMEZONE, config::NTP_SERVER_PRIMARY, config::NTP_SERVER_SECONDARY, config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
      acquired = acquireComic(true, comic, image, layout);
    }
  }

  // PNG decoding is complete at this point. Keep the radio off during
  // dithering and the comparatively slow e-paper update.
  wifi_sta::disable();

  uint64_t nextSleepSeconds = config::SLEEP_SECONDS;
  if (local_time::localClock(localTime)) {
    if (quiet_hours::active(localTime) ||
        quiet_hours::nextWakeFallsInside(localTime, config::SLEEP_SECONDS)) {
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
    renderStatus("XKCD refresh failed", reason);
  }

  // The panel retains the newly rendered frame without power. Do scheduled
  // cache maintenance only now, so downloads never delay or replace the UI.
  if (archiveDue) {
    LOG.println("[cache] starting scheduled maintenance after panel refresh");
    armMaintenanceButtonCancellation();
    networkOperationDeadlineMs =
        millis() + config::ARCHIVE_MAINTENANCE_DEADLINE_MS;
    if (wifi_sta::connectStation(WIFI_SSID, WIFI_PASSWORD, config::WIFI_TIMEOUT_MS, nullptr, networkOperationShouldStop)) {
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

  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
