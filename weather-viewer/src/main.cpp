#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <TFT_eSPI.h>
#include <driver/rtc_io.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <limits.h>

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
#include "log_sd_sink.h"
#include "text_render.h"
#include "units.h"
#include "weather_format.h"
#include "quiet_hours.h"
#include "sensors.h"
#include "low_battery.h"
#include "pcf8563_utc.h"
#include "screenshot_bmp.h"
#include "smooth_font_manager.h"
#include "panel_watchdog.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "timestamped_logger.h"
#include "theme.h"
#include "compact_portrait_layout.h"
#include "weather_data.h"
#include "weather_provider.h"
#include "canonical_weather.h"
#include "config_portal.h"
#include "config_portal_ui.h"
#include "sd_web_portal.h"
#include "wifi_schema.h"
#include "weather_config_schema.h"
#include "weather_config_runtime.h"
#include "weather_wifi_credentials.h"
#include "weather_icons.h"
#include "weather_quotes.h"
#include "weather_background.h"

#if RETERMINAL_MODEL == 1003
#include "fonts/Roboto_Bold90pt7b.h"
#elif RETERMINAL_MODEL == 1004
#include "fonts/Roboto_Bold72pt7b.h"
#else
#include "fonts/Roboto_Bold48pt7b.h"
#endif

SET_LOOP_TASK_STACK_SIZE(16U * 1024U);

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check common/include/driver.h"
#endif

TimestampedLogger appLog(Serial1);
// LOG is provided by app_logger.h; the definition above lives at namespace
// scope so the provider translation units can extern-link to it.

namespace {

using namespace ::board;
// Per-model palette, layout, dither and button-pin constants all live in
// include/theme.h; the using-directive keeps unqualified names (PANEL_WHITE,
// COLOR_SUN, PIN_BUTTON_GREEN, ...) working at every callsite below.
using namespace theme;

// The ink-wash weather background can be enabled on every model via
// /settings. E1005 uses a strongly faded one-bit version behind its compact
// layout.
// TFT_eSPI's drawString path for GFX free fonts
// unconditionally paints a padded fillRect(..., textbgcolor) behind
// every string whenever textcolor != textbgcolor - the _fillbg / bgfill
// flag is only checked on the smooth-font path. So the only way to stop
// body text from stamping visible rectangles onto the picture is
// to set textbgcolor == textcolor, which trips the "no fill needed"
// branch inside drawString. When the background is off we keep the classic
// (fg, PANEL_WHITE, true) behaviour so redraws still erase old glyphs
// and smooth fonts keep their anti-aliasing.
EPaper epaper;
smooth_fonts::Manager smoothFontManager(epaper);

inline int panelWidth() {
  return weather_config::runtime::panelWidth();
}

inline int panelHeight() {
  return weather_config::runtime::panelHeight();
}

String configurationGestureHint(bool reconfigure) {
#if RETERMINAL_MODEL == 1005
  return "Hold " + String(PRIMARY_BUTTON_LABEL) +
         " 2s from sleep to " + (reconfigure ? "reconfigure" : "configure");
#else
  if (reconfigure) {
    return "Keep " + String(PRIMARY_BUTTON_LABEL) +
           " pressed for 2 seconds to reconfigure.";
  }
  return "To configure device - from sleep, hold " +
         String(PRIMARY_BUTTON_LABEL) + " for 2 seconds";
#endif
}

inline bool weatherBackgroundActive() {
  return weather_config::runtime::weatherBackgroundEnabled();
}

inline void setBodyTextColor(uint16_t fg) {
  if (weatherBackgroundActive()) {
    epaper.setTextColor(fg, fg);
  } else {
    epaper.setTextColor(fg, PANEL_WHITE, true);
  }
}

// Header/alert strip callers know their background color (the strip
// fillRect that precedes them), so they can keep the classic path and
// preserve smooth-font anti-aliasing even when the background is enabled -
// the backdrop rect just repaints the strip's own color and stays invisible.
inline void setStripTextColor(uint16_t fg, uint16_t stripColor) {
  epaper.setTextColor(fg, stripColor, true);
}
Adafruit_SHT4x sht4;

bool sdReady = false;
bool screenshotRequested = false;
sensors::Readings sensorReadings;

RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
bool quietSleepNotice = false;

void beginPanel() {
#if RETERMINAL_MODEL == 1005
  // Sticky's SD card shares SCK/MOSI with the panel but has a separate
  // power rail. Power and deselect an inserted card before panel traffic
  // so it cannot clamp or back-power the shared bus.
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
#endif
  epaper_setup::begin(epaper);
#if RETERMINAL_MODEL == 1005
  epaper.setRotation(weather_config::runtime::panelRotation());
  LOG.printf("[panel] orientation=%s rotation=%d geometry=%dx%d\n",
             weather_config::runtime::isLandscape()
                 ? (weather_config::runtime::orientation() ==
                            weather_orientation::Orientation::RotateCW
                        ? "rotate-cw"
                        : "rotate-ccw")
                 : "portrait",
             weather_config::runtime::panelRotation(), panelWidth(),
             panelHeight());
#endif
}

// WeatherData / DailyForecast now live in weather_data.h so both the
// provider translation units and main.cpp share one definition.

// writeLittleEndian16/32, screenshotPaletteColor, and saveScreenshotBmp now
// live in common/include/screenshot_bmp.h and are invoked via the template
// screenshot::saveScreenshotBmp<EPaper>().

void updatePanel() {
  if (screenshotRequested && sdReady) {
    screenshot::saveScreenshotBmp(epaper, panelWidth(), panelHeight());
    screenshotRequested = false;
  }
  panel_watchdog::guard([]() { epaper.update(); });
}

// batteryPercentForVoltage() and the 16-sample averaging block used to be
// inline here; they now live in common/include/battery_gauge.h and are
// invoked via battery::measureBatteryFromAdc().

void selectSmallFont() {
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold12pt7b);
#else
  epaper.setFreeFont(&FreeSansBold9pt7b);
#endif
}

void selectSmallLightFont() {
  // Non-bold sibling of selectSmallFont for secondary labels that we
  // don't want to shout (e.g. "Overcast", forecast card details).
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSans18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSans12pt7b);
#else
  epaper.setFreeFont(&FreeSans9pt7b);
#endif
}

// Optional smooth-font support for the footer location name (which may
// contain non-ASCII characters like "München", "São Paulo").  The font
// files live at /fonts/sans_bold_<size>.vlw on the SD card (shared with
// the xkcd viewer; generated by tools/fonts/make_vlw.py).  Sizes
// mirror the small-font fallback: DejaVu Sans Bold's cap-height is a
// little taller than FreeSansBold's for the same em, so em 16/20/30
// visually matches FreeSansBold9/12/18pt7b.
#if RETERMINAL_MODEL == 1003
constexpr int SMOOTH_FONT_SMALL_PX = 30;
constexpr int SMOOTH_FONT_LARGE_PX = 48;  // largest .vlw baked by tools/fonts/make_vlw.py
constexpr const GFXfont* SMALL_SMOOTH_FALLBACK_FONT = &FreeSansBold18pt7b;
constexpr const GFXfont* LARGE_SMOOTH_FALLBACK_FONT = &FreeSansBold24pt7b;
#elif RETERMINAL_MODEL == 1004
constexpr int SMOOTH_FONT_SMALL_PX = 20;
constexpr int SMOOTH_FONT_LARGE_PX = 48;
constexpr const GFXfont* SMALL_SMOOTH_FALLBACK_FONT = &FreeSansBold12pt7b;
constexpr const GFXfont* LARGE_SMOOTH_FALLBACK_FONT = &FreeSansBold18pt7b;
#elif RETERMINAL_MODEL == 1005
// The 16 px smooth font loses too much stroke weight on the one-bit panel.
constexpr int SMOOTH_FONT_SMALL_PX = 20;
constexpr int SMOOTH_FONT_LARGE_PX = 48;
constexpr const GFXfont* SMALL_SMOOTH_FALLBACK_FONT = &FreeSansBold12pt7b;
constexpr const GFXfont* LARGE_SMOOTH_FALLBACK_FONT = &FreeSansBold12pt7b;
#else
constexpr int SMOOTH_FONT_SMALL_PX = 16;
constexpr int SMOOTH_FONT_LARGE_PX = 48;
constexpr const GFXfont* SMALL_SMOOTH_FALLBACK_FONT = &FreeSansBold9pt7b;
constexpr const GFXfont* LARGE_SMOOTH_FALLBACK_FONT = &FreeSansBold12pt7b;
#endif

// Select the smooth (Unicode-capable) small font.  Its cap-height
// matches selectSmallFont() so switching between the two keeps the
// header and footer visually consistent.  Used for the header title,
// the footer provider label, and the footer location name.
smooth_fonts::Selection selectSmallSmoothFont() {
  epaper.setTextSize(1);
  return smoothFontManager.select(
      SMOOTH_FONT_SMALL_PX, SMALL_SMOOTH_FALLBACK_FONT, sdReady);
}

// Select the largest smooth (Unicode-capable) font we bake to SD.
// tools/fonts/make_vlw.py generates sans_bold_<N>.vlw for every integer
// N from 12 to 48, so 48 px is the biggest that's actually on disk.
// Used for the location city label on the "Connecting to Wi-Fi" splash.
smooth_fonts::Selection selectLargeSmoothFont() {
  epaper.setTextSize(1);
  return smoothFontManager.select(
      SMOOTH_FONT_LARGE_PX, LARGE_SMOOTH_FALLBACK_FONT, sdReady);
}

