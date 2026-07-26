// SMPTE-inspired test pattern for the reTerminal E100X e-paper panels.
//
// Renders a familiar TV colour-bar layout adapted to whatever palette
// the selected panel actually supports:
//
//   E1001 : four grey shades (Gray4).            800x480 landscape.
//   E1002 : six-colour Spectra E6.               800x480 landscape.
//   E1003 : sixteen grey shades (Gray16).        1872x1404 landscape.
//   E1004 : six-colour Spectra E6.               1200x1600 portrait.
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
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "board_pins.h"
#include "driver.h"

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

// Panel geometry + palette selection. Keeping the arrays static and the
// counts constexpr makes every draw call inlineable and avoids any
// PSRAM allocation for what is essentially a boot-and-sleep utility.
#if RETERMINAL_MODEL == 1001
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;
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
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;
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
constexpr int PANEL_WIDTH = 1872;
constexpr int PANEL_HEIGHT = 1404;
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
constexpr int PANEL_WIDTH = 1200;
constexpr int PANEL_HEIGHT = 1600;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr const char* PANEL_LABEL = "reTerminal E1004 - Spectra E6";
constexpr uint32_t PALETTE[] = {TFT_WHITE, TFT_YELLOW, TFT_GREEN,
                                TFT_BLUE, TFT_RED, TFT_BLACK};
constexpr const char* PALETTE_NAMES[] = {"W", "Y", "G", "B", "R", "K"};
constexpr bool PALETTE_DARK[] = {false, false, false, true, true, true};
constexpr uint32_t RAMP[] = {TFT_WHITE, TFT_BLACK};
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, or 1004"
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
  // Left third: solid black patch for reference.
  const int leftWidth = PANEL_WIDTH / 3;
  epaper.fillRect(0, top, leftWidth, height, PANEL_BLACK);

  // Right third: solid white patch (framed 1px to keep it visible on a
  // clean e-paper flush).
  const int rightWidth = PANEL_WIDTH / 3;
  const int rightX = PANEL_WIDTH - rightWidth;
  epaper.fillRect(rightX, top, rightWidth, height, PANEL_WHITE);
  epaper.drawRect(rightX, top, rightWidth, height, PANEL_BLACK);

  // Middle third: banner text over a mid-swatch, plus the ramp along
  // the bottom edge.
  const int centerX = leftWidth;
  const int centerWidth = PANEL_WIDTH - leftWidth - rightWidth;
  const uint32_t centerBg =
      (RAMP_COUNT > 2) ? RAMP[RAMP_COUNT / 2] : PANEL_WHITE;
  const uint32_t centerFg =
      (centerBg == PANEL_WHITE) ? PANEL_BLACK : PANEL_WHITE;
  epaper.fillRect(centerX, top, centerWidth, height, centerBg);
  epaper.setTextColor(centerFg, centerBg, true);
  selectBannerFont();
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString(PANEL_LABEL, centerX + centerWidth / 2,
                    top + height / 2 - height / 6, 1);
  selectLabelFont();
  const String subtitle =
      String(PALETTE_COUNT) + " colours - " + String(PANEL_WIDTH) + "x" +
      String(PANEL_HEIGHT);
  epaper.drawString(subtitle, centerX + centerWidth / 2,
                    top + height / 2 + height / 8, 1);

  // Bottom ramp strip: on grayscale panels this shows every intermediate
  // shade, so a dead LUT entry stands out. On six-colour panels the ramp
  // degenerates to black + white and just verifies bit-depth 0 and max.
  const int rampHeight = height / 4;
  const int rampTop = top + height - rampHeight;
  const int stepBase = PANEL_WIDTH / RAMP_COUNT;
  const int stepRemainder = PANEL_WIDTH - stepBase * RAMP_COUNT;
  int rx = 0;
  for (int i = 0; i < RAMP_COUNT; ++i) {
    const int stepWidth = stepBase + (i < stepRemainder ? 1 : 0);
    epaper.fillRect(rx, rampTop, stepWidth, rampHeight, RAMP[i]);
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

void powerDownAndSleep() {
  // Wire the three front buttons (GPIO 3/4/5, all wired active-low with
  // external pull-ups on the reTerminal) as EXT1 wake sources so pressing
  // any of them redraws the pattern. Without this the panel would stay
  // as-is forever, since there is no physical EN-reset button on the
  // reTerminal E100X - the labelled "reset" is itself a GPIO.
  constexpr int kButtons[] = {3, 4, 5};
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
  constexpr uint64_t kWakeMask =
      (1ULL << 3) | (1ULL << 4) | (1ULL << 5);
  const esp_err_t wakeResult =
      rtcPinsReady
          ? esp_sleep_enable_ext1_wakeup(kWakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  LOG.printf("[panel-test] wake config: %s\n", esp_err_to_name(wakeResult));
  LOG.flush();
  delay(50);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  delay(50);
  LOG.println();
  LOG.printf("[panel-test] %s\n", PANEL_LABEL);
  LOG.printf("[panel-test] %d x %d, %d palette entries\n", PANEL_WIDTH,
             PANEL_HEIGHT, PALETTE_COUNT);

  epaper.begin();
#if RETERMINAL_MODEL == 1001
  // Enable 4-level greyscale mode; without this the UC8179 driver runs
  // in 1-bit mode and every non-zero code renders as white.
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  // Enable 16-level greyscale mode on the ED103TC2 driver. Same trap
  // as E1001 - default is 1-bit and swallows all intermediate shades.
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  renderPattern();
  LOG.println("[panel-test] refreshing panel");
  epaper.update();
  LOG.println("[panel-test] done; sleeping - press any front button to redraw");
  powerDownAndSleep();
}

void loop() {
  delay(1000);
}
