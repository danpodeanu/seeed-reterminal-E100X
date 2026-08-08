// SMPTE-inspired test pattern for the reTerminal E-series e-paper panels.
//
// Renders a familiar TV colour-bar layout adapted to whatever palette
// the selected panel actually supports:
//
//   E1001 : four grey shades (Gray4).            800x480 landscape.
//   E1002 : six-colour Spectra E6.               800x480 landscape.
//   E1003 : sixteen grey shades (Gray16).        1872x1404 landscape.
//   E1004 : six-colour Spectra E6.               1200x1600 portrait.
//   E1005 : four grey shades + interactive GT911 test. 480x800 portrait.
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
// After the refresh benchmark, every front button beeps. The primary green/OK
// button enters deep sleep; any front button wakes, beeps, and redraws.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "board_pins.h"
#include "driver.h"
#include "e1005_fast_refresh.h"
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
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr const char* PANEL_LABEL = "reTerminal Sticky E1005 - Gray4 + Touch";
constexpr uint32_t PALETTE[] = {TFT_GRAY_3, TFT_GRAY_2, TFT_GRAY_1, TFT_GRAY_0};
constexpr const char* PALETTE_NAMES[] = {"W", "L", "D", "K"};
constexpr bool PALETTE_DARK[] = {false, false, true, true};
constexpr uint32_t RAMP[] = {TFT_GRAY_3, TFT_GRAY_2, TFT_GRAY_1, TFT_GRAY_0};
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, 1004, or 1005"
#endif

constexpr int PALETTE_COUNT = sizeof(PALETTE) / sizeof(PALETTE[0]);
constexpr int RAMP_COUNT = sizeof(RAMP) / sizeof(RAMP[0]);
constexpr int BARS_HEIGHT = (PANEL_HEIGHT * 2) / 3;
constexpr int CAST_TOP = BARS_HEIGHT;
constexpr int CAST_HEIGHT = PANEL_HEIGHT / 12;
constexpr int FOOTER_TOP = CAST_TOP + CAST_HEIGHT;
constexpr int FOOTER_HEIGHT = PANEL_HEIGHT - FOOTER_TOP;
constexpr uint32_t kButtonDebounceMs = 30;

struct ButtonState {
  int pin;
  const char* name;
  int stableLevel;
  int sampledLevel;
  uint32_t changedAtMs;
};

#if RETERMINAL_MODEL == 1005
ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "OK / power", HIGH, HIGH, 0},
    {board::PIN_BUTTON_1, "UP", HIGH, HIGH, 0},
    {board::PIN_BUTTON_2, "DOWN", HIGH, HIGH, 0},
};
constexpr const char* PRIMARY_BUTTON_NAME = "OK";
#else
ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "GREEN", HIGH, HIGH, 0},
    {board::PIN_BUTTON_1, "RIGHT", HIGH, HIGH, 0},
    {board::PIN_BUTTON_2, "LEFT", HIGH, HIGH, 0},
};
constexpr const char* PRIMARY_BUTTON_NAME = "GREEN";
#endif

void configureButtons() {
  for (ButtonState& button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableLevel = digitalRead(button.pin);
    button.sampledLevel = button.stableLevel;
    button.changedAtMs = millis();
  }
}

ButtonState* pollButtonPress() {
  const uint32_t now = millis();
  for (ButtonState& button : buttons) {
    const int level = digitalRead(button.pin);
    if (level != button.sampledLevel) {
      button.sampledLevel = level;
      button.changedAtMs = now;
    }
    if (level != button.stableLevel &&
        static_cast<uint32_t>(now - button.changedAtMs) >=
            kButtonDebounceMs) {
      button.stableLevel = level;
      if (level == LOW) return &button;
    }
  }
  return nullptr;
}

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

  drawColorBars(0, BARS_HEIGHT);
  drawCastellations(CAST_TOP, CAST_HEIGHT);
  drawFooter(FOOTER_TOP, FOOTER_HEIGHT);

  // Restore default text state so anything printed later renders sanely.
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
}

#if RETERMINAL_MODEL == 1005
TwoWire touchWire(0);
Gt911Touch touch;
E1005FastRefresh fastRefresh(epaper);
bool touchReady = false;
bool touchActive = false;