// TFT_eSPI's MC/ML/MR datums center the smooth font's yAdvance box on
// the requested y, but DejaVu Sans Bold's ascent is much larger than
// its descent, so the visual cap-center sits a few pixels above the
// box center.  This helper returns the y offset (in pixels) needed to
// align the cap-center with the caller's y.  Returns 0 when a smooth
// font is not loaded so GFX callers are unaffected.
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

void drawE1005HeaderLocation(const String& location, int maxWidth,
                             int centerX, int centerY) {
  const String label = text_render::ellipsize(epaper, location, maxWidth - 2);
  if (!drawLoadedSmoothTextMonochrome(label, centerX, centerY)) {
    epaper.drawString(label, centerX, centerY, 1);
  }
}
#endif

void selectUpdateTimeFont() {
#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSans9pt7b);
#else
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
#endif
}

void selectMediumFont() {
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold24pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold18pt7b);
#else
  epaper.setFreeFont(&FreeSansBold12pt7b);
#endif
}

void selectLargeTemperatureFont() {
  // Each panel gets a native-resolution numeral font matching the physical
  // size of the formerly magnified 24-point font, without bitmap scaling.
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&Roboto_Bold90pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&Roboto_Bold72pt7b);
#else
  epaper.setFreeFont(&Roboto_Bold48pt7b);
#endif
}

#if RETERMINAL_MODEL == 1003
// Scale numerator/denominator for the E1003 hero temperature. 3/2 =
// 1.5x, which reads clearly on the 1872x1404 panel without needing a
// second bundled font asset. The stroke width of Roboto_Bold90pt7b
// (~20 px at source) is thick enough that a 1.5x nearest-neighbor
// upscale never shows more than ~1 px of stair-step per stroke edge.
constexpr int kLargeTempScaleNum = 3;
constexpr int kLargeTempScaleDen = 2;

inline int scaleN(int value) {
  // Integer scale by num/den with round-to-nearest for non-negative
  // values. Negative values (glyph yOffset is typically negative) are
  // handled explicitly so rounding tracks the source sign symmetrically.
  if (value >= 0) {
    return (value * kLargeTempScaleNum + kLargeTempScaleDen / 2) /
           kLargeTempScaleDen;
  }
  return -(((-value) * kLargeTempScaleNum + kLargeTempScaleDen / 2) /
          kLargeTempScaleDen);
}

// Draw one glyph from `font` at the fixed 3:2 scale. `leftX` is the
// pen position at the start of this glyph, `baselineY` is the shared
// baseline in output pixels. Returns the pen advance in output
// pixels. Nearest-neighbor upscaling; skips characters outside the
// font's coverage instead of asserting so mixed strings degrade
// silently.
int drawGlyphScaled150(const GFXfont* font, char c,
                       int leftX, int baselineY, uint32_t color) {
  const uint8_t ch = static_cast<uint8_t>(c);
  if (ch < font->first || ch > font->last) return 0;
  const GFXglyph* g = &font->glyph[ch - font->first];
  const int gw = g->width;
  const int gh = g->height;
  const int outW = scaleN(gw);
  const int outH = scaleN(gh);
  const int gLeft = leftX + scaleN(g->xOffset);
  const int gTop = baselineY + scaleN(g->yOffset);
  const uint8_t* bmp = &font->bitmap[g->bitmapOffset];
  for (int oy = 0; oy < outH; ++oy) {
    const int sy = (oy * kLargeTempScaleDen) / kLargeTempScaleNum;
    if (sy >= gh) continue;
    for (int ox = 0; ox < outW; ++ox) {
      const int sx = (ox * kLargeTempScaleDen) / kLargeTempScaleNum;
      if (sx >= gw) continue;
      const int bitIdx = sy * gw + sx;
      const uint8_t byte = pgm_read_byte(bmp + (bitIdx >> 3));
      if (byte & (0x80 >> (bitIdx & 7))) {
        epaper.drawPixel(gLeft + ox, gTop + oy, color);
      }
    }
  }
  return scaleN(g->xAdvance);
}

// Bounding box of `s` in output pixels for the given font at 3:2
// scale. Also returns the baseline offset from box top (`baselineFromTop`)
// so callers can center the box vertically and still position the
// baseline correctly for drawGlyphScaled150.
void measureScaled150(const GFXfont* font, const char* s,
                      int& outW, int& outH, int& baselineFromTop) {
  int totalAdvance = 0;
  int minTop = INT_MAX;
  int maxBot = INT_MIN;
  bool any = false;
  for (const char* p = s; *p; ++p) {
    const uint8_t ch = static_cast<uint8_t>(*p);
    if (ch < font->first || ch > font->last) continue;
    const GFXglyph* g = &font->glyph[ch - font->first];
    totalAdvance += g->xAdvance;
    minTop = min(minTop, static_cast<int>(g->yOffset));
    maxBot = max(maxBot, static_cast<int>(g->yOffset + g->height));
    any = true;
  }
  if (!any) {
    outW = 0;
    outH = 0;
    baselineFromTop = 0;
    return;
  }
  outW = scaleN(totalAdvance);
  outH = scaleN(maxBot - minTop);
  // baseline lives `-minTop` above the box bottom in source coords;
  // equivalently baseline sits `-minTop` below the box top (minTop is
  // typically negative for numerals like -126).
  baselineFromTop = scaleN(-minTop);
}
#endif  // RETERMINAL_MODEL == 1003

