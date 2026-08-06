// SMPTE-inspired test pattern for the reTerminal E-series e-paper panels.
//
// Renders a familiar TV colour-bar layout adapted to whatever palette
// the selected panel actually supports:
//
//   E1001 : four grey shades (Gray4).            800x480 landscape.
//   E1002 : six-colour Spectra E6.               800x480 landscape.
//   E1003 : sixteen grey shades (Gray16).        1872x1404 landscape.
//   E1004 : six-colour Spectra E6.               1200x1600 portrait.
//   E1005 : monochrome + interactive GT911 test. 480x800 portrait.
//
// Layout (all panels):
//
//   Top ~2/3    : one full-height bar per palette entry, tallest colour
//                 on the left, drawn at the panel's native code so no
//                 dithering can mask a dead colour lane.
//   Middle ~1/12: "castellation" strip - each colour bar reversed
//                 against black. Kicks out obvious refresh-artefact
//                 patterns.
//   Bottom ~1/4 : model banner + solid black + solid white + a
//                 grayscale ramp using intermediate codes if the panel
//                 has more than two greys.
//
// After the first refresh the sketch drops into deep sleep. Hit the
// reset button (or unplug/replug USB) to redraw.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "board_pins.h"
#include "driver.h"
#include "epaper_setup.h"
#include "hardware.h"
#include "gt911_touch.h"
#include "panel_traits.h"
#include "peripheral_power.h"
#include "power_latch.h"

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check common/include/driver.h"
#endif

// Definition of the extern declared in app_logger.h. Kept at global
// scope so the LOG macro resolves without ambiguity.
TimestampedLogger appLog(Serial1);

// Global so both the render helpers (in the anonymous namespace below)
// and setup() can address it.
EPaper epaper;

namespace {

constexpr int PANEL_WIDTH = panel_traits::WIDTH;
constexpr int PANEL_HEIGHT = panel_traits::HEIGHT;

// Panel geometry + palette selection. Keeping the arrays static and the
// counts constexpr makes every draw call inlineable and avoids any
// PSRAM allocation for what is essentially a boot-and-sleep utility.
#if RETERMINAL_MODEL == 1001
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr const char* PANEL_LABEL = "reTerminal E1001 - Gray4";
// Darkest to lightest so the bars order white->...->black when we draw
// them right-to-left below.
constexpr uint32_t PALETTE[] = {TFT_GRAY_3, TFT_GRAY_2, TFT_GRAY_1, TFT_GRAY_0};
constexpr const char* PALETTE_NAMES[] = {"W", "L", "D", "K"};
// true = use white text on this swatch; false = use black text.
constexpr bool PALETTE_DARK[] = {false, false, true, true};
constexpr uint32_t RAMP[] = {TFT_GRAY_3, TFT_GRAY_2, TFT_GRAY_1, TFT_GRAY_0};
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr const char* PANEL_LABEL = "reTerminal E1002 - Spectra E6";
// SMPTE-style: brightest first, chromatic ordering by hue.
constexpr uint32_t PALETTE[] = {TFT_WHITE, TFT_YELLOW, TFT_GREEN,
                                TFT_BLUE, TFT_RED, TFT_BLACK};
constexpr const char* PALETTE_NAMES[] = {"W", "Y", "G", "B", "R", "K"};
constexpr bool PALETTE_DARK[] = {false, false, false, true, true, true};
constexpr uint32_t RAMP[] = {TFT_WHITE, TFT_BLACK};
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr const char* PANEL_LABEL = "reTerminal E1003 - Gray16";
constexpr uint32_t PALETTE[] = {TFT_GRAY_15, TFT_GRAY_14, TFT_GRAY_13, TFT_GRAY_12,
                                TFT_GRAY_11, TFT_GRAY_10, TFT_GRAY_9, TFT_GRAY_8,
                                TFT_GRAY_7, TFT_GRAY_6, TFT_GRAY_5, TFT_GRAY_4,
                                TFT_GRAY_3, TFT_GRAY_2, TFT_GRAY_1, TFT_GRAY_0};
constexpr const char* PALETTE_NAMES[] = {"F", "E", "D", "C", "B", "A", "9", "8",
                                         "7", "6", "5", "4", "3", "2", "1", "0"};
// Anything at 0x07 or darker uses white ink so the label reads over the
// swatch; the mid-scale codes stay black-on-gray.
constexpr bool PALETTE_DARK[] = {false, false, false, false, false, false, false, false,
                                 true,  true,  true,  true,  true,  true,  true,  true};
constexpr uint32_t RAMP[] = {TFT_GRAY_15, TFT_GRAY_13, TFT_GRAY_11, TFT_GRAY_9,
                             TFT_GRAY_7, TFT_GRAY_5, TFT_GRAY_3, TFT_GRAY_0};
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr const char* PANEL_LABEL = "reTerminal E1004 - Spectra E6";
constexpr uint32_t PALETTE[] = {TFT_WHITE, TFT_YELLOW, TFT_GREEN,
                                TFT_BLUE, TFT_RED, TFT_BLACK};
constexpr const char* PALETTE_NAMES[] = {"W", "Y", "G", "B", "R", "K"};
constexpr bool PALETTE_DARK[] = {false, false, false, true, true, true};
constexpr uint32_t RAMP[] = {TFT_WHITE, TFT_BLACK};
#elif RETERMINAL_MODEL == 1005
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr const char* PANEL_LABEL = "reTerminal Sticky E1005 - Mono + Touch";
constexpr uint32_t PALETTE[] = {TFT_WHITE, TFT_BLACK};
constexpr const char* PALETTE_NAMES[] = {"W", "K"};
constexpr bool PALETTE_DARK[] = {false, true};
constexpr uint32_t RAMP[] = {TFT_WHITE, TFT_BLACK};
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, 1004, or 1005"
#endif

constexpr int PALETTE_COUNT = sizeof(PALETTE) / sizeof(PALETTE[0]);
constexpr int RAMP_COUNT = sizeof(RAMP) / sizeof(RAMP[0]);

// SMPTE castellation colours cycle white/palette/black/palette/... to
// make a stripe that alternates whichever colour is above it against
// pure black. Simpler than the classic bars but keeps the visual cue.
uint32_t castellationColor(int barIndex) {
  const uint32_t primary = PALETTE[barIndex];
  return (barIndex & 1) ? PANEL_BLACK : primary;
}

// Font selection mirrors the app conventions so labels are legible on
// every panel size without needing a per-model layout pass.
void selectLabelFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold12pt7b);
#else
  epaper.setFreeFont(&FreeSansBold9pt7b);