struct PatternRegion {
  int left;
  int top;
  int width;
  int height;
};

PatternRegion columnRegion(int x, int top, int height, int columnCount) {
  const int baseWidth = PANEL_WIDTH / columnCount;
  const int remainder = PANEL_WIDTH - baseWidth * columnCount;
  int left = 0;
  for (int column = 0; column < columnCount; ++column) {
    const int width = baseWidth + (column < remainder ? 1 : 0);
    if (x < left + width || column == columnCount - 1) {
      return {left, top, width, height};
    }
    left += width;
  }
  return {0, top, PANEL_WIDTH, height};
}

PatternRegion touchedPatternRegion(const Gt911Touch::Point& point) {
  if (point.y < BARS_HEIGHT) {
    return columnRegion(point.x, 0, BARS_HEIGHT, PALETTE_COUNT);
  }
  if (point.y < FOOTER_TOP) {
    return columnRegion(point.x, CAST_TOP, CAST_HEIGHT, PALETTE_COUNT);
  }

  const int bannerHeight = (FOOTER_HEIGHT * 2) / 3;
  const int rampTop = FOOTER_TOP + bannerHeight;
  if (point.y < rampTop) {
    return {0, FOOTER_TOP, PANEL_WIDTH, bannerHeight};
  }
  return columnRegion(point.x, rampTop, PANEL_HEIGHT - rampTop, RAMP_COUNT);
}

void renderMonoBenchmarkPattern() {
  epaper.fillSprite(TFT_WHITE);
  constexpr int kBlockSize = 80;
  for (int y = 0; y < PANEL_HEIGHT; y += kBlockSize) {
    for (int x = 0; x < PANEL_WIDTH; x += kBlockSize) {
      if (((x / kBlockSize) + (y / kBlockSize)) % 2 != 0) {
        epaper.fillRect(x, y, kBlockSize, kBlockSize, TFT_BLACK);
      }
    }
  }
}

void runE1005RefreshBenchmark() {
  renderMonoBenchmarkPattern();
  LOG.println("[benchmark] monochrome full refresh starting");
  const uint32_t monoFullStartedUs = micros();
  epaper.update();
  const uint32_t monoFullUs = micros() - monoFullStartedUs;
  LOG.printf("[benchmark] monochrome full refresh=%lu us\n",
             static_cast<unsigned long>(monoFullUs));

  uint32_t monoFastUs = 0;
  const E1005FastRefresh::Result baselineResult = fastRefresh.begin();
  if (baselineResult == E1005FastRefresh::Result::Ok) {
    constexpr E1005FastRefresh::Region kGameRegion = {30, 130, 420, 550};
    epaper.fillRect(kGameRegion.x, kGameRegion.y, kGameRegion.width,
                   kGameRegion.height, TFT_BLACK);
    E1005FastRefresh::Timing timing;
    const E1005FastRefresh::Result result =
        fastRefresh.refresh(kGameRegion, timing);
    if (result == E1005FastRefresh::Result::Ok) {
      monoFastUs = timing.totalUs;
      LOG.printf(
          "[benchmark] monochrome fast 420x550 refresh=%lu us "
          "(prepare=%lu transfer=%lu panel=%lu reseed=%lu)\n",
          static_cast<unsigned long>(timing.totalUs),
          static_cast<unsigned long>(timing.prepareUs),
          static_cast<unsigned long>(timing.transferUs),
          static_cast<unsigned long>(timing.panelUs),
          static_cast<unsigned long>(timing.reseedUs));
    } else {
      LOG.printf("[benchmark] monochrome fast refresh failed: %s\n",
                 E1005FastRefresh::resultMessage(result));
    }
  } else {
    LOG.printf("[benchmark] monochrome baseline failed: %s\n",
               E1005FastRefresh::resultMessage(baselineResult));
  }
  fastRefresh.end();
  epaper.sleep();

  epaper.initGrayMode(GRAY_LEVEL4);
  epaper.setRotation(panel_traits::DISPLAY_ROTATION);
  renderPattern();
  LOG.println("[benchmark] Gray4 full refresh starting");
  const uint32_t grayFullStartedUs = micros();
  epaper.update();
  const uint32_t grayFullUs = micros() - grayFullStartedUs;
  LOG.printf("[benchmark] Gray4 full refresh=%lu us\n",
             static_cast<unsigned long>(grayFullUs));
  if (monoFastUs != 0) {
    const uint32_t slowdownTenths =
        static_cast<uint32_t>((static_cast<uint64_t>(grayFullUs) * 10U +
                               monoFastUs / 2U) /
                              monoFastUs);
    LOG.printf(
        "[benchmark] Gray4 / monochrome-fast slowdown=%lu.%lux\n",
        static_cast<unsigned long>(slowdownTenths / 10U),
        static_cast<unsigned long>(slowdownTenths % 10U));
  }
}