void drawBadges(uint32_t background = PANEL_WHITE,
                bool fillTextBackground = true,
                time_t weatherUpdateTime = 0,
                bool showOnCompactPortrait = false) {
#if RETERMINAL_MODEL == 1005
  if (!showOnCompactPortrait) return;
#else
  (void)showOnCompactPortrait;
#endif
  epaper.setTextColor(PANEL_BLACK, background, fillTextBackground);
  selectSmallFont();

  const int statusCenterY = config::ui(24);
  const int edgeInset = config::ui(6);
  epaper.setTextDatum(ML_DATUM);
  String climate = String("--.-") + units::temperatureLabel() + "  --%";
  if (sensorReadings.climateValid) {
    climate = weather_format::temperature(sensorReadings.temperatureC, 1) +
              "  " + String(sensorReadings.humidityPct, 0) + "%";
  }
  epaper.drawString(climate, edgeInset, statusCenterY, 1);

  const String percent = sensorReadings.batteryPct >= 0 ? String(sensorReadings.batteryPct) + "%" : "--%";
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
  if (weather_config::runtime::debugShowStatusBadges() &&
      weatherUpdateTime != 0) {
    // Format the UTC epoch as the device-local calendar day (MM-DD)
    // and time (HH:MM) — matching what the header shows.
    struct tm tm = {};
    if (localtime_r(&weatherUpdateTime, &tm) != nullptr) {
      char updateDate[6];
      char updateClock[6];
      snprintf(updateDate, sizeof(updateDate), "%02d-%02d",
               tm.tm_mon + 1, tm.tm_mday);
      snprintf(updateClock, sizeof(updateClock), "%02d:%02d",
               tm.tm_hour, tm.tm_min);
      const int updateRightX =
          percentRightX - epaper.textWidth(percent, 1) - config::ui(10);
      selectUpdateTimeFont();
      epaper.setTextColor(PANEL_BLACK, background, fillTextBackground);
      const int lineCenterDistance = epaper.fontHeight(1) + 1;
      const int dateY = statusCenterY - lineCenterDistance / 2;
      const int timeY = dateY + lineCenterDistance;
      epaper.drawString(updateDate, updateRightX, dateY, 1);
      epaper.drawString(updateClock, updateRightX, timeY, 1);
    }
  }
  text_render::drawBatteryGauge(epaper, x, y, w, h, sensorReadings.batteryPct, outline,
                                terminalWidth, terminalHeight, PANEL_BLACK, PANEL_WHITE,
                                sensorReadings.externalPowerValid && sensorReadings.externalPower);
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

void renderStatus(const String& message, const String& detail = "",
                  const String& lineAbove = "",
                  const String& helpBelow = "",
                  const String& subLineAbove = "",
                  const String& subHelpBelow = "") {
  epaper.fillSprite(PANEL_WHITE);
  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(MC_DATUM);
  if (!lineAbove.isEmpty()) {
    // Location city name -- use the largest smooth (Unicode) font we
    // bake so non-ASCII names ("Muenchen", "Sao Paulo") stay readable.
    selectLargeSmoothFont();
    epaper.drawString(
        text_render::ellipsize(epaper, lineAbove, panelWidth() - config::ui(60)),
        panelWidth() / 2,
        panelHeight() / 2 - config::ui(70) + smoothCenterYAdjust(), 1);
    // TFT_eSPI treats loadFont as sticky: setFreeFont alone won't switch
    // back. Unload so the subsequent selectMediumFont() actually applies.
    smoothFontManager.unload();
  }
  if (!subLineAbove.isEmpty()) {
    // Small ASCII line above the main message. Used by "Weather
    // unavailable" to keep a long failure summary readable without
    // shrinking the main title.
    selectSmallFont();
    epaper.drawString(
        text_render::ellipsize(epaper, subLineAbove, panelWidth() - config::ui(60)),
        panelWidth() / 2,
        panelHeight() / 2 - config::ui(40), 1);
  }
  selectMediumFont();
  epaper.drawString(
      text_render::ellipsize(epaper, message, panelWidth() - config::ui(60)),
      panelWidth() / 2,
      panelHeight() / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectSmallFont();
    epaper.drawString(
        text_render::ellipsize(epaper, detail, panelWidth() - config::ui(60)),
        panelWidth() / 2,
        panelHeight() / 2 + config::ui(25), 1);
  }
  if (!subHelpBelow.isEmpty()) {
    // Small ASCII sub-line (MAC + firmware) drawn just above the bottom
    // help hint so it stays informative without competing with the main
    // "Connecting to..." message.
    selectSmallFont();
    epaper.drawString(
        text_render::ellipsize(epaper, subHelpBelow, panelWidth() - config::ui(60)),
        panelWidth() / 2,
        panelHeight() - config::ui(46), 1);
  }
  if (!helpBelow.isEmpty()) {
    selectSmallFont();
    epaper.drawString(
        text_render::ellipsize(epaper, helpBelow, panelWidth() - config::ui(60)),
        panelWidth() / 2,
        panelHeight() - config::ui(24), 1);
  }
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  drawBadges();
  updatePanel();
}

// NTP sync helpers now live in common/include/ntp_sync.h. The wrapper below
bool parseWeather(const String& body, WeatherData& weather) {
  if (weather_config::runtime::weatherProvider() == config::WeatherProvider::QWeather) {
    return weather_provider::parseQWeather(body, weather);
  }
  return weather_provider::parseOpenMeteo(body, weather);
}

bool fetchWeather(WeatherData& weather, String& responseBody,
                  String& failureReason, bool bypassHttpCache = false) {
  if (weather_config::runtime::weatherProvider() == config::WeatherProvider::QWeather) {
    return weather_provider::fetchQWeather(weather, responseBody,
                                           failureReason, bypassHttpCache);
  }
  return weather_provider::fetchOpenMeteo(weather, responseBody,
                                          failureReason, bypassHttpCache);
}

bool loadCachedWeather(WeatherData& weather, String& failureReason,
                       uint64_t maxAgeSeconds = config::CACHE_MAX_AGE_SECONDS) {
  failureReason = "";
  String body;
  if (!sdReady) {
    failureReason = "No SD forecast cache is available";
    return false;
  }
  if (!sd_card::fileExists(config::FORECAST_CACHE)) {
    failureReason = "No saved forecast is available";
    return false;
  }
  if (!sd_card::readFile(config::FORECAST_CACHE, body, 128U * 1024U)) {
    failureReason = "Saved forecast could not be read";
    return false;
  }
  if (!canonical_weather::parse(body, weather)) {
    failureReason = "Saved forecast is invalid";
    return false;
  }

  const time_t now = time(nullptr);
  if (!local_time::clockIsValid()) {
    failureReason = "Cannot verify saved forecast age";
    LOG.printf("[cache] %s\n", failureReason.c_str());
    return false;
  }
  const time_t forecastTime = weather.updateTime;
  if (forecastTime == 0) {
    failureReason = "Saved forecast time is invalid";
    LOG.printf("[cache] %s\n", failureReason.c_str());
    return false;
  }
  if (!app_logic::cachedDataFresh(
          true, now, forecastTime, maxAgeSeconds)) {
    if (now < forecastTime) {
      failureReason = "Saved forecast time is in the future";
    } else {
      const uint64_t ageMinutes =
          static_cast<uint64_t>(now - forecastTime + 59) / 60;
      failureReason =
          "Saved forecast is " + String(ageMinutes) + " minutes old";
    }
    LOG.printf("[cache] rejected: %s (maximum %llu seconds)\n",
               failureReason.c_str(),
               static_cast<unsigned long long>(maxAgeSeconds));
    return false;
  }

  weather.fromCache = true;
  {
    char buf[24];
    local_time::formatLocalIso(weather.updateTime, buf, sizeof(buf));
    LOG.printf("[cache] using forecast updated %s\n", buf);
  }
  return true;
}

String uvDescription(float uv) {
  if (!isfinite(uv)) return "--";
  if (uv < 3.0f) return "Low";
  if (uv < 6.0f) return "Moderate";
  if (uv < 8.0f) return "High";
  if (uv < 11.0f) return "Very high";
  return "Extreme";
}

String updateClock(time_t epoch) {
  if (epoch == 0) return "";
  struct tm tm = {};
  if (localtime_r(&epoch, &tm) == nullptr) return "";
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
  return String(buf);
}

String weatherAgeText(time_t forecastTime) {
  if (!local_time::clockIsValid() || forecastTime == 0) return "";

  const int64_t roundedMinutes = app_logic::roundedAgeMinutes(
      static_cast<int64_t>(time(nullptr)),
      static_cast<int64_t>(forecastTime));
  if (roundedMinutes < 0) return "";
  if (roundedMinutes == 0) return "just now";

  const int64_t hours = roundedMinutes / 60;
  const int64_t minutes = roundedMinutes % 60;
  String age;
  if (hours > 0) {
    age = String(static_cast<long long>(hours)) +
          (hours == 1 ? " hour" : " hours");
    if (minutes > 0) {
      age += " and " + String(static_cast<long long>(minutes)) +
             (minutes == 1 ? " minute" : " minutes");
    }
  } else {
    age = String(static_cast<long long>(minutes)) +
          (minutes == 1 ? " minute" : " minutes");
  }
  return age + " ago";
}

// Full weekday name derived from an ISO "YYYY-MM-DD" date string.  Uses
// mktime() to let libc normalize the calendar (so leap years / month
// lengths are handled without a lookup table).  Returns "" for malformed
// input so callers can fall back to a shorter format.
String weekdayName(const String& date) {
  if (date.length() < 10) return "";
  const int y = date.substring(0, 4).toInt();
  const int m = date.substring(5, 7).toInt();
  const int d = date.substring(8, 10).toInt();
  if (y == 0 || m == 0 || d == 0) return "";
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = 12;  // avoid DST-boundary weirdness at midnight
  if (mktime(&t) == static_cast<time_t>(-1)) return "";
  static const char* kNames[] = {"Sunday",   "Monday", "Tuesday",
                                 "Wednesday", "Thursday", "Friday",
                                 "Saturday"};
  if (t.tm_wday < 0 || t.tm_wday > 6) return "";
  return String(kNames[t.tm_wday]);
}

String dayLabel(uint8_t index, const String& date) {
  if (index == 0) return "Today";
  if (index == 1) return "Tomorrow";
  const String weekday = weekdayName(date);
  if (weekday.length() > 0) return weekday;
  if (date.length() >= 10) return date.substring(5);
  return "Day " + String(index + 1);
}

String nextRainWhen(const WeatherData& weather) {
  if (weather.nextRainTime == 0) return "";
  const String clock = updateClock(weather.nextRainTime);
  if (clock.isEmpty()) return "";
  // Format the observation instant as a device-local calendar date to
  // compare against days[i].date (also device-local "YYYY-MM-DD").
  struct tm tm = {};
  if (localtime_r(&weather.nextRainTime, &tm) == nullptr) return clock;
  char date[11];
  snprintf(date, sizeof(date), "%04d-%02d-%02d",
           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  const String dateStr(date);
  if (!weather.days[0].date.isEmpty() && dateStr == weather.days[0].date)
    return clock;
  if (!weather.days[1].date.isEmpty() && dateStr == weather.days[1].date)
    return "tomorrow " + clock;
  char mmdd[6];
  snprintf(mmdd, sizeof(mmdd), "%02d-%02d", tm.tm_mon + 1, tm.tm_mday);
  return String(mmdd) + " " + clock;
}

String rainSummary(const WeatherData& weather) {
  if (weather.rainExpected) {
    const String when = nextRainWhen(weather);
    if (when.startsWith("tomorrow "))
      return "Rain tomorrow " + when.substring(9);
    String summary = "Rain " + when;
    if (weather.nextRainProbability >= 0) {
      summary += " (" + String(weather.nextRainProbability) + "%)";
    }
    return summary;
  }
  if (weather.rainTimingAvailable) {
    // Hidden entirely in default clutter-free display; the caller falls
    // back to showing wind on this line instead.
    return "";
  }
  return "";
}

void thickLine(int x1, int y1, int x2, int y2, int thickness,
               uint32_t color) {
  if (thickness <= 1) {
    epaper.drawLine(x1, y1, x2, y2, color);
    return;
  }
  // Axis-aligned rays use rectangles so their opposing directions have
  // exactly the same rasterized width. Triangle edge rules can otherwise
  // make a right-to-left horizontal line one pixel thinner.
  if (y1 == y2) {
    const int left = min(x1, x2);
    epaper.fillRect(left, y1 - thickness / 2,
                    abs(x2 - x1) + 1, thickness, color);
    return;
  }
  if (x1 == x2) {
    const int top = min(y1, y2);
    epaper.fillRect(x1 - thickness / 2, top,
                    thickness, abs(y2 - y1) + 1, color);
    return;
  }
  const float dx = static_cast<float>(x2 - x1);
  const float dy = static_cast<float>(y2 - y1);
  const float length = sqrtf(dx * dx + dy * dy);
  if (length < 1.0f) {
    epaper.fillCircle(x1, y1, max(1, thickness / 2), color);
    return;
  }
  const float half = thickness / 2.0f;
  const int px = static_cast<int>(roundf(-dy * half / length));
  const int py = static_cast<int>(roundf(dx * half / length));
  epaper.fillTriangle(x1 + px, y1 + py, x1 - px, y1 - py,
                      x2 + px, y2 + py, color);
  epaper.fillTriangle(x1 - px, y1 - py, x2 - px, y2 - py,
                      x2 + px, y2 + py, color);
}

void drawWeatherIcon(int cx, int cy, int size, int code, bool isDay = true) {
  // Blit one of the 26 baked Meteocons sprites (see weather_icons.h and
  // tools/generate_weather_icons.py). The sprite table is generated
  // per-board at three sizes; pickSprite picks the closest and blit
  // nearest-neighbour scales to the requested size.
  weather_icons::draw(epaper, cx, cy, size, code, isDay, PANEL_BLACK);
}

// Bold degree mark next to the hero temperature. The classic single-
// pixel outline gets swallowed by both the ink-wash background and
// the sheer size of the digits next to it, so paint a thick ring by
// stroking `thickness` concentric circles. The interior stays hollow
// so the mark still reads as "degree" rather than a filled bullet.
void drawDegreeMark(int cx, int cy, int outerRadius, int thickness,
                    uint32_t color) {
  if (thickness < 1) thickness = 1;
  if (thickness > outerRadius) thickness = outerRadius;
  for (int t = 0; t < thickness; ++t) {
    epaper.drawCircle(cx, cy, outerRadius - t, color);
  }
}

void drawLargeTemperature(float celsius, int cx, int cy) {
  if (!isfinite(celsius)) {
    setBodyTextColor(PANEL_BLACK);
    epaper.setTextDatum(MC_DATUM);
    selectMediumFont();
    epaper.drawString(weather_format::kMissing, cx, cy, 1);
    return;
  }
  const int rounded = static_cast<int>(roundf(units::temperatureDisplay(celsius)));
  const bool negative = rounded < 0;
  const String value = String(abs(rounded));
  setBodyTextColor(PANEL_BLACK);
#if RETERMINAL_MODEL == 1003
  // E1003 hero temperature is rendered 1.5x larger than the bundled
  // 90pt font by walking Roboto_Bold90pt7b glyphs and blitting each
  // source pixel at 3:2 nearest-neighbor scale. This avoids shipping
  // a second font asset just for this panel. The minus sign and
  // degree glyph are drawn manually so they scale with `textHeight`
  // automatically.
  const GFXfont* font = &Roboto_Bold90pt7b;
  int boxW, boxH, baselineFromTop;
  measureScaled150(font, value.c_str(), boxW, boxH, baselineFromTop);
  const int textHeight = boxH;
  const int minusWidth = negative ? max(4, textHeight / 3) : 0;
  const int minusGap = negative ? max(2, textHeight / 12) : 0;
  const int valueLeft = cx - boxW / 2 + (minusWidth + minusGap) / 2;
  const int valueTop = cy - boxH / 2;
  const int baselineY = valueTop + baselineFromTop;
  int pen = valueLeft;
  for (unsigned i = 0; i < value.length(); ++i) {
    pen += drawGlyphScaled150(font, value[i], pen, baselineY, PANEL_BLACK);
  }
  const int valueCenterX = valueLeft + boxW / 2;
  if (negative) {
    const int totalWidth = minusWidth + minusGap + boxW;
    const int minusLeft = cx - totalWidth / 2;
    thickLine(minusLeft, cy, minusLeft + minusWidth, cy,
              max(2, textHeight / 18), PANEL_BLACK);
  }
  const int degreeRadius = max(3, textHeight / 9);
  const int degreeThickness = max(2, textHeight / 28);
  drawDegreeMark(valueLeft + boxW + degreeRadius * 2,
                 cy - textHeight / 3, degreeRadius, degreeThickness,
                 PANEL_BLACK);
  (void)valueCenterX;
#else
  epaper.setTextDatum(MC_DATUM);
  selectLargeTemperatureFont();
  const int textWidth = epaper.textWidth(value, 1);
  const int textHeight = epaper.fontHeight(1);
  const int minusWidth = negative ? max(4, textHeight / 3) : 0;
  const int minusGap = negative ? max(2, textHeight / 12) : 0;
  const int valueCenterX = cx + (minusWidth + minusGap) / 2;
  epaper.drawString(value, valueCenterX, cy, 1);
  if (negative) {
    const int totalWidth = minusWidth + minusGap + textWidth;
    const int minusLeft = cx - totalWidth / 2;
    thickLine(minusLeft, cy, minusLeft + minusWidth, cy,
              max(2, textHeight / 18), PANEL_BLACK);
  }
  const int degreeRadius = max(3, textHeight / 9);
  const int degreeThickness = max(2, textHeight / 28);
  drawDegreeMark(valueCenterX + textWidth / 2 + degreeRadius * 2,
                 cy - textHeight / 3, degreeRadius, degreeThickness,
                 PANEL_BLACK);
#endif
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(1);
}

void drawHeader(const WeatherData& weather) {
#if RETERMINAL_MODEL == 1005
  if (weather_config::runtime::isLandscape()) {
    const int height = config::ui(45);
    epaper.fillRect(0, 0, panelWidth(), height, PANEL_WHITE);
    drawBadges(PANEL_WHITE, true, weather.updateTime, true);
    setStripTextColor(PANEL_BLACK, PANEL_WHITE);
    epaper.setTextDatum(MC_DATUM);
    selectSmallSmoothFont();
    const String location = text_render::displayText(
        String(weather_config::runtime::locationName()));
    drawE1005HeaderLocation(
        location, panelWidth() - config::ui(380), panelWidth() / 2,
        config::ui(25) + smoothCenterYAdjust());
    smoothFontManager.unload();
    epaper.drawFastHLine(config::ui(10), config::ui(44),
                         panelWidth() - config::ui(20), PANEL_BLACK);
    return;
  }
  using namespace compact_portrait_layout;
  epaper.fillRect(0, 0, config::PANEL_WIDTH, HEADER_HEIGHT, PANEL_WHITE);
  drawBadges(PANEL_WHITE, true, 0, true);
  setStripTextColor(PANEL_BLACK, PANEL_WHITE);
  epaper.setTextDatum(MC_DATUM);
  selectSmallSmoothFont();
  const String location =
      text_render::displayText(String(weather_config::runtime::locationName()));
  drawE1005HeaderLocation(location, HEADER_LOCATION_WIDTH,
                          config::PANEL_WIDTH / 2,
                          20 + smoothCenterYAdjust());
  smoothFontManager.unload();

  selectSmallFont();
  String status;
  if (quietSleepNotice) {
    status = "Sleeping until " + quiet_hours::endLabel();
  } else {
    const String age = weatherAgeText(weather.updateTime);
    status = age.isEmpty() ? String("Weather")
                           : String("Updated ") + age;
  }
  epaper.drawString(
      text_render::ellipsize(epaper, status, config::PANEL_WIDTH - 28),
      config::PANEL_WIDTH / 2, 47, 1);
  epaper.drawFastHLine(14, HEADER_HEIGHT - 1,
                       config::PANEL_WIDTH - 28, PANEL_BLACK);
  return;
#else
  const int height = config::ui(45);
  epaper.fillRect(0, 0, config::PANEL_WIDTH, height, PANEL_WHITE);
  drawBadges(PANEL_WHITE, true, weather.updateTime);
  setStripTextColor(PANEL_BLACK, PANEL_WHITE);
  epaper.setTextDatum(MC_DATUM);
  String heading;
  const String age = weatherAgeText(weather.updateTime);
  if (!age.isEmpty()) {
    heading = "Weather " + age;
  } else {
    heading = "Weather";
  }
  if (quietSleepNotice) {
    heading = "Weather - sleeping until " + quiet_hours::endLabel();
  }
  // Render the header title in the shared smooth (Unicode-capable) font
  // so it matches the footer's provider label and location name.
  selectSmallSmoothFont();
  epaper.drawString(
      text_render::ellipsize(epaper, text_render::displayText(heading), config::PANEL_WIDTH - config::ui(380)),
      config::PANEL_WIDTH / 2, config::ui(25) + smoothCenterYAdjust(), 1);
  // Unload before returning: the main-body renders that follow use GFX
  // fonts, but TFT_eSPI's drawString stays on the smooth-font path as
  // long as one is loaded, which would shrink e.g. the large outdoor
  // temperature down to 16 px.  renderFooter reloads the same size.
  smoothFontManager.unload();
  epaper.drawFastHLine(config::ui(10), config::ui(44),
                       config::PANEL_WIDTH - config::ui(20), PANEL_BLACK);
#endif
}

void drawForecastCard(const DailyForecast& day, uint8_t index,
                      int left, int top, int width, int height) {
  const int centerX = left + width / 2;
  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(TC_DATUM);
  selectMediumFont();
  epaper.drawString(dayLabel(index, day.date), centerX,
                    top + config::ui(7), 1);

  const int iconSize = min(width / 7, config::ui(32));
  drawWeatherIcon(centerX, top + config::ui(55), iconSize,
                  day.weatherCode, true);

  selectSmallFont();
  epaper.drawString(
      text_render::ellipsize(epaper, app_logic::conditionName(day.weatherCode), width - config::ui(12)),
      centerX, top + config::ui(83), 1);
  const String range =
      weather_format::temperature(day.minimumC) + "  /  " +
      weather_format::temperature(day.maximumC);
  epaper.drawString(range, centerX, top + config::ui(107), 1);
  if (!weather_config::runtime::clutterFreeMode()) {
    // Muted gray reads fine on a plain white sprite but gets lost on
    // top of the ink-wash background, so match the rest of the card's
    // text weight when the background is on.
    const uint32_t extraColor =
        weatherBackgroundActive() ? PANEL_BLACK : PANEL_MUTED;
    setBodyTextColor(extraColor);
    String extra;
    if (day.precipitationProbability >= 0) {
      extra = "Rain " + weather_format::integer(day.precipitationProbability) +
              "%   UV " + weather_format::number(day.uvMaximum, 1) + " " +
              uvDescription(day.uvMaximum);
    } else {
      extra = "UV " + weather_format::number(day.uvMaximum, 1) + " " +
              uvDescription(day.uvMaximum);
    }
    epaper.drawString(text_render::ellipsize(epaper, extra, width - config::ui(12)), centerX,
                      top + config::ui(130), 1);
    setBodyTextColor(PANEL_BLACK);
  }
}

void renderLandscape(const WeatherData& weather) {
  const int mainTop = config::ui(48);
  const int mainBottom = panelHeight() * 62 / 100;
  const int mainCenterY = (mainTop + mainBottom) / 2;
  const int leftDividerX = panelWidth() * 34 / 100;
  // Center the hero icon inside the left pane (0 .. leftDividerX)
  // instead of at a hard-coded 19% offset that was slightly off.
  const int iconX = leftDividerX / 2;
  const int temperatureX = panelWidth() * 49 / 100;
  const int detailX = panelWidth() * 83 / 100;

  drawWeatherIcon(iconX, mainCenterY,
                  min(panelWidth() * 27 / 100,
                      (mainBottom - mainTop) * 72 / 100),
                  weather.weatherCode, weather.isDay);
  epaper.drawFastVLine(leftDividerX,
                       mainTop + config::ui(12),
                       mainBottom - mainTop - config::ui(24), PANEL_MUTED);

  drawLargeTemperature(weather.temperatureC, temperatureX,
                       mainCenterY - config::ui(13));
  // "Outdoor temperature" is dropped -- the giant number next to the
  // weather icon already communicates the same thing.  The condition
  // name takes its slot in bold black instead.
  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(TC_DATUM);
  selectMediumFont();
  epaper.drawString(app_logic::conditionName(weather.weatherCode), temperatureX,
                    mainCenterY + config::ui(53), 1);

  epaper.drawFastVLine(panelWidth() * 66 / 100,
                       mainTop + config::ui(12),
                       mainBottom - mainTop - config::ui(24), PANEL_MUTED);
  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(
      weather_format::temperature(weather.apparentC),
      detailX, mainCenterY - config::ui(66), 1);
  // Primary captions ("Feels like", "Outdoor humidity") stay bold black so
  // they read cleanly next to the big numbers.  The rain / wind line below
  // is the only secondary detail rendered in muted grey.
  selectSmallFont();
  epaper.drawString("Feels like", detailX,
                    mainCenterY - config::ui(40), 1);

  selectMediumFont();
  epaper.drawString(
      isfinite(weather.humidityPct)
          ? String(static_cast<int>(roundf(weather.humidityPct))) + "%"
          : String(weather_format::kMissing),
      detailX, mainCenterY + config::ui(5), 1);
  selectSmallFont();
  epaper.drawString("Outdoor humidity", detailX,
                    mainCenterY + config::ui(31), 1);

  const int detailWidth = panelWidth() * 31 / 100;
  const String windLine = "Wind " + weather_format::windSpeed(weather.windKmh);
  String rainLine = rainSummary(weather);
  const bool rainLineIsWind = rainLine.length() == 0;
  if (rainLineIsWind) {
    rainLine = windLine;
  }
  // Render both lines in solid black so their weight visually matches
  // "Feels like" / "Outdoor humidity" above; previously the rain line
  // used PANEL_MUTED which on Gray16 reads as a thinner, lighter font
  // even though the glyph shapes are identical.
  setBodyTextColor(PANEL_BLACK);
  epaper.drawString(
      text_render::ellipsize(epaper, rainLine, detailWidth),
      detailX, mainCenterY + config::ui(67), 1);
  if (!rainLineIsWind) {
    epaper.drawString(windLine, detailX,
                      mainCenterY + config::ui(101), 1);
  }
  setBodyTextColor(PANEL_BLACK);

  epaper.drawFastHLine(config::ui(10), mainBottom,
                       panelWidth() - config::ui(20), PANEL_MUTED);
  const int forecastTop = mainBottom + config::ui(4);
  const int footerTop = panelHeight() - config::ui(30);
  const int cardWidth =
      (panelWidth() - config::ui(20)) / config::FORECAST_DAYS;
  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    const int left = config::ui(10) + i * cardWidth;
    if (i > 0) {
      epaper.drawFastVLine(left, forecastTop + config::ui(8),
                           footerTop - forecastTop - config::ui(12),
                           PANEL_LIGHT);
    }
    drawForecastCard(weather.days[i], i, left, forecastTop, cardWidth,
                     footerTop - forecastTop);
  }
}

void drawPortraitForecastRow(const DailyForecast& day, uint8_t index,
                             int top, int height) {
  const int margin = config::ui(14);
  const int iconX = config::PANEL_WIDTH * 18 / 100;
  const int textX = config::PANEL_WIDTH * 37 / 100;
  const int valuesX = config::PANEL_WIDTH * 78 / 100;
  const int centerY = top + height / 2;

  epaper.drawFastHLine(margin, top, config::PANEL_WIDTH - 2 * margin,
                       PANEL_LIGHT);
  const int iconSize =
      min(height * 3 / 5, config::PANEL_WIDTH * 14 / 100);
  drawWeatherIcon(iconX, centerY, iconSize,
                  day.weatherCode, true);
  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(ML_DATUM);
  selectMediumFont();
  epaper.drawString(dayLabel(index, day.date), textX,
                    centerY - config::ui(28), 1);
  selectSmallFont();
  epaper.drawString(
      text_render::ellipsize(epaper, app_logic::conditionName(day.weatherCode),
                config::PANEL_WIDTH * 36 / 100),
      textX, centerY + config::ui(4), 1);
  if (!weather_config::runtime::clutterFreeMode()) {
    String extra;
    if (day.precipitationProbability >= 0) {
      extra = "Rain " + weather_format::integer(day.precipitationProbability) +
              "%  UV " + weather_format::number(day.uvMaximum, 1);
    } else {
      extra = "UV " + weather_format::number(day.uvMaximum, 1);
    }
    epaper.drawString(extra, textX, centerY + config::ui(31), 1);
  }

  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(
      weather_format::temperature(day.minimumC) + " / " +
          weather_format::temperature(day.maximumC),
      valuesX, centerY - config::ui(8), 1);
  selectSmallFont();
  epaper.drawString("Low / High", valuesX,
                    centerY + config::ui(25), 1);
}

void renderPortrait(const WeatherData& weather) {
  const int mainTop = config::ui(50);
  const int mainBottom = config::PANEL_HEIGHT * 43 / 100;
  const int mainCenterY = (mainTop + mainBottom) / 2;

  drawWeatherIcon(config::PANEL_WIDTH * 24 / 100,
                  mainCenterY - config::ui(25),
                  min(config::PANEL_WIDTH * 35 / 100,
                      (mainBottom - mainTop) * 72 / 100),
                  weather.weatherCode, weather.isDay);
  drawLargeTemperature(weather.temperatureC,
                       config::PANEL_WIDTH * 67 / 100,
                       mainCenterY - config::ui(38));

  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(app_logic::conditionName(weather.weatherCode),
                    config::PANEL_WIDTH / 2,
                    mainCenterY + config::ui(72), 1);
  selectSmallFont();
  const String details =
      "Feels " + weather_format::temperature(weather.apparentC) +
      "   Humidity " + String(weather.humidityPct, 0) + "%   Wind " +
      weather_format::windSpeed(weather.windKmh);
  epaper.drawString(
      text_render::ellipsize(epaper, details, config::PANEL_WIDTH - config::ui(40)),
      config::PANEL_WIDTH / 2, mainCenterY + config::ui(108), 1);
  epaper.drawString(
      text_render::ellipsize(epaper, rainSummary(weather),
                config::PANEL_WIDTH - config::ui(40)),
      config::PANEL_WIDTH / 2, mainCenterY + config::ui(142), 1);

  const int footerTop = config::PANEL_HEIGHT - config::ui(34);
  const int rowHeight =
      (footerTop - mainBottom) / config::FORECAST_DAYS;
  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    drawPortraitForecastRow(weather.days[i], i,
                            mainBottom + i * rowHeight, rowHeight);
  }
}