#endif
}

void selectBannerFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold24pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold18pt7b);
#else
  epaper.setFreeFont(&FreeSansBold12pt7b);
#endif
}

void drawColorBars(int top, int height) {
  // Standard SMPTE dimensions: the first (leftmost) bar gets the extra
  // pixel when width doesn't divide evenly, matching how a broadcast
  // generator would treat rounding error.
  const int base = PANEL_WIDTH / PALETTE_COUNT;
  const int remainder = PANEL_WIDTH - base * PALETTE_COUNT;
  const int labelInset = height / 20;
  int x = 0;
  for (int i = 0; i < PALETTE_COUNT; ++i) {
    const int barWidth = base + (i < remainder ? 1 : 0);
    epaper.fillRect(x, top, barWidth, height, PALETTE[i]);
    // Draw a thin black separator between adjacent bars so their edges
    // read on Gray16 even when neighbouring shades look nearly the same
    // to a camera. Skip the very first bar (nothing to the left of it).
    if (i > 0) {
      epaper.drawFastVLine(x, top, height, PANEL_BLACK);
    }
    // Per-bar text contrast: PALETTE_DARK[i] flags the swatches where
    // black ink would disappear (blue/red/black, plus the darker half
    // of any grayscale palette).
    const uint32_t label = PALETTE_DARK[i] ? PANEL_WHITE : PANEL_BLACK;
    epaper.setTextColor(label, PALETTE[i], true);
    selectLabelFont();
    // Label at both the top and the bottom of the bar so the code stays
    // findable regardless of where the observer looks.
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString(PALETTE_NAMES[i], x + barWidth / 2, top + labelInset, 1);
    epaper.setTextDatum(BC_DATUM);
    epaper.drawString(PALETTE_NAMES[i], x + barWidth / 2,
                      top + height - labelInset, 1);
    x += barWidth;
  }
}

void drawCastellations(int top, int height) {
  const int base = PANEL_WIDTH / PALETTE_COUNT;
  const int remainder = PANEL_WIDTH - base * PALETTE_COUNT;
  int x = 0;
  for (int i = 0; i < PALETTE_COUNT; ++i) {
    const int barWidth = base + (i < remainder ? 1 : 0);
    epaper.fillRect(x, top, barWidth, height, castellationColor(i));
    x += barWidth;
  }
}

