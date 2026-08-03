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
#include "hardware.h"
#include "local_time.h"
#include "wake_report.h"
#include "rtc_sync.h"
#include "wifi_sta.h"
#include "climate_sensor.h"
#include "sd_card.h"
#include "epaper_setup.h"
#include "log_sd_sink.h"
#include "text_render.h"
#include "units.h"
#include "weather_format.h"
#include "quiet_hours.h"
#include "sensors.h"
#include "pcf8563_utc.h"
#include "screenshot_bmp.h"
#include "panel_watchdog.h"
#include "timestamped_logger.h"
#include "theme.h"
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

// The ink-wash weather background can now be enabled on any of the four
// panels via /settings. TFT_eSPI's drawString path for GFX free fonts
// unconditionally paints a padded fillRect(..., textbgcolor) behind
// every string whenever textcolor != textbgcolor - the _fillbg / bgfill
// flag is only checked on the smooth-font path. So the only way to stop
// body text from stamping visible rectangles onto the picture is
// to set textbgcolor == textcolor, which trips the "no fill needed"
// branch inside drawString. When the background is off we keep the classic
// (fg, PANEL_WHITE, true) behaviour so redraws still erase old glyphs
// and smooth fonts keep their anti-aliasing.
EPaper epaper;

inline void setBodyTextColor(uint16_t fg) {
  if (weather_config::runtime::weatherBackgroundEnabled()) {
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

// WeatherData / DailyForecast now live in weather_data.h so both the
// provider translation units and main.cpp share one definition.

// writeLittleEndian16/32, screenshotPaletteColor, and saveScreenshotBmp now
// live in common/include/screenshot_bmp.h and are invoked via the template
// screenshot::saveScreenshotBmp<EPaper>().

void updatePanel() {
  if (screenshotRequested && sdReady) {
    screenshot::saveScreenshotBmp(epaper, config::PANEL_WIDTH,
                                  config::PANEL_HEIGHT);
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
#else
constexpr int SMOOTH_FONT_SMALL_PX = 16;
constexpr int SMOOTH_FONT_LARGE_PX = 48;
constexpr const GFXfont* SMALL_SMOOTH_FALLBACK_FONT = &FreeSansBold9pt7b;
constexpr const GFXfont* LARGE_SMOOTH_FALLBACK_FONT = &FreeSansBold12pt7b;
#endif

static int g_currentSmoothSize = 0;
static bool g_smoothFontsUnavailable = false;
// Sizes we have already opened successfully at least once this boot. Set
// entries let a later applySmoothFont() call skip the SD.exists()/open()
// probe and go straight to loadFont(), which avoids a rare SD flake
// between the header and footer causing the footer to silently drop to
// the GFX fallback.
static int g_smoothFontVerifiedSizes[4] = {0, 0, 0, 0};

static bool smoothFontSizeVerified(int size) {
  for (int slot : g_smoothFontVerifiedSizes) {
    if (slot == size) return true;
  }
  return false;
}

static void rememberSmoothFontSizeVerified(int size) {
  for (int& slot : g_smoothFontVerifiedSizes) {
    if (slot == size) return;
    if (slot == 0) {
      slot = size;
      return;
    }
  }
}

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

// Install the smooth font at `size` (pixels) for the next drawString
// calls.  Falls back to `fallback` (a GFX FreeFont at roughly the same
// cap-height) when the SD is not mounted, the .vlw is missing, or a
// previous load has already failed this boot.  Mirrors the xkcd-viewer
// helper of the same shape - keeps behaviour consistent across apps.
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
  if (!smoothFontSizeVerified(size) && !smoothFontFileExists(size)) {
    LOG.printf("[font] /fonts/sans_bold_%d.vlw probe failed; falling back to GFX font for this call\n",
               size);
    // Do NOT set g_smoothFontsUnavailable: a probe miss on the SPI SD is
    // often transient. Latching it here converts one SD hiccup into
    // "the whole boot renders without Unicode", which is much worse
    // than a one-frame fallback. Later calls will retry the probe.
    epaper.setFreeFont(fallback);
    return;
  }
  epaper.setFreeFont(nullptr);
  const uint32_t t0 = millis();
  // TFT_eSPI::loadFont builds "/" + name + ".vlw" internally, so pass
  // the subdir as part of the name to get "/fonts/sans_bold_XX.vlw".
  epaper.loadFont(String("fonts/sans_bold_") + size, SD);
  g_currentSmoothSize = size;
  rememberSmoothFontSizeVerified(size);
  LOG.printf("[font] loaded sans_bold_%d in %lu ms (yAdvance=%u ascent=%u descent=%u)\n",
             size, (unsigned long)(millis() - t0),
             (unsigned)epaper.gFont.yAdvance,
             (unsigned)epaper.gFont.ascent,
             (unsigned)epaper.gFont.descent);
}

// Select the smooth (Unicode-capable) small font.  Its cap-height
// matches selectSmallFont() so switching between the two keeps the
// header and footer visually consistent.  Used for the header title,
// the footer provider label, and the footer location name.
void selectSmallSmoothFont() {
  epaper.setTextSize(1);
  applySmoothFont(SMOOTH_FONT_SMALL_PX, SMALL_SMOOTH_FALLBACK_FONT);
}

// Select the largest smooth (Unicode-capable) font we bake to SD.
// tools/fonts/make_vlw.py generates sans_bold_<N>.vlw for every integer
// N from 12 to 48, so 48 px is the biggest that's actually on disk.
// Used for the location city label on the "Connecting to Wi-Fi" splash.
void selectLargeSmoothFont() {
  epaper.setTextSize(1);
  applySmoothFont(SMOOTH_FONT_LARGE_PX, LARGE_SMOOTH_FALLBACK_FONT);
}

// TFT_eSPI's MC/ML/MR datums center the smooth font's yAdvance box on
// the requested y, but DejaVu Sans Bold's ascent is much larger than
// its descent, so the visual cap-center sits a few pixels above the
// box center.  This helper returns the y offset (in pixels) needed to
// align the cap-center with the caller's y.  Returns 0 when a smooth
// font is not loaded so GFX callers are unaffected.
static int smoothCenterYAdjust() {
  if (g_currentSmoothSize == 0) return 0;
  const int yA = static_cast<int>(epaper.gFont.yAdvance);
  const int mA = static_cast<int>(epaper.gFont.maxAscent);
  const int a  = static_cast<int>(epaper.gFont.ascent);
  // Approximate cap-height as ascent * 0.78 (DejaVu Sans Bold).
  return (yA / 2) - mA + (a * 78 / 200);
}

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
                time_t weatherUpdateTime = 0) {
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
  const int x = config::PANEL_WIDTH - edgeInset - terminalWidth - w;
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
                                sensorReadings.chargerValid && sensorReadings.externalPower);
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

void renderStatus(const String& message, const String& detail = "",
                  const String& lineAbove = "",
                  const String& helpBelow = "") {
  epaper.fillSprite(PANEL_WHITE);
  setBodyTextColor(PANEL_BLACK);
  epaper.setTextDatum(MC_DATUM);
  if (!lineAbove.isEmpty()) {
    // Location city name -- use the largest smooth (Unicode) font we
    // bake so non-ASCII names ("Muenchen", "Sao Paulo") stay readable.
    selectLargeSmoothFont();
    epaper.drawString(
        text_render::ellipsize(epaper, lineAbove, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 - config::ui(70) + smoothCenterYAdjust(), 1);
    // TFT_eSPI treats loadFont as sticky: setFreeFont alone won't switch
    // back. Unload so the subsequent selectMediumFont() actually applies.
    unloadSmoothFontIfLoaded();
  }
  selectMediumFont();
  epaper.drawString(
      text_render::ellipsize(epaper, message, config::PANEL_WIDTH - config::ui(60)),
      config::PANEL_WIDTH / 2,
      config::PANEL_HEIGHT / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectSmallFont();
    epaper.drawString(
        text_render::ellipsize(epaper, detail, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 + config::ui(25), 1);
  }
  if (!helpBelow.isEmpty()) {
    selectSmallFont();
    epaper.drawString(
        text_render::ellipsize(epaper, helpBelow, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT - config::ui(24), 1);
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
  unloadSmoothFontIfLoaded();
  epaper.drawFastHLine(config::ui(10), config::ui(44),
                       config::PANEL_WIDTH - config::ui(20), PANEL_BLACK);
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
        weather_config::runtime::weatherBackgroundEnabled() ? PANEL_BLACK
                                                            : PANEL_MUTED;
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
  const int mainBottom = config::PANEL_HEIGHT * 62 / 100;
  const int mainCenterY = (mainTop + mainBottom) / 2;
  const int leftDividerX = config::PANEL_WIDTH * 34 / 100;
  // Center the hero icon inside the left pane (0 .. leftDividerX)
  // instead of at a hard-coded 19% offset that was slightly off.
  const int iconX = leftDividerX / 2;
  const int temperatureX = config::PANEL_WIDTH * 49 / 100;
  const int detailX = config::PANEL_WIDTH * 83 / 100;

  drawWeatherIcon(iconX, mainCenterY,
                  min(config::PANEL_WIDTH * 27 / 100,
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

  epaper.drawFastVLine(config::PANEL_WIDTH * 66 / 100,
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

  const int detailWidth = config::PANEL_WIDTH * 31 / 100;
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
                       config::PANEL_WIDTH - config::ui(20), PANEL_MUTED);
  const int forecastTop = mainBottom + config::ui(4);
  const int footerTop = config::PANEL_HEIGHT - config::ui(30);
  const int cardWidth =
      (config::PANEL_WIDTH - config::ui(20)) / config::FORECAST_DAYS;
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
  drawWeatherIcon(iconX, centerY, min(height / 5, config::ui(40)),
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

void renderFooter(const WeatherData& weather) {
  const int top = config::PANEL_HEIGHT - config::ui(30);
  // Anchor the label baseline to the actual band vertical center so the
  // text visually sits in the middle of the strip (previously the label
  // was 2 px above centre for a 30 px band, which was noticeable on
  // solid backgrounds).
  const int labelY = (top + config::PANEL_HEIGHT) / 2;
  text_render::fillStatusBackground(epaper, top, config::PANEL_HEIGHT - top, config::PANEL_WIDTH, config::PANEL_HEIGHT, PANEL_STATUS_BACKGROUND, PANEL_STATUS_DITHERED, PANEL_STATUS_DITHER_COLOR, PANEL_STATUS_DITHER_THRESHOLD);
  epaper.drawFastHLine(config::ui(10), top,
                       config::PANEL_WIDTH - config::ui(20), PANEL_MUTED);
  selectSmallSmoothFont();
  // Smooth fonts always anti-alias against an explicit background color;
  // in transparent mode (bgFill=false) the AA blender still needs the
  // correct bg or every glyph collapses to solid PANEL_BLACK.  On a
  // dithered status bar we keep bgFill off so glyphs don't punch solid
  // rectangles through the dither pattern; on solid bars we fill for
  // crisp edges.
  epaper.setTextColor(PANEL_BLACK, PANEL_STATUS_BACKGROUND, !PANEL_STATUS_DITHERED);
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
  const int locationLeft = config::PANEL_WIDTH - rightPad
                           - epaper.textWidth(locationText);
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(locationText, config::PANEL_WIDTH - rightPad,
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
  unloadSmoothFontIfLoaded();
  selectSmallFont();
}

void drawAlertBar(const WeatherData& weather) {
  if (weather.alertTitle.isEmpty()) return;
  const int top = config::ui(46);
  const int height = config::ui(22);
  epaper.fillRect(0, top, config::PANEL_WIDTH, height, PANEL_LIGHT);
  epaper.drawFastHLine(config::ui(10), top + height,
                       config::PANEL_WIDTH - config::ui(20), PANEL_MUTED);
  setStripTextColor(PANEL_BLACK, PANEL_LIGHT);
  epaper.setTextDatum(MC_DATUM);
  selectSmallFont();
  String line = "! Alert: " + weather.alertTitle;
  if (weather.alertOtherCount > 0) {
    line += " (+" + String(weather.alertOtherCount) + " more)";
  }
  epaper.drawString(
      text_render::ellipsize(epaper, line,
                             config::PANEL_WIDTH - config::ui(24)),
      config::PANEL_WIDTH / 2, top + height / 2, 1);
}

void renderWeather(const WeatherData& weather) {
  // Paint the ink-wash landscape first so the header, icons, and text
  // render on top of it. Payloads are pre-baked per model (2bpp on the
  // gray panels, 1bpp black-and-white on the Spectra-6 panels) so the
  // blit covers every pixel and we skip the fillSprite step. When the
  // user has toggled the background off in /settings, fall back to a
  // plain white sprite instead.
  if (weather_config::runtime::weatherBackgroundEnabled()) {
    weather_background::draw(
        epaper, weather_background::themeForWmoCode(weather.weatherCode));
  } else {
    epaper.fillSprite(PANEL_WHITE);
  }
  drawHeader(weather);
  drawAlertBar(weather);
#if RETERMINAL_MODEL == 1004
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
  pinMode(PIN_SD_ENABLE, OUTPUT);
  digitalWrite(PIN_SD_ENABLE, LOW);
  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);

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
    LOG.printf("[sleep] %llu seconds; GPIO3/GPIO4/GPIO5 wake enabled\n",
               static_cast<unsigned long long>(sleepSeconds));
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
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
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

  // Unified green-button boot gesture. Applies on both cold boot and
  // deep-sleep green wakes. A first beep marks the press being registered;
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
        LOG.printf("[gesture] green released after %u ms (< %u ms min) -> ignored\n",
                   static_cast<unsigned>(releasedAtMs),
                   static_cast<unsigned>(kPortalMinMs));
        gesture = GreenGesture::None;
      } else {
        LOG.printf("[gesture] green released after %u ms -> portal\n",
                   static_cast<unsigned>(releasedAtMs));
        gesture = GreenGesture::PortalRequest;
      }
    } else {
      LOG.printf("[gesture] green still held at %u ms -> screenshot\n",
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
               gesture == GreenGesture::PortalRequest ? "green-tap" : "auto");
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
    epaper.begin();
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
    // We used to call sd_card::mount here (which internally does
    // epaper_setup::finalize), but on E1001 that left SPI in a state
    // TFT_eSPI's epaper.update() would then trample, so subsequent SD
    // ops via /browse would fail every time with 'bus reset ... failed'.
    // The pre-SD portal path called epaper_setup::finalize directly for
    // the same reason: without it, E1001 Gray4 pushes silently vanish.
    // We call it here for the same effect, then defer sd_card::mount
    // until after the panel refresh completes (below) so SD.begin() is
    // the last configurator to touch the SPI bus.
    epaper_setup::finalize(epaper.getSPIinstance());
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
      info.wifiPayload = config_portal::wifiQrPayload(
          info.ssid,
          info.wifiPassword.length() ? info.wifiPassword.c_str() : nullptr);
      info.urlPayload = config_portal::urlQrPayload(
          config_portal::currentIp(), config_portal::currentPort(), "/wifi");
      info.fonts.titleFont = titleFont;
      info.fonts.subtitleFont = subtitleFont;
      info.fonts.captionFont = captionFont;
      info.fonts.detailFont = detailFont;
      LOG.println("[portal] rendering QR splash");
      const uint32_t drawStart = millis();
      config_portal::ui::renderPortalScreen<EPaper>(
          epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT, PANEL_BLACK,
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
      while (!config_portal::rebootRequested() &&
             !sd_web_portal::exitRequested()) {
        config_portal::loop();
        const uint32_t nowMs = millis();
        // Green button in the portal = reboot the device. Convenient exit
        // once you've saved settings on your phone, matching the "Reboot"
        // button on /reset. Debounced at 50 ms.
        if (!digitalRead(PIN_BUTTON_GREEN)) {
          if (greenLowSinceMs == 0) {
            greenLowSinceMs = nowMs;
          } else if (nowMs - greenLowSinceMs >= 50) {
            LOG.println("[portal] green button pressed -> reboot");
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
    // green-button gesture handler above has already decided whether
    // this boot is a portal request or a screenshot request.
    hardware::beep();
  }

  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone Weather / %s\n",
             MODEL_NAME, COLOR_MODE_NAME);
  LOG.println("============================================");
  LOG.printf("[boot] wake cause=%d pins=0x%llx, PSRAM=%luK, "
             "GPIO3=%s GPIO4=%s GPIO5=%s\n",
             wakeCause, static_cast<unsigned long long>(wakePins),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024),
             greenWokeDevice
                 ? (screenshotRequested ? "long-press" : "short-press")
                 : "idle",
             rightWokeDevice ? "wake" : "idle",
             leftWokeDevice ? "wake" : "idle");
  if (screenshotRequested) {
    LOG.println("[screenshot] green-button long press requested export");
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
  epaper.begin();
  sdReady = sd_card::mount(epaper.getSPIinstance(), config::CACHE_DIR);
  if (sdReady && weather_config::runtime::logToSd()) {
    log_sd_sink::install(appLog);
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
  LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());
  LOG.printf("[location] %s (%.4f, %.4f)\n", locationLabel.c_str(),
             weather_config::runtime::latitude(),
             weather_config::runtime::longitude());

  // Cold-boot "Connecting to Wi-Fi" splash. Pushed BEFORE any
  // initGrayMode() call so it renders as a fast 1bpp partial refresh
  // (~1-2 s) rather than paying a full Gray4/Gray16 waveform (~5 s)
  // for a screen that gets replaced the moment the weather frame is
  // ready. renderStatus() ends with updatePanel(), so the splash is
  // actually on the panel before we block on Wi-Fi.
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(weather_wifi::ssid()), connectionDetail,
                 locationLabel,
                 "To configure device - from sleep, hold green for 2 seconds");
  }
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

  const bool networkRequired = buttonWake || ntpDue || !cacheLoaded;
  const bool networkAvailable =
      networkRequired && wifi_sta::connectStation(weather_wifi::ssid(), weather_wifi::password(), config::WIFI_TIMEOUT_MS, &liveFailureReason);
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
    renderStatus("Connecting to " + String(weather_wifi::ssid()), warning, locationLabel);
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
        " minutes. Press the green button to retry now.";
    const String help =
        "Keep the green button pressed for 2 seconds to reconfigure.";
    renderStatus("Weather unavailable", detail, failureSummary, help);
    powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    return;
  }

  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