#if RETERMINAL_MODEL == 1005
void drawCompactForecastRow(const DailyForecast& day, uint8_t index,
                            int top, int height) {
  const int centerY = top + height / 2;
  epaper.drawFastHLine(14, top, config::PANEL_WIDTH - 28, PANEL_BLACK);

  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(ML_DATUM);
  selectMediumFont();
  epaper.drawString(dayLabel(index, day.date), 20, centerY - 24, 1);
  selectSmallLightFont();
  epaper.drawString(
      text_render::ellipsize(
          epaper, app_logic::conditionName(day.weatherCode), 150),
      20, centerY + 18, 1);

  drawWeatherIcon(220, centerY, 64, day.weatherCode, true);

  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(
      weather_format::temperature(day.minimumC) + " / " +
          weather_format::temperature(day.maximumC),
      372, centerY - 14, 1);
  selectSmallFont();
  epaper.drawString("Low / High", 372, centerY + 23, 1);
}

void renderCompactPortrait(const WeatherData& weather) {
  using namespace compact_portrait_layout;
  static_assert(fitsPanel(config::PANEL_WIDTH, config::PANEL_HEIGHT),
                "compact E1005 layout must fit the logical panel");

  const int top = heroTop(!weather.alertTitle.isEmpty());
  drawWeatherIcon(HERO_ICON_X, top + 78, HERO_ICON_SIZE,
                  weather.weatherCode, weather.isDay);
  drawLargeTemperature(weather.temperatureC, HERO_TEMPERATURE_X, top + 78);

  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(app_logic::conditionName(weather.weatherCode),
                    config::PANEL_WIDTH / 2, top + 174, 1);

  selectSmallFont();
  const String humidity =
      isfinite(weather.humidityPct)
          ? String(static_cast<int>(roundf(weather.humidityPct))) + "%"
          : String(weather_format::kMissing);
  const String details =
      "Feels " + weather_format::temperature(weather.apparentC) +
      "   Humidity " + humidity;
  epaper.drawString(
      text_render::ellipsize(epaper, details, config::PANEL_WIDTH - 32),
      config::PANEL_WIDTH / 2, top + 207, 1);

  String secondary = rainSummary(weather);
  if (secondary.isEmpty()) {
    secondary = "Wind " + weather_format::windSpeed(weather.windKmh);
  }
  epaper.drawString(
      text_render::ellipsize(epaper, secondary, config::PANEL_WIDTH - 32),
      config::PANEL_WIDTH / 2, top + 236, 1);

  for (uint8_t i = 0; i < FORECAST_DAYS; ++i) {
    drawCompactForecastRow(weather.days[i], i, forecastRowTop(i),
                           FORECAST_ROW_HEIGHT);
  }
}
#endif