void drawFooter(int top, int height) {
  // Split the footer into a banner (top 2/3) and a ramp strip (bottom
  // 1/3). Both span the full panel width so the model name never gets
  // clipped by neighbouring patches, and the ramp can dedicate all its
  // real estate to showing every intermediate shade cleanly. Pure black
  // and pure white are already covered by the color-bars section above,
  // so no separate reference patches are needed here.
  const int bannerHeight = (height * 2) / 3;
  const int rampTop = top + bannerHeight;
  const int rampHeight = height - bannerHeight;

  epaper.fillRect(0, top, PANEL_WIDTH, bannerHeight, PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectBannerFont();
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString(PANEL_LABEL, PANEL_WIDTH / 2,
                    top + bannerHeight / 2 - bannerHeight / 6, 1);
  selectLabelFont();
  const String subtitle = String(PALETTE_COUNT) + " colours - " +
                          String(PANEL_WIDTH) + "x" + String(PANEL_HEIGHT);
  epaper.drawString(subtitle, PANEL_WIDTH / 2,
                    top + bannerHeight / 2 + bannerHeight / 4, 1);

  // 1px separator between banner and ramp so the transition reads on
  // panels where the extreme ramp step is white.
  epaper.drawFastHLine(0, rampTop, PANEL_WIDTH, PANEL_BLACK);

  // Full-width ramp: on grayscale panels every intermediate shade shows
  // up, so a dead LUT entry stands out. On six-colour panels the ramp
  // degenerates to black + white and just verifies bit-depth 0 and max.
  const int stepBase = PANEL_WIDTH / RAMP_COUNT;
  const int stepRemainder = PANEL_WIDTH - stepBase * RAMP_COUNT;
  int rx = 0;
  for (int i = 0; i < RAMP_COUNT; ++i) {
    const int stepWidth = stepBase + (i < stepRemainder ? 1 : 0);
    epaper.fillRect(rx, rampTop + 1, stepWidth, rampHeight - 1, RAMP[i]);
    if (i > 0) {
      epaper.drawFastVLine(rx, rampTop + 1, rampHeight - 1, PANEL_BLACK);
    }
    rx += stepWidth;
  }
}

void renderPattern() {
  epaper.fillSprite(PANEL_WHITE);

  // Classic SMPTE proportions: bars 2/3, castellations 1/12, footer 1/4.
  const int barsTop = 0;
  const int barsHeight = (PANEL_HEIGHT * 2) / 3;
  const int castTop = barsHeight;
  const int castHeight = PANEL_HEIGHT / 12;
  const int footerTop = castTop + castHeight;
  const int footerHeight = PANEL_HEIGHT - footerTop;

  drawColorBars(barsTop, barsHeight);
  drawCastellations(castTop, castHeight);
  drawFooter(footerTop, footerHeight);

  // Restore default text state so anything printed later renders sanely.
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
}

#if RETERMINAL_MODEL == 1005
TwoWire touchWire(0);
Gt911Touch touch;
bool touchReady = false;
bool touchActive = false;

bool invertTouchRegion(int left, int top, int width, int height) {
  auto* framebuffer = static_cast<uint8_t*>(epaper.getPointer());
  if (!framebuffer) return false;

  constexpr int kNativeStrideBytes = PANEL_HEIGHT / 8;
  for (int y = top; y < top + height; ++y) {
    const int nativeX = PANEL_HEIGHT - y - 1;
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (nativeX & 7));
    for (int x = left; x < left + width; ++x) {
      const int nativeY = x;
      framebuffer[nativeY * kNativeStrideBytes + nativeX / 8] ^= mask;
    }
  }
  return true;
}

void showTouchFeedback(const Gt911Touch::Point& point) {
  constexpr int kFeedbackSize = 72;
  constexpr int kFeedbackHalfSize = kFeedbackSize / 2;
  constexpr uint32_t kFeedbackHoldMs = 400;

  const int left = max(0, static_cast<int>(point.x) - kFeedbackHalfSize);
  const int top = max(0, static_cast<int>(point.y) - kFeedbackHalfSize);
  const int right =
      min(PANEL_WIDTH, static_cast<int>(point.x) + kFeedbackHalfSize);
  const int bottom =
      min(PANEL_HEIGHT, static_cast<int>(point.y) + kFeedbackHalfSize);
  const int width = right - left;
  const int height = bottom - top;
  if (width < 1 || height < 1 ||
      !invertTouchRegion(left, top, width, height)) {
    return;
  }

  hardware::beep();
  epaper.updataPartial(left, top, width, height);
  delay(kFeedbackHoldMs);
  invertTouchRegion(left, top, width, height);
  epaper.updataPartial(left, top, width, height);
}