bool invertGray4Region(const PatternRegion& region) {
  auto* framebuffer = static_cast<uint8_t*>(epaper.getPointer());
  if (!framebuffer) return false;

  constexpr int kNativeStrideBytes = PANEL_HEIGHT / 2;
  for (int y = region.top; y < region.top + region.height; ++y) {
    const int nativeX = PANEL_HEIGHT - y - 1;
    const uint8_t mask = (nativeX & 1) == 0 ? 0x30 : 0x03;
    for (int x = region.left; x < region.left + region.width; ++x) {
      framebuffer[x * kNativeStrideBytes + nativeX / 2] ^= mask;
    }
  }
  return true;
}

void showTouchFeedback(const Gt911Touch::Point& point,
                       uint32_t touchDetectedUs) {
  const PatternRegion region = touchedPatternRegion(point);
  if (!invertGray4Region(region)) {
    LOG.println("[touch] Gray4 framebuffer is unavailable");
    touchReady = false;
    return;
  }
  epaper.update();
  const uint32_t invertedCompleteUs = micros();

  const uint32_t restoreStartedUs = micros();
  invertGray4Region(region);
  epaper.update();
  const uint32_t restoredCompleteUs = micros();
  LOG.printf(
      "[touch] Gray4 invert latency=%lu us, restore latency=%lu us, "
      "touch cycle=%lu us\n",
      static_cast<unsigned long>(invertedCompleteUs - touchDetectedUs),
      static_cast<unsigned long>(restoredCompleteUs - restoreStartedUs),
      static_cast<unsigned long>(restoredCompleteUs - touchDetectedUs));
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

  const uint32_t touchDetectedUs = micros();
  touchActive = true;
  showTouchFeedback(point, touchDetectedUs);
  LOG.printf("[touch] x=%u y=%u size=%u id=%u\n", point.x, point.y,
             point.size, point.id);
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
  runE1005RefreshBenchmark();
#else
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
#endif

  // A second, higher-pitched beep marks a completed refresh. Together
  // the pair makes it obvious over serial-less USB whether we made it
  // through the (multi-second) e-paper update.
  hardware::beep();
  configureButtons();

#if RETERMINAL_MODEL == 1005
  touchReady = touch.begin(touchWire);
  if (touchReady) {
    LOG.printf("[touch] GT%s ready at 0x%02X, sensor=%ux%u\n",
               touch.productId(), touch.address(), touch.sensorWidth(),
               touch.sensorHeight());
    LOG.println(
        "[panel-test] touch a block to invert and restore its Gray4 pixels");
  } else {
    LOG.println("[touch] GT911 initialization failed");
  }
#endif
  LOG.printf(
      "[panel-test] press any button to beep; press and release %s to sleep\n",
      PRIMARY_BUTTON_NAME);
}

void loop() {
#if RETERMINAL_MODEL == 1005
  pollTouch();
#endif
  if (ButtonState* button = pollButtonPress()) {
    LOG.printf("[button] %s pressed\n", button->name);
    hardware::beep();
    if (button->pin == board::PIN_BUTTON_0) {
      LOG.printf("[panel-test] entering deep sleep after %s release\n",
                 PRIMARY_BUTTON_NAME);
      while (digitalRead(button->pin) == LOW) delay(10);
#if RETERMINAL_MODEL == 1005
      touch.end();
#endif
      powerDownAndSleep();
    }
  }
#if RETERMINAL_MODEL != 1005
  delay(5);
#endif
}