void renderFooter(const WeatherData& weather) {
#if RETERMINAL_MODEL == 1005
  if (!weather_config::runtime::isLandscape()) {
    using namespace compact_portrait_layout;
    epaper.fillRect(0, FOOTER_TOP, config::PANEL_WIDTH,
                    config::PANEL_HEIGHT - FOOTER_TOP, PANEL_WHITE);
    epaper.drawFastHLine(14, FOOTER_TOP,
                         config::PANEL_WIDTH - 28, PANEL_BLACK);
    setStripTextColor(PANEL_BLACK, PANEL_WHITE);
    epaper.setTextDatum(MC_DATUM);
    selectSmallFont();
    epaper.drawString(weather_provider::name(), config::PANEL_WIDTH / 2,
                      (FOOTER_TOP + config::PANEL_HEIGHT) / 2, 1);
    return;
  }
#endif
  const int top = panelHeight() - config::ui(30);
  // Anchor the label baseline to the actual band vertical center so the
  // text visually sits in the middle of the strip (previously the label
  // was 2 px above centre for a 30 px band, which was noticeable on
  // solid backgrounds).
  const int labelY = (top + panelHeight()) / 2;
  text_render::fillStatusBackground(epaper, top, panelHeight() - top,
                                    panelWidth(), panelHeight(),
                                    PANEL_STATUS_BACKGROUND,
                                    PANEL_STATUS_DITHERED,
                                    PANEL_STATUS_DITHER_COLOR,
                                    PANEL_STATUS_DITHER_THRESHOLD);
  epaper.drawFastHLine(config::ui(10), top,
                       panelWidth() - config::ui(20), PANEL_MUTED);
  const smooth_fonts::Selection footerFont = selectSmallSmoothFont();
  if (PANEL_STATUS_DITHERED &&
      footerFont == smooth_fonts::Selection::GfxFallback) {
    // TFT_eSPI's bgfill flag applies only to smooth fonts. GFX FreeFont
    // fallbacks still fill their full bounding box whenever fg != bg, so
    // use the one-colour overload to make the fallback truly transparent.
    epaper.setTextColor(PANEL_BLACK);
  } else {
    // Smooth-font anti-aliasing still needs the explicit band colour even
    // when its rectangular background fill is disabled.
    epaper.setTextColor(PANEL_BLACK, PANEL_STATUS_BACKGROUND,
                        !PANEL_STATUS_DITHERED);
  }
  const int footerYAdjust = smoothCenterYAdjust();

  // Left: provider name. Measure its width first so we know where the
  // available middle band ends.
  const String providerText =
      text_render::displayText(String(weather_provider::name()));
  const int leftPad = config::ui(12);
  const int providerRight = leftPad + epaper.textWidth(providerText);
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString(providerText, leftPad, labelY + footerYAdjust, 1);

  // Right: location name. Measure similarly to know where the middle
  // band starts.
  const String locationText =
      text_render::displayText(String(weather_config::runtime::locationName()));
  const int rightPad = config::ui(12);
  const int locationLeft = panelWidth() - rightPad
                           - epaper.textWidth(locationText);
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(locationText, panelWidth() - rightPad,
                    labelY + footerYAdjust, 1);

  // Centre: weather-themed proverb whose bucket matches the current
  // WMO code. Random per refresh via esp_random(); if no proverb in
  // the primary bucket (or the UNIVERSAL fallback) fits the gap, skip
  // silently so nothing collides with the fixed left/right labels.
  const int gapPad = config::ui(12);
  const int availStart = providerRight + gapPad;
  const int availEnd = locationLeft - gapPad;
  const int availPx = availEnd - availStart;
  if (availPx > config::ui(40)) {
    const char* quote = weather_quotes::pickForWmo(
        weather.weatherCode, esp_random(), epaper, availPx);
    if (quote != nullptr) {
      epaper.setTextDatum(MC_DATUM);
      epaper.drawString(quote, (availStart + availEnd) / 2,
                        labelY + footerYAdjust, 1);
    }
  }

  // Restore the GFX small font in case any later footer additions rely
  // on it; also releases the .vlw resources so the next full-panel
  // repaint doesn't hold onto them.
  smoothFontManager.unload();
  selectSmallFont();
}