void pollTouch() {
  if (!touchReady) return;
  Gt911Touch::Point point = {};
  const Gt911Touch::PollResult result = touch.poll(point);
  if (result == Gt911Touch::PollResult::Release) {
    touchActive = false;
    return;
  }
  if (result != Gt911Touch::PollResult::Touch || touchActive) return;

  touchActive = true;
  LOG.printf("[touch] x=%u y=%u size=%u id=%u\n", point.x, point.y,
             point.size, point.id);
  showTouchFeedback(point);
}
#endif

void powerDownAndSleep() {
  const int kButtons[] = {
      board::PIN_BUTTON_0,
      board::PIN_BUTTON_1,
      board::PIN_BUTTON_2,
  };
  bool rtcPinsReady = true;
  for (const int pin : kButtons) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    rtc_gpio_hold_dis(gpio);
    rtcPinsReady =
        rtc_gpio_init(gpio) == ESP_OK &&
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK &&
        rtc_gpio_pullup_en(gpio) == ESP_OK &&
        rtc_gpio_pulldown_dis(gpio) == ESP_OK &&
        rtcPinsReady;
  }
  const uint64_t kWakeMask =
      (1ULL << board::PIN_BUTTON_0) |
      (1ULL << board::PIN_BUTTON_1) |
      (1ULL << board::PIN_BUTTON_2);
  const esp_err_t wakeResult =
      rtcPinsReady
          ? esp_sleep_enable_ext1_wakeup(kWakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  LOG.printf("[panel-test] wake config: %s\n", esp_err_to_name(wakeResult));
  LOG.flush();
  delay(50);
#if RETERMINAL_MODEL == 1005
  epaper.getSPIinstance().end();
  pinMode(board::PIN_SD_CS, INPUT);
  pinMode(board::PIN_SD_SCK, INPUT);
  pinMode(board::PIN_SD_MOSI, INPUT);
  pinMode(board::PIN_SD_MISO, INPUT);
  peripheral_power::disableSd();
#endif
  peripheral_power::disable();
  power_latch::holdDuringDeepSleep();
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  power_latch::holdOn();
  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  delay(50);

  // Beep once as soon as we know we've booted, so a user pressing a
  // button gets audible confirmation even before the panel refreshes.
  hardware::beep();

  LOG.println();
  LOG.printf("[panel-test] %s\n", PANEL_LABEL);
  LOG.printf("[panel-test] %d x %d, %d palette entries\n", PANEL_WIDTH,
             PANEL_HEIGHT, PALETTE_COUNT);

#if RETERMINAL_MODEL == 1005
  // The SD socket shares SCK/MOSI with the panel but has a separate power
  // rail. Keep an inserted card powered and deselected so panel traffic
  // cannot back-power it through the SPI pins and clamp the shared bus.
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
#endif
  epaper_setup::begin(epaper);
#if RETERMINAL_MODEL == 1005
  epaper.setRotation(1);
#endif
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  // E1002 (ED2208) and E1004 (T133A01) drive their six-colour palettes
  // straight out of panel startup - no gray-mode init, matching how
  // the viewer apps hand them off directly to draw calls.
  renderPattern();
  LOG.println("[panel-test] refreshing panel");
  epaper.update();

  // A second, higher-pitched beep marks a completed refresh. Together
  // the pair makes it obvious over serial-less USB whether we made it
  // through the (multi-second) e-paper update.
  hardware::beep();

#if RETERMINAL_MODEL == 1005
  for (const int pin : {
           board::PIN_BUTTON_0,
           board::PIN_BUTTON_1,
           board::PIN_BUTTON_2,
       }) {
    pinMode(pin, INPUT_PULLUP);
  }
  touchReady = touch.begin(touchWire);
  if (touchReady) {
    LOG.printf("[touch] GT%s ready at 0x%02X, sensor=%ux%u\n",
               touch.productId(), touch.address(), touch.sensorWidth(),
               touch.sensorHeight());
    LOG.println(
        "[panel-test] touch the display to beep and invert that area");
  } else {
    LOG.println("[touch] GT911 initialization failed");
  }
  LOG.println("[panel-test] press and release OK to sleep; any button wakes");
#else
  LOG.println("[panel-test] done; sleeping - press any front button to redraw");
  powerDownAndSleep();
#endif
}

void loop() {
#if RETERMINAL_MODEL == 1005
  pollTouch();
  if (digitalRead(board::PIN_BUTTON_0) == LOW) {
    delay(30);
    if (digitalRead(board::PIN_BUTTON_0) == LOW) {
      LOG.println("[panel-test] OK pressed; entering deep sleep");
      while (digitalRead(board::PIN_BUTTON_0) == LOW) delay(10);
      touch.end();
      powerDownAndSleep();
    }
  }
  delay(20);
#else
  delay(1000);
#endif
}