void drawAlertBar(const WeatherData& weather) {
  if (weather.alertTitle.isEmpty()) return;
#if RETERMINAL_MODEL == 1005
  if (!weather_config::runtime::isLandscape()) {
    using namespace compact_portrait_layout;
    epaper.fillRect(0, ALERT_TOP, config::PANEL_WIDTH, ALERT_HEIGHT,
                    PANEL_BLACK);
    setStripTextColor(PANEL_WHITE, PANEL_BLACK);
    epaper.setTextDatum(MC_DATUM);
    selectSmallFont();
    String line = "! " + weather.alertTitle;
    if (weather.alertOtherCount > 0) {
      line += " (+" + String(weather.alertOtherCount) + ")";
    }
    epaper.drawString(
        text_render::ellipsize(epaper, line, config::PANEL_WIDTH - 24),
        config::PANEL_WIDTH / 2, ALERT_TOP + ALERT_HEIGHT / 2, 1);
    return;
  }
#endif
  const int top = config::ui(46);
  const int height = config::ui(22);
  epaper.fillRect(0, top, panelWidth(), height, PANEL_LIGHT);
  epaper.drawFastHLine(config::ui(10), top + height,
                       panelWidth() - config::ui(20), PANEL_MUTED);
  setStripTextColor(PANEL_BLACK, PANEL_LIGHT);
  epaper.setTextDatum(MC_DATUM);
  selectSmallFont();
  String line = "! Alert: " + weather.alertTitle;
  if (weather.alertOtherCount > 0) {
    line += " (+" + String(weather.alertOtherCount) + " more)";
  }
  epaper.drawString(
      text_render::ellipsize(epaper, line,
                             panelWidth() - config::ui(24)),
      panelWidth() / 2, top + height / 2, 1);
}

void renderWeather(const WeatherData& weather) {
  // Paint the ink-wash landscape first so the header, icons, and text
  // render on top of it. Payloads are pre-baked per model (2bpp on the
  // gray panels, 1bpp black-and-white on the Spectra-6 panels) so the
  // blit covers every pixel and we skip the fillSprite step. When the
  // user has toggled the background off in /settings, fall back to a
  // plain white sprite instead.
  if (weatherBackgroundActive()) {
    weather_background::draw(
        epaper, weather_background::themeForWmoCode(weather.weatherCode));
  } else {
    epaper.fillSprite(PANEL_WHITE);
  }
  drawHeader(weather);
  drawAlertBar(weather);
#if RETERMINAL_MODEL == 1005
  if (weather_config::runtime::isLandscape()) {
    renderLandscape(weather);
  } else {
    renderCompactPortrait(weather);
  }
#elif RETERMINAL_MODEL == 1004
  renderPortrait(weather);
#else
  renderLandscape(weather);
#endif
  renderFooter(weather);
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  {
    const String ageForLog = weatherAgeText(weather.updateTime);
    char updateBuf[24];
    local_time::formatLocalIso(weather.updateTime, updateBuf,
                               sizeof(updateBuf));
    LOG.printf("[render] source=%s updateTime=%s header=\"Weather %s\"\n",
               weather.fromCache ? "cache" : "live",
               updateBuf[0] ? updateBuf : "(unknown)",
               ageForLog.isEmpty() ? "(no age)" : ageForLog.c_str());
  }
  LOG.println("[render] refreshing weather panel");
  updatePanel();
  LOG.println("[render] complete");
}

void powerDownAndSleep(uint64_t sleepSeconds = config::SLEEP_SECONDS,
                       bool timerWakeEnabled = true) {
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
  peripheral_power::disable();
  if (PIN_BATTERY_ENABLE >= 0) {
    pinMode(PIN_BATTERY_ENABLE, OUTPUT);
    digitalWrite(PIN_BATTERY_ENABLE, LOW);
  }

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const uint32_t releaseStarted = millis();
  while ((!digitalRead(PIN_BUTTON_GREEN) ||
          !digitalRead(PIN_BUTTON_RIGHT) ||
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
      timerWakeEnabled
          ? esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL)
          : esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  LOG.printf("[sleep] wake config: buttons=%s timer=%s levels=%d/%d/%d\n",
             esp_err_to_name(buttonWakeResult),
             timerWakeEnabled ? esp_err_to_name(timerWakeResult) : "disabled",
             digitalRead(PIN_BUTTON_GREEN),
             digitalRead(PIN_BUTTON_RIGHT),
             digitalRead(PIN_BUTTON_LEFT));
  if (timerWakeEnabled) {
    LOG.printf("[sleep] %llu seconds; GPIO%d/%d/%d wake enabled\n",
               static_cast<unsigned long long>(sleepSeconds),
               PIN_BUTTON_GREEN, PIN_BUTTON_RIGHT, PIN_BUTTON_LEFT);
  } else {
    LOG.println("[sleep] waiting for a front button or hardware reset");
  }
  if (buttonWakeResult != ESP_OK &&
      (!timerWakeEnabled || timerWakeResult != ESP_OK)) {
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
  const esp_sleep_wakeup_cause_t wakeCause =
      esp_sleep_get_wakeup_cause();
  const bool buttonWake = wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  const uint64_t wakePins =
      buttonWake
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

  local_time::configureTimezone(weather_config::runtime::timezone());
  quiet_hours::configure({weather_config::runtime::quietHoursEnabled(),
                          weather_config::runtime::quietStartHour(),
                          weather_config::runtime::quietStartMinute(),
                          weather_config::runtime::quietEndHour(),
                          weather_config::runtime::quietEndMinute()});
  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);

  // Load NVS-backed settings so every weather_config::runtime accessor
  // returns a consistent value for the rest of this boot. Do this after
  // LOG.begin so any storage messages land on the serial console.
  weather_config::runtime::load();
  weather_wifi::load();
  // Dump the resolved runtime config so an operator can see at a glance
  // which NVS values are in effect vs. falling back to compile-time /
  // secrets.h defaults. Helps distinguish "portal saved the wrong thing"
  // from "portal didn't save at all" when a device comes up unexpectedly.
  {
    const auto provider = weather_config::runtime::weatherProvider();
    const char* providerName =
        (provider == ::config::WeatherProvider::OpenMeteo) ? "OpenMeteo" : "QWeather";
    LOG.printf("[config] location=\"%s\" lat=%.4f lon=%.4f\n",
               weather_config::runtime::locationName(),
               weather_config::runtime::latitude(),
               weather_config::runtime::longitude());
    LOG.printf("[config] provider=%s sleep=%llus tz=\"%s\"\n",
               providerName,
               static_cast<unsigned long long>(weather_config::runtime::sleepSeconds()),
               weather_config::runtime::timezone());
  }
  // Re-apply timezone / quiet hours now that NVS values are cached (the
  // initial configure calls above ran off the constexpr defaults).
  local_time::configureTimezone(weather_config::runtime::timezone());
  quiet_hours::configure({weather_config::runtime::quietHoursEnabled(),
                          weather_config::runtime::quietStartHour(),
                          weather_config::runtime::quietStartMinute(),
                          weather_config::runtime::quietEndHour(),
                          weather_config::runtime::quietEndMinute()});

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
                   PRIMARY_BUTTON_LABEL,
                   static_cast<unsigned>(releasedAtMs),
                   static_cast<unsigned>(kPortalMinMs));
        gesture = GreenGesture::None;
      } else {
        LOG.printf("[gesture] %s released after %u ms -> portal\n",
                   PRIMARY_BUTTON_LABEL,
                   static_cast<unsigned>(releasedAtMs));
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
  const bool wifiUnconfigured = coldBoot && !weather_wifi::haveCredentials();
  const bool portalRequested =
      gesture == GreenGesture::PortalRequest || wifiUnconfigured;
  screenshotRequested = gesture == GreenGesture::ScreenshotRequest;

  if (portalRequested) {
    LOG.printf("[portal] entering config portal (no_wifi=%d gesture=%s)\n",
               wifiUnconfigured,
               gesture == GreenGesture::PortalRequest ? "primary-button" : "auto");
    // Restore the wall clock from the battery-backed PCF8563 before we
    // start the portal. NTP isn't available here (Wi-Fi is almost
    // certainly unconfigured - that's why we're in the portal), and
    // the ESP32's own RTC drifts several percent per sleep cycle. The
    // main app path does this too, further down; the portal branch
    // returns via ESP.restart() so it never reaches that call.
    rtc_sync::restoreSystemClock();
    // Bring the panel up FIRST so the QR splash renders before we start
    // the Wi-Fi AP + web server.
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
    portalCfg.appSchema = &weather_config::kSchema;
    portalCfg.appName = "weather viewer";
    portalCfg.useAutoApPassword = true;
    // Nav tab that jumps to the SD browser served by sd_web_portal.
    // The route is registered below via attachRoutes; the config_portal
    // chrome renders this tab between Settings and Reset.
    static const config_portal::NavTab kExtraTabs[] = {
        {"SD", "/browse?path=%2F", "sd"},
    };
    portalCfg.extraTabs = kExtraTabs;
    portalCfg.extraTabCount = sizeof(kExtraTabs) / sizeof(kExtraTabs[0]);
    portalCfg.wifiFallback = [](const char* key) -> String {
      if (strcmp(key, "ssid") == 0) return String(weather_wifi::ssid());
      if (strcmp(key, "password") == 0) return String(weather_wifi::password());
      return String();
    };
    portalCfg.sdFormat = [](String& error) -> bool {
      return sd_card::formatCard(epaper.getSPIinstance(), config::CACHE_DIR, error);
    };
    portalCfg.sdFormatWarning = "cached weather forecasts and logs";
    if (config_portal::begin(portalCfg)) {
      // Wire the SD browser routes onto config_portal's WebServer. This
      // must happen after begin() succeeds so webServer() is non-null,
      // and before the portal loop starts so the first request lands on
      // registered handlers. Browser-only config: no photo uploader.
      sd_web_portal::Config sdCfg;
      sdCfg.navHtml = nullptr;  // filled in after we compute nav strip
      static String s_navHtml;
      s_navHtml = config_portal::renderNavStripHtml(portalCfg, nullptr);
      sdCfg.navHtml = s_navHtml.c_str();
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
      panel_watchdog::guard([]() { epaper.update(); });
      LOG.printf("[portal] panel refresh complete in %u ms\n",
                 static_cast<unsigned>(millis() - updateStart));

      // Now that the panel is done, mount SD so SD.begin() is the last
      // caller to configure the shared SPI bus. Mounting earlier left
      // SD in a state that epaper.update() would then break, causing
      // every /browse to fail with 'bus reset ... failed'. SD is
      // optional in portal mode - a missing/failing card just leaves
      // the browser tab empty.
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

  if (app_logic::startupBeepRequired(coldBoot, buttonWake)) {
    // Acknowledge cold boots and button wakes immediately. The unified
    // primary-button gesture handler above has already decided whether
    // this boot is a portal request or a screenshot request.
    hardware::beep();
  }

  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone Weather / %s\n",
             MODEL_NAME, COLOR_MODE_NAME);
  LOG.printf(" Firmware v%s\n", board::FIRMWARE_VERSION);
  LOG.println("============================================");
  LOG.printf("[boot] wake cause=%d pins=0x%llx, PSRAM=%luK, "
             "GPIO%d(%s)=%s GPIO%d(%s)=%s GPIO%d(%s)=%s\n",
             wakeCause, static_cast<unsigned long long>(wakePins),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024),
             PIN_BUTTON_GREEN, BUTTON_0_NAME,
             greenWokeDevice
                 ? (screenshotRequested ? "long-press" : "short-press")
                 : "idle",
             PIN_BUTTON_RIGHT, BUTTON_1_NAME,
             rightWokeDevice ? "wake" : "idle",
             PIN_BUTTON_LEFT, BUTTON_2_NAME,
             leftWokeDevice ? "wake" : "idle");
  if (screenshotRequested) {
    LOG.printf("[screenshot] %s-button long press requested export\n",
               PRIMARY_BUTTON_LABEL);
  }

  const time_t startupTime = time(nullptr);
  const bool schedulingClockSuspicious =
      !local_time::clockIsValid() ||
      (lastNtpSyncEpoch > 0 && startupTime < lastNtpSyncEpoch);
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
  const bool ntpDue = config::DEBUG_FORCE_NTP ||
      local_time::refreshDue(coldBoot, lastNtpSyncEpoch, config::NTP_REFRESH_SECONDS);
  if (config::DEBUG_FORCE_NTP) {
    LOG.println("[debug] DEBUG_FORCE_NTP=true, resyncing every wake");
  }
  struct tm localTime = {};
  const bool haveLocalTime = local_time::localClock(localTime);
  if (app_logic::suppressForQuietHours(
          coldBoot, buttonWake, ntpDue, haveLocalTime,
          haveLocalTime && quiet_hours::active(localTime))) {
    const uint64_t quietSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
    LOG.printf("[quiet] refresh suppressed; sleeping until %s\n",
               quiet_hours::endLabel().c_str());
    powerDownAndSleep(quietSleepSeconds);
    return;
  }

  sensors::readAll(PIN_BATTERY_ENABLE, PIN_BATTERY_ADC, sht4, config::SENSOR_READ_ATTEMPTS, config::SENSOR_RETRY_DELAY_MS, sensorReadings);
  if (coldBoot && !hardwareRtcCheckedEarly) {
    pcf8563::Reading storedRtc;
    rtc_sync::readAndLog(storedRtc);
  }
  beginPanel();
  if (low_battery::shouldWarn(weather_config::runtime::lowBatteryWarn(),
                              sensorReadings.batteryValid,
                              sensorReadings.externalPower,
                              sensorReadings.batteryPct)) {
    LOG.printf("[battery] %d%% (%.3fV) below %d%% -- rendering recharge screen\n",
               sensorReadings.batteryPct, sensorReadings.batteryVoltage,
               low_battery::kThresholdPct);
    renderStatus("Please recharge",
                 "Plug in USB-C then press " +
                     String(PRIMARY_BUTTON_LABEL) + " to continue.",
                 "Battery low");
    powerDownAndSleep(config::SLEEP_SECONDS);
    return;
  }
  sdReady = sd_card::mount(epaper.getSPIinstance(), config::CACHE_DIR);
  if (sdReady && weather_config::runtime::logToSd()) {
    log_sd_sink::install(appLog);
  }
  // SD-driven firmware update. If /update.bin is present, stream it into
  // the inactive OTA slot and reboot; the current firmware stays intact
  // if anything (SHA-256, model tag, SD read) fails. Shared across all
  // three viewer apps - see common/src/sd_ota.cpp.
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

  WeatherData weather;
  String liveFailureReason;
  String cacheFailureReason;
  bool cacheChecked = false;
  bool cacheLoaded = false;

  // Validate the cache before composing the cold-boot screen. File existence
  // alone does not mean the saved forecast is recent enough to use.
  if (!buttonWake && local_time::clockIsValid()) {
    cacheChecked = true;
    cacheLoaded = loadCachedWeather(weather, cacheFailureReason);
  }

  const bool showConnectionStatus = coldBoot;
  const String connectionDetail =
      cacheLoaded ? "Live update not required" : "Live update required";
  const String stationMac = wifi_sta::stationMacAddress();
  const String locationLabel = String(weather_config::runtime::locationName());
  // Firmware version + MAC form the small ASCII-only sub-line under the
  // large city label so the connecting splash tells the user which build
  // is running (for confirming an SD-driven update landed) and which
  // device is joining the AP.
  const String macAndVersion =
      String("MAC: ") + stationMac + "  Firmware: " + board::FIRMWARE_VERSION;
  LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());
  LOG.printf("[location] %s (%.4f, %.4f)\n", locationLabel.c_str(),
             weather_config::runtime::latitude(),
             weather_config::runtime::longitude());

  // Cold-boot "Connecting to Wi-Fi" splash. On gray panels this is pushed
  // before initGrayMode() for a faster monochrome refresh; six-color panels
  // use their native controller mode. renderStatus() completes the update
  // before Wi-Fi connection blocks.
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(weather_wifi::ssid()), connectionDetail,
                 locationLabel,
                 configurationGestureHint(false),
                 "", macAndVersion);
  }
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

  const bool networkRequired = buttonWake || ntpDue || !cacheLoaded;
  const bool networkAvailable =
      networkRequired && wifi_sta::connectStation(weather_wifi::ssid(), weather_wifi::password(), config::WIFI_TIMEOUT_MS, &liveFailureReason).connected;
  const bool ntpSynchronized =
      networkAvailable && ntpDue && ntp::synchronizeAndPersist(weather_config::runtime::timezone(), weather_config::runtime::ntpPrimary(), weather_config::runtime::ntpSecondary(), config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
  bool rtcRestored = false;
  if (ntpDue && !ntpSynchronized && !coldBoot) {
    LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
    rtcRestored = rtc_sync::restoreSystemClock();
  }
  if (coldBoot && ntpDue && !ntpSynchronized) {
    LOG.println("[display] cold boot NTP unavailable; warning user about clock");
    const String warning =
        weather_config::runtime::weatherProvider() == config::WeatherProvider::QWeather
            ? "Clock not synced - times inaccurate, QWeather may fail"
            : "Clock not synced - displayed times may be inaccurate";
    renderStatus("Connecting to " + String(weather_wifi::ssid()), warning, locationLabel,
                 "", "", macAndVersion);
  }
  local_time::configureTimezone(weather_config::runtime::timezone());
  quiet_hours::configure({weather_config::runtime::quietHoursEnabled(),
                          weather_config::runtime::quietStartHour(),
                          weather_config::runtime::quietStartMinute(),
                          weather_config::runtime::quietEndHour(),
                          weather_config::runtime::quietEndMinute()});
  if (!wakeEventLogged) {
    wake_report::logWakeEvent(wakeCause, wakePins, true);
  }

  if (!coldBoot && !buttonWake && local_time::localClock(localTime) &&
      quiet_hours::active(localTime)) {
    const uint64_t quietSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
    LOG.printf("[quiet] refresh suppressed after clock sync; sleeping until %s\n",
               quiet_hours::endLabel().c_str());
    wifi_sta::disable();
    powerDownAndSleep(quietSleepSeconds);
    return;
  }

  // Cold boot NTP may have made it possible to validate a cache whose age
  // could not be checked before connecting. Only re-check when the clock's
  // state actually changed (fresh NTP sync or RTC restore) or when the first
  // attempt was skipped because the clock was not yet valid -- otherwise
  // re-running produces the same answer and duplicates the log line.
  if (!buttonWake && !cacheLoaded &&
      (!cacheChecked || ntpSynchronized || rtcRestored) &&
      local_time::clockIsValid()) {
    cacheChecked = true;
    cacheLoaded = loadCachedWeather(weather, cacheFailureReason);
  }

  String liveResponse;
  const bool liveUpdated =
      !cacheLoaded && networkAvailable &&
      fetchWeather(weather, liveResponse, liveFailureReason, buttonWake);
  if (cacheLoaded) {
    LOG.println("[cache] fresh forecast selected; skipping live weather request");
  }
  // The response has already been parsed. Do not keep the radio associated
  // while writing the cache, composing the frame, or refreshing the panel.
  wifi_sta::disable();

  if (liveUpdated && sdReady) {
    String canonical;
    if (!canonical_weather::serialize(weather, canonical)) {
      LOG.println("[cache] forecast serialization failed; skipping save");
    } else if (sd_card::writeFileAtomically(config::FORECAST_CACHE,
                                            canonical)) {
      LOG.println("[cache] saved latest forecast");
    } else {
      LOG.println("[cache] forecast not stored; continuing with live data");
    }
  }
  if (!liveUpdated && !cacheLoaded && !cacheChecked) {
    cacheLoaded = loadCachedWeather(weather, cacheFailureReason);
  }
  const bool haveWeather = liveUpdated || cacheLoaded;
  uint64_t nextSleepSeconds = weather_config::runtime::sleepSeconds();
  if (local_time::localClock(localTime)) {
    if (quiet_hours::active(localTime) ||
        quiet_hours::nextWakeFallsInside(localTime, weather_config::runtime::sleepSeconds())) {
      quietSleepNotice = true;
      nextSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
      LOG.printf("[quiet] this is the final refresh; sleeping until %s\n",
                 quiet_hours::endLabel().c_str());
    }
  }

  if (haveWeather) {
    renderWeather(weather);
  } else {
    // Live weather failed. Fall back to the saved forecast if it is still
    // reasonably fresh; only show the error screen if the cache is older
    // than FAILURE_CACHE_MAX_AGE_SECONDS (or missing).
    String staleFailureReason;
    const bool staleCacheLoaded =
        local_time::clockIsValid() &&
        loadCachedWeather(weather, staleFailureReason,
                          config::FAILURE_CACHE_MAX_AGE_SECONDS);
    if (app_logic::useCachedForecastOnFailure(
            /*liveFetchSucceeded=*/false, local_time::clockIsValid(),
            /*cacheAvailable=*/staleCacheLoaded,
            /*cacheWithinFailureWindow=*/staleCacheLoaded)) {
      LOG.println("[weather] live fetch failed; showing recent cached forecast");
      renderWeather(weather);
      powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
      return;
    }
    if (liveFailureReason.isEmpty())
      liveFailureReason = "Live weather is unavailable";
    if (cacheFailureReason.isEmpty())
      cacheFailureReason = staleFailureReason.isEmpty()
                               ? "No fresh saved forecast is available"
                               : staleFailureReason;
    const String failureSummary =
        liveFailureReason + "; " + cacheFailureReason;
    LOG.printf("[weather] unavailable: %s\n", failureSummary.c_str());
    const uint64_t retryMinutes = config::FAILURE_RETRY_SECONDS / 60ULL;
    const String detail =
        "Retrying in " + String(static_cast<unsigned long>(retryMinutes)) +
        " minutes. Press " + String(PRIMARY_BUTTON_LABEL) +
        " to retry now.";
    const String help = configurationGestureHint(true);
    renderStatus("Weather unavailable", detail, "", help, failureSummary);
    powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    return;
  }

  // Reached the successful sleep path: mark the running image valid so
  // ESP-IDF's rollback watchdog does not revert an SD-OTA install on the
  // next boot. Safe no-op if this isn't a pending-verify image.
  sd_ota::confirmRunningImage();
  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
