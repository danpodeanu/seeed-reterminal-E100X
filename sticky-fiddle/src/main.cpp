#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "app_logger.h"
#include "battery_gauge.h"
#include "board_pins.h"
#include "driver.h"
#include "e1005_fast_refresh.h"
#include "epaper_setup.h"
#include "fidget_activities.h"
#include "gt911_touch.h"
#include "hardware.h"
#include "low_battery.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "sd_card.h"
#include "sd_ota.h"
#include "usb_screen_capture.h"
#include "version.h"

#if RETERMINAL_MODEL != 1005
#error "Sticky Fiddle supports only reTerminal E1005"
#endif

TimestampedLogger appLog(Serial1);
EPaper epaper;
usb_screen_capture::Server usbScreenCapture;

namespace {

using sticky_fiddle::BubbleWrap;
using sticky_fiddle::FlipDots;
using sticky_fiddle::PointlessCounter;
using sticky_fiddle::RakeSegment;
using sticky_fiddle::Ripple;
using sticky_fiddle::RipplePond;
using sticky_fiddle::Squish;
using sticky_fiddle::ZenRake;

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 800;
constexpr int kStatusHeight = 50;
constexpr int kHeaderBottom = 110;
constexpr int kContentTop = 116;
constexpr int kContentBottom = 696;
constexpr int kFooterTop = 710;
constexpr int kFooterBottom = 784;
constexpr uint32_t kButtonDebounceMs = 25;
constexpr uint32_t kDeepSleepHoldMs = 2000;
constexpr uint32_t kInactivitySleepMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kBatteryCheckIntervalMs = 60UL * 1000UL;
constexpr uint32_t kRippleAgeIntervalMs = 2500;
constexpr uint32_t kSquishStepMs = 350;
constexpr uint32_t kPersistedMagic = 0x46494444UL;
constexpr uint16_t kPersistedVersion = 1;
constexpr char kAppName[] = "Sticky Fiddle";
constexpr char kBrandName[] = "STICKY FIDDLE";
constexpr E1005FastRefresh::Region kStatusRegion = {340, 0, 140, 49};
constexpr E1005FastRefresh::Region kContentRegion = {
    0, kContentTop, kScreenWidth, kContentBottom - kContentTop + 1};

struct Rect {
  int x;
  int y;
  int width;
  int height;

  bool contains(int pointX, int pointY) const {
    return pointX >= x && pointX < x + width && pointY >= y &&
           pointY < y + height;
  }
};

constexpr Rect kBackButton = {12, 58, 54, 40};
constexpr Rect kHelpButton = {414, 58, 54, 40};
constexpr Rect kResetButton = {100, kFooterTop, 280, 62};
constexpr Rect kActivityArea = {
    0, kContentTop, kScreenWidth, kContentBottom - kContentTop + 1};
constexpr Rect kMenuCards[] = {
    {18, 72, 213, 208}, {249, 72, 213, 208},
    {18, 296, 213, 208}, {249, 296, 213, 208},
    {18, 520, 213, 208}, {249, 520, 213, 208},
};

enum class Screen : uint8_t {
  Menu,
  BubbleWrap,
  ZenRake,
  FlipDots,
  RipplePond,
  PointlessCounter,
  Squish,
};

constexpr size_t kActivityCount = 6;

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint8_t screen;
  uint8_t reserved;
  uint64_t bubbles;
  uint16_t rakeCount;
  RakeSegment rakeSegments[ZenRake::kMaximumSegments];
  uint32_t flipDots[FlipDots::kWordCount];
  uint8_t rippleCount;
  Ripple ripples[RipplePond::kMaximumRipples];
  uint32_t counter;
  uint8_t squishLevel;
  uint8_t padding[3];
  uint32_t checksum;
};

RTC_DATA_ATTR PersistedState persistedState = {};

struct ButtonState {
  int pin;
  const char* name;
  int stableLevel;
  int sampledLevel;
  uint32_t changedAtMs;
  uint32_t pressedAtMs;
};

struct ButtonEvent {
  ButtonState* button = nullptr;
  uint32_t heldMs = 0;
};

ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "OK", HIGH, HIGH, 0, 0},
    {board::PIN_BUTTON_1, "UP", HIGH, HIGH, 0, 0},
    {board::PIN_BUTTON_2, "DOWN", HIGH, HIGH, 0, 0},
};

TwoWire touchWire(1);
Gt911Touch touch;
E1005FastRefresh fastRefresh(epaper);
BubbleWrap bubbles;
ZenRake rake;
FlipDots flipDots;
RipplePond pond;
PointlessCounter counter;
Squish squish;

Screen currentScreen = Screen::Menu;
bool helpVisible = false;
bool touchReady = false;
bool touchActive = false;
bool touchDirty = false;
bool lightSleepReady = false;
bool sdCardReady = false;
bool batterySampled = false;
bool externalPowerPresent = false;
bool squishRelaxing = false;
int batteryPercent = -1;
int lastFlipCell = -1;
uint32_t lastActivityAtMs = 0;
uint32_t nextBatteryCheckAtMs = 0;
uint32_t nextRippleAgeAtMs = 0;
uint32_t nextSquishStepAtMs = 0;
Gt911Touch::Point touchStart = {};
Gt911Touch::Point touchLast = {};

const char* screenName(Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return kAppName;
    case Screen::BubbleWrap:
      return "Bubble Wrap";
    case Screen::ZenRake:
      return "Zen Rake";
    case Screen::FlipDots:
      return "Flip-Dot Board";
    case Screen::RipplePond:
      return "Ripple Pond";
    case Screen::PointlessCounter:
      return "Pointless Counter";
    case Screen::Squish:
      return "Squish";
  }
  return kAppName;
}

const char* helpText(Screen screen) {
  switch (screen) {
    case Screen::BubbleWrap:
      return "Tap bubbles until the sheet is satisfyingly flat.";
    case Screen::ZenRake:
      return "Drag a finger through the sand to leave three calm grooves.";
    case Screen::FlipDots:
      return "Tap or drag across dots to flip them black or white.";
    case Screen::RipplePond:
      return "Tap the pond to send out ripples. They fade on their own.";
    case Screen::PointlessCounter:
      return "Tap the enormous button. The number goes up. That is all.";
    case Screen::Squish:
      return "Press and hold the blob to squish it. Release to let it recover.";
    case Screen::Menu:
      return "Pick anything. There are no goals, scores, timers, or wrong moves.";
  }
  return "";
}

size_t activityIndex(Screen screen) {
  const uint8_t value = static_cast<uint8_t>(screen);
  return value == 0 ? 0 : static_cast<size_t>(value - 1);
}

Screen activityScreen(size_t index) {
  return static_cast<Screen>(1 + index % kActivityCount);
}

void centerText(const String& text, int x, int y, int font = 4,
                uint16_t color = TFT_BLACK, uint16_t background = TFT_WHITE) {
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(color, background, true);
  epaper.drawString(text, x, y, font);
}

void leftText(const String& text, int x, int y, int font = 2,
              uint16_t color = TFT_BLACK, uint16_t background = TFT_WHITE) {
  epaper.setTextDatum(ML_DATUM);
  epaper.setTextColor(color, background, true);
  epaper.drawString(text, x, y, font);
}

void drawFiddleMark(int centerX, int centerY, uint16_t color = TFT_BLACK) {
  epaper.drawCircle(centerX - 28, centerY + 6, 15, color);
  epaper.drawCircle(centerX + 28, centerY - 6, 15, color);
  epaper.fillCircle(centerX - 28, centerY + 6, 5, color);
  epaper.fillCircle(centerX + 28, centerY - 6, 5, color);
  epaper.drawLine(centerX - 13, centerY + 3, centerX - 4, centerY - 8, color);
  epaper.drawLine(centerX - 4, centerY - 8, centerX + 4, centerY + 8, color);
  epaper.drawLine(centerX + 4, centerY + 8, centerX + 13, centerY - 3, color);
}

void drawBatteryStatus() {
  const int gaugeX = 427;
  const int gaugeY = 17;
  const int gaugeWidth = 34;
  const int gaugeHeight = 17;
  epaper.fillRect(kStatusRegion.x, kStatusRegion.y, kStatusRegion.width,
                  kStatusRegion.height, TFT_WHITE);
  const String percent =
      batteryPercent >= 0 ? String(batteryPercent) + "%" : "--%";
  epaper.setTextDatum(MR_DATUM);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.drawString(percent, gaugeX - 8, 25, 2);
  epaper.drawRect(gaugeX, gaugeY, gaugeWidth, gaugeHeight, TFT_BLACK);
  epaper.fillRect(gaugeX + gaugeWidth, gaugeY + 5, 4, 7, TFT_BLACK);
  if (batteryPercent >= 0) {
    const int fill = std::max(
        1, (gaugeWidth - 4) * std::min(100, batteryPercent) / 100);
    epaper.fillRect(gaugeX + 2, gaugeY + 2, fill, gaugeHeight - 4, TFT_BLACK);
  }
  if (externalPowerPresent) {
    epaper.drawLine(gaugeX + 13, gaugeY + 3, gaugeX + 9, gaugeY + 9,
                    TFT_WHITE);
    epaper.drawLine(gaugeX + 9, gaugeY + 9, gaugeX + 17, gaugeY + 9,
                    TFT_WHITE);
    epaper.drawLine(gaugeX + 17, gaugeY + 9, gaugeX + 13, gaugeY + 14,
                    TFT_WHITE);
  }
}

void drawStatusBar() {
  leftText(kBrandName, 12, 25, 2);
  drawBatteryStatus();
  epaper.drawFastHLine(0, kStatusHeight - 1, kScreenWidth, TFT_BLACK);
}

void drawBackButton() {
  epaper.fillRoundRect(kBackButton.x, kBackButton.y, kBackButton.width,
                       kBackButton.height, 8, TFT_BLACK);
  const int centerY = kBackButton.y + kBackButton.height / 2;
  epaper.fillTriangle(kBackButton.x + 11, centerY, kBackButton.x + 27,
                      centerY - 12, kBackButton.x + 27, centerY + 12,
                      TFT_WHITE);
  epaper.fillRect(kBackButton.x + 25, centerY - 3, 18, 7, TFT_WHITE);
}

void drawHelpButton() {
  epaper.drawCircle(kHelpButton.x + kHelpButton.width / 2,
                    kHelpButton.y + kHelpButton.height / 2, 18, TFT_BLACK);
  centerText("?", kHelpButton.x + kHelpButton.width / 2,
             kHelpButton.y + kHelpButton.height / 2, 4);
}

void drawActivityHeader() {
  drawBackButton();
  centerText(screenName(currentScreen), kScreenWidth / 2, 79, 4);
  drawHelpButton();
  epaper.drawFastHLine(0, kHeaderBottom - 1, kScreenWidth, TFT_BLACK);
}

void drawFooterButton(const char* label = "RESET") {
  epaper.fillRoundRect(kResetButton.x, kResetButton.y, kResetButton.width,
                       kResetButton.height, 14, TFT_BLACK);
  centerText(label, kResetButton.x + kResetButton.width / 2,
             kResetButton.y + kResetButton.height / 2, 4, TFT_WHITE,
             TFT_BLACK);
}

void drawActivityIcon(Screen screen, int centerX, int centerY) {
  switch (screen) {
    case Screen::BubbleWrap:
      for (int row = -1; row <= 1; ++row) {
        for (int column = -1; column <= 1; ++column) {
          epaper.drawCircle(centerX + column * 34, centerY + row * 34, 12,
                           TFT_BLACK);
        }
      }
      break;
    case Screen::ZenRake:
      for (int offset = -12; offset <= 12; offset += 12) {
        epaper.drawLine(centerX - 66, centerY + offset + 20, centerX - 20,
                        centerY + offset - 20, TFT_BLACK);
        epaper.drawLine(centerX - 20, centerY + offset - 20, centerX + 25,
                        centerY + offset + 20, TFT_BLACK);
        epaper.drawLine(centerX + 25, centerY + offset + 20, centerX + 66,
                        centerY + offset - 20, TFT_BLACK);
      }
      break;
    case Screen::FlipDots:
      for (int row = -2; row <= 2; ++row) {
        for (int column = -3; column <= 3; ++column) {
          const bool on = (row + column) % 3 == 0;
          if (on) {
            epaper.fillCircle(centerX + column * 23, centerY + row * 23, 8,
                              TFT_BLACK);
          } else {
            epaper.drawCircle(centerX + column * 23, centerY + row * 23, 8,
                              TFT_BLACK);
          }
        }
      }
      break;
    case Screen::RipplePond:
      for (int radius = 18; radius <= 66; radius += 16) {
        epaper.drawCircle(centerX, centerY, radius, TFT_BLACK);
      }
      break;
    case Screen::PointlessCounter:
      epaper.fillCircle(centerX, centerY, 64, TFT_BLACK);
      centerText("+1", centerX, centerY, 4, TFT_WHITE, TFT_BLACK);
      break;
    case Screen::Squish:
      epaper.fillEllipse(centerX, centerY, 74, 53, TFT_BLACK);
      epaper.fillCircle(centerX - 24, centerY - 8, 5, TFT_WHITE);
      epaper.fillCircle(centerX + 24, centerY - 8, 5, TFT_WHITE);
      epaper.drawFastHLine(centerX - 20, centerY + 19, 40, TFT_WHITE);
      break;
    case Screen::Menu:
      drawFiddleMark(centerX, centerY);
      break;
  }
}

void drawMenu() {
  epaper.fillScreen(TFT_WHITE);
  drawStatusBar();
  for (size_t index = 0; index < kActivityCount; ++index) {
    const Rect& card = kMenuCards[index];
    const Screen screen = activityScreen(index);
    epaper.drawRoundRect(card.x, card.y, card.width, card.height, 14, TFT_BLACK);
    drawActivityIcon(screen, card.x + card.width / 2, card.y + 82);
    centerText(screenName(screen), card.x + card.width / 2,
               card.y + card.height - 28, 2);
  }
}

void drawBubbleWrap() {
  constexpr int left = 42;
  constexpr int top = 128;
  constexpr int cell = 66;
  epaper.fillRect(0, kContentTop, kScreenWidth,
                  kContentBottom - kContentTop + 1, TFT_WHITE);
  for (int row = 0; row < BubbleWrap::kRows; ++row) {
    for (int column = 0; column < BubbleWrap::kColumns; ++column) {
      const int centerX = left + column * cell + cell / 2;
      const int centerY = top + row * cell + cell / 2;
      if (bubbles.popped(row, column)) {
        epaper.drawCircle(centerX, centerY, 25, TFT_BLACK);
        epaper.drawLine(centerX - 13, centerY - 13, centerX + 13,
                        centerY + 13, TFT_BLACK);
        epaper.drawLine(centerX + 13, centerY - 13, centerX - 13,
                        centerY + 13, TFT_BLACK);
      } else {
        epaper.fillCircle(centerX, centerY, 25, TFT_BLACK);
        epaper.fillCircle(centerX - 7, centerY - 8, 6, TFT_WHITE);
      }
    }
  }
  if (bubbles.allPopped()) {
    centerText("FLAT.", kScreenWidth / 2, 674, 2);
  }
}

void drawZenRake() {
  epaper.fillRect(0, kContentTop, kScreenWidth,
                  kContentBottom - kContentTop + 1, TFT_WHITE);
  epaper.drawRoundRect(18, 124, 444, 560, 14, TFT_BLACK);
  for (int y = 150; y < 670; y += 32) {
    epaper.drawFastHLine(30, y, 420, 0xDEFB);
  }
  for (size_t index = 0; index < rake.count(); ++index) {
    const RakeSegment& segment = rake.segment(index);
    for (int offset = -6; offset <= 6; offset += 6) {
      epaper.drawLine(segment.x1 + offset, segment.y1, segment.x2 + offset,
                      segment.y2, TFT_BLACK);
    }
  }
}

void drawFlipDots() {
  constexpr int left = 48;
  constexpr int top = 126;
  constexpr int cellWidth = 48;
  constexpr int cellHeight = 46;
  epaper.fillRect(0, kContentTop, kScreenWidth,
                  kContentBottom - kContentTop + 1, TFT_WHITE);
  for (int row = 0; row < FlipDots::kRows; ++row) {
    for (int column = 0; column < FlipDots::kColumns; ++column) {
      const int centerX = left + column * cellWidth + cellWidth / 2;
      const int centerY = top + row * cellHeight + cellHeight / 2;
      if (flipDots.on(row, column)) {
        epaper.fillCircle(centerX, centerY, 17, TFT_BLACK);
      } else {
        epaper.drawCircle(centerX, centerY, 17, TFT_BLACK);
      }
    }
  }
}

void drawRipplePond() {
  epaper.fillRect(0, kContentTop, kScreenWidth,
                  kContentBottom - kContentTop + 1, TFT_WHITE);
  epaper.drawRoundRect(18, 124, 444, 560, 36, TFT_BLACK);
  for (int y = 160; y < 660; y += 80) {
    epaper.drawFastHLine(45, y, 390, 0xDEFB);
  }
  for (size_t index = 0; index < pond.count(); ++index) {
    const Ripple& ripple = pond.ripple(index);
    const int baseRadius = 16 + ripple.age * 10;
    const int rings = RipplePond::kMaximumAge - ripple.age;
    for (int ring = 0; ring < rings; ++ring) {
      epaper.drawCircle(ripple.x, ripple.y, baseRadius + ring * 18, TFT_BLACK);
    }
  }
  if (pond.count() == 0) {
    centerText("tap the water", kScreenWidth / 2, 405, 2);
  }
}

void drawPointlessCounter() {
  epaper.fillRect(0, kContentTop, kScreenWidth,
                  kContentBottom - kContentTop + 1, TFT_WHITE);
  epaper.fillCircle(kScreenWidth / 2, 400, 178, TFT_BLACK);
  epaper.drawCircle(kScreenWidth / 2, 400, 190, TFT_BLACK);
  epaper.setTextSize(counter.value() < 100000 ? 2 : 1);
  centerText(String(counter.value()), kScreenWidth / 2, 390, 4, TFT_WHITE,
             TFT_BLACK);
  epaper.setTextSize(1);
  centerText("TAP", kScreenWidth / 2, 480, 4, TFT_WHITE, TFT_BLACK);
}

void drawSquish() {
  epaper.fillRect(0, kContentTop, kScreenWidth,
                  kContentBottom - kContentTop + 1, TFT_WHITE);
  const int level = squish.level();
  const int radiusX = 105 + level * 18;
  const int radiusY = 120 - level * 10;
  const int centerY = 405 + level * 8;
  epaper.fillEllipse(kScreenWidth / 2, centerY, radiusX, radiusY, TFT_BLACK);
  const int eyeSpread = 35 + level * 5;
  epaper.fillCircle(kScreenWidth / 2 - eyeSpread, centerY - 28, 8, TFT_WHITE);
  epaper.fillCircle(kScreenWidth / 2 + eyeSpread, centerY - 28, 8, TFT_WHITE);
  epaper.drawFastHLine(kScreenWidth / 2 - 28 - level * 3, centerY + 32,
                      56 + level * 6, TFT_WHITE);
  centerText(touchActive ? "SQUISH" : "press and hold", kScreenWidth / 2, 650,
             2);
}

void drawActivityContent() {
  switch (currentScreen) {
    case Screen::BubbleWrap:
      drawBubbleWrap();
      break;
    case Screen::ZenRake:
      drawZenRake();
      break;
    case Screen::FlipDots:
      drawFlipDots();
      break;
    case Screen::RipplePond:
      drawRipplePond();
      break;
    case Screen::PointlessCounter:
      drawPointlessCounter();
      break;
    case Screen::Squish:
      drawSquish();
      break;
    case Screen::Menu:
      return;
  }
}

void drawHelpOverlay() {
  constexpr Rect panel = {24, 146, 432, 500};
  epaper.fillRoundRect(panel.x, panel.y, panel.width, panel.height, 20,
                       TFT_WHITE);
  epaper.drawRoundRect(panel.x, panel.y, panel.width, panel.height, 20,
                       TFT_BLACK);
  epaper.drawRoundRect(panel.x + 2, panel.y + 2, panel.width - 4,
                       panel.height - 4, 18, TFT_BLACK);
  centerText(screenName(currentScreen), kScreenWidth / 2, panel.y + 74, 4);

  String text = helpText(currentScreen);
  const int maximumCharacters = 34;
  int y = panel.y + 160;
  while (text.length() > 0 && y < panel.y + panel.height - 80) {
    int split = std::min(maximumCharacters, static_cast<int>(text.length()));
    if (split < static_cast<int>(text.length())) {
      const int space = text.lastIndexOf(' ', split);
      if (space > 0) split = space;
    }
    centerText(text.substring(0, split), kScreenWidth / 2, y, 2);
    text.remove(0, split);
    text.trim();
    y += 32;
  }

  const int closeX = panel.x + panel.width - 45;
  const int closeY = panel.y + 43;
  epaper.fillCircle(closeX, closeY, 24, TFT_BLACK);
  epaper.drawLine(closeX - 9, closeY - 9, closeX + 9, closeY + 9, TFT_WHITE);
  epaper.drawLine(closeX + 9, closeY - 9, closeX - 9, closeY + 9, TFT_WHITE);
  centerText("OK = BACK  |  HOLD OK = SLEEP", kScreenWidth / 2,
             panel.y + panel.height - 45, 2);
}

void drawCurrentScreen() {
  if (currentScreen == Screen::Menu) {
    drawMenu();
  } else {
    epaper.fillScreen(TFT_WHITE);
    drawStatusBar();
    drawActivityHeader();
    drawActivityContent();
    drawFooterButton(currentScreen == Screen::ZenRake ? "CLEAR" : "RESET");
  }
  if (helpVisible) drawHelpOverlay();
}

void drawStatus(const char* title, const char* subtitle) {
  epaper.fillScreen(TFT_WHITE);
  drawFiddleMark(kScreenWidth / 2, 270);
  centerText(title, kScreenWidth / 2, 390, 4);
  centerText(subtitle, kScreenWidth / 2, 445, 2);
}

void drawSleepSplash() {
  epaper.fillScreen(TFT_WHITE);
  drawFiddleMark(kScreenWidth / 2, 270);
  centerText(kBrandName, kScreenWidth / 2, 390, 4);
  centerText("Press OK to fiddle again", kScreenWidth / 2, 450, 2);
}

void drawChargeSplash(int percent) {
  epaper.fillScreen(TFT_WHITE);
  drawFiddleMark(kScreenWidth / 2, 250);
  centerText("BATTERY LOW", kScreenWidth / 2, 365, 4);
  if (percent >= 0) {
    centerText(String(percent) + "%", kScreenWidth / 2, 435, 4);
  }
  centerText(kBrandName, kScreenWidth / 2, 535, 2);
}

bool refreshRegion(const E1005FastRefresh::Region& region,
                   const char* action) {
  if (!fastRefresh.ready()) return false;
  E1005FastRefresh::Timing timing;
  const E1005FastRefresh::Result result = fastRefresh.refresh(region, timing);
  if (result == E1005FastRefresh::Result::Ok) return true;
  LOG.printf("[fiddle] %s refresh failed: %s\n", action,
             E1005FastRefresh::resultMessage(result));
  epaper.sleep();
  epaper.update();
  return fastRefresh.begin() == E1005FastRefresh::Result::Ok;
}

bool refreshScreen(const char* action) {
  epaper.sleep();
  epaper.update();
  const E1005FastRefresh::Result result = fastRefresh.begin();
  if (result == E1005FastRefresh::Result::Ok) return true;
  LOG.printf("[fiddle] %s full refresh failed: %s\n", action,
             E1005FastRefresh::resultMessage(result));
  touchReady = false;
  return false;
}

uint32_t stateChecksum(const PersistedState& state) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
  uint32_t checksum = 2166136261UL;
  for (size_t index = 0; index < offsetof(PersistedState, checksum); ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619UL;
  }
  return checksum;
}

void saveResumeState() {
  PersistedState state = {};
  state.magic = kPersistedMagic;
  state.version = kPersistedVersion;
  state.screen = static_cast<uint8_t>(currentScreen);
  state.bubbles = bubbles.snapshot();
  state.rakeCount = static_cast<uint16_t>(rake.count());
  for (size_t index = 0; index < rake.count(); ++index) {
    state.rakeSegments[index] = rake.segment(index);
  }
  memcpy(state.flipDots, flipDots.snapshot(), sizeof(state.flipDots));
  state.rippleCount = static_cast<uint8_t>(pond.count());
  for (size_t index = 0; index < pond.count(); ++index) {
    state.ripples[index] = pond.ripple(index);
  }
  state.counter = counter.value();
  state.squishLevel = squish.level();
  state.checksum = stateChecksum(state);
  persistedState = state;
}

bool restoreResumeState() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;
  const PersistedState state = persistedState;
  if (state.magic != kPersistedMagic ||
      state.version != kPersistedVersion ||
      state.screen > static_cast<uint8_t>(Screen::Squish) ||
      state.rakeCount > ZenRake::kMaximumSegments ||
      state.rippleCount > RipplePond::kMaximumRipples ||
      state.checksum != stateChecksum(state)) {
    LOG.println("[fiddle] saved state is invalid");
    return false;
  }
  for (size_t index = 0; index < state.rippleCount; ++index) {
    if (state.ripples[index].age >= RipplePond::kMaximumAge) return false;
  }

  currentScreen = static_cast<Screen>(state.screen);
  bubbles.restore(state.bubbles);
  rake.restore(state.rakeSegments, state.rakeCount);
  flipDots.restore(state.flipDots);
  pond.restore(state.ripples, state.rippleCount);
  counter.restore(state.counter);
  squish.restore(state.squishLevel);
  return true;
}

void configureButtons() {
  for (ButtonState& button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableLevel = digitalRead(button.pin);
    button.sampledLevel = button.stableLevel;
    button.changedAtMs = millis();
    button.pressedAtMs =
        button.stableLevel == LOW ? button.changedAtMs : 0;
  }
}

bool pollButtonEvent(ButtonEvent& event) {
  const uint32_t now = millis();
  for (ButtonState& button : buttons) {
    const int level = digitalRead(button.pin);
    if (level != button.sampledLevel) {
      button.sampledLevel = level;
      button.changedAtMs = now;
    }
    if (level == button.stableLevel ||
        now - button.changedAtMs < kButtonDebounceMs) {
      continue;
    }
    button.stableLevel = level;
    if (level == LOW) {
      button.pressedAtMs = now;
    } else {
      event = {&button, now - button.pressedAtMs};
      return true;
    }
  }
  return false;
}

void recordActivity() { lastActivityAtMs = millis(); }

bool inputHandlingActive() {
  if (touchActive ||
      (currentScreen == Screen::Squish && squishRelaxing)) {
    return true;
  }
  for (const ButtonState& button : buttons) {
    if (button.sampledLevel != button.stableLevel ||
        button.stableLevel == LOW) {
      return true;
    }
  }
  return false;
}

void disableLightSleepWake() {
  for (const ButtonState& button : buttons) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(button.pin));
  }
  gpio_wakeup_disable(static_cast<gpio_num_t>(board::PIN_TOUCH_INTERRUPT));
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#if USB_SCREEN_CAPTURE_ENABLED
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);
#endif
  lightSleepReady = false;
}

bool configureLightSleepWake() {
  bool configured = true;
  for (const ButtonState& button : buttons) {
    configured =
        gpio_wakeup_enable(static_cast<gpio_num_t>(button.pin),
                           GPIO_INTR_LOW_LEVEL) == ESP_OK &&
        configured;
  }
  if (touchReady) {
    configured =
        gpio_wakeup_enable(
            static_cast<gpio_num_t>(board::PIN_TOUCH_INTERRUPT),
            GPIO_INTR_LOW_LEVEL) == ESP_OK &&
        configured;
  }
#if USB_SCREEN_CAPTURE_ENABLED
  if (uart_set_wakeup_threshold(UART_NUM_1, 3) == ESP_OK) {
    esp_sleep_enable_uart_wakeup(UART_NUM_1);
  }
#endif
  configured = esp_sleep_enable_gpio_wakeup() == ESP_OK && configured;
  if (!configured) disableLightSleepWake();
  return configured;
}

void powerDownAndSleep(bool lowBattery = false, int percent = -1) {
  disableLightSleepWake();
  while (digitalRead(board::PIN_BUTTON_0) == LOW) delay(10);
  const bool wakePinReady = hardware::configureWakePin(board::PIN_BUTTON_0);
  const esp_err_t wakeResult =
      wakePinReady
          ? esp_sleep_enable_ext1_wakeup(1ULL << board::PIN_BUTTON_0,
                                         ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  if (wakeResult != ESP_OK) {
    LOG.println("[fiddle] refusing sleep without OK-button wake");
    LOG.flush();
    ESP.restart();
  }

  saveResumeState();
  if (lowBattery) {
    drawChargeSplash(percent);
  } else {
    drawSleepSplash();
  }
  epaper.update();
  touch.end();
  fastRefresh.end();
  LOG.flush();
  delay(50);

  SD.end();
  epaper.getSPIinstance().end();
  pinMode(board::PIN_SD_CS, INPUT);
  pinMode(board::PIN_SD_SCK, INPUT);
  pinMode(board::PIN_SD_MOSI, INPUT);
  pinMode(board::PIN_SD_MISO, INPUT);
  peripheral_power::disableSd();
  peripheral_power::disable();
  power_latch::holdDuringDeepSleep();
  while (digitalRead(board::PIN_BUTTON_0) == LOW) delay(10);
  esp_deep_sleep_start();
}

void resetCurrentActivity() {
  switch (currentScreen) {
    case Screen::BubbleWrap:
      bubbles.reset();
      break;
    case Screen::ZenRake:
      rake.reset();
      break;
    case Screen::FlipDots:
      flipDots.reset();
      break;
    case Screen::RipplePond:
      pond.reset();
      break;
    case Screen::PointlessCounter:
      counter.reset();
      break;
    case Screen::Squish:
      squish.reset();
      squishRelaxing = false;
      break;
    case Screen::Menu:
      return;
  }
  drawActivityContent();
  refreshRegion(kContentRegion, "reset activity");
}

void showMenu() {
  helpVisible = false;
  squishRelaxing = false;
  currentScreen = Screen::Menu;
  drawMenu();
  refreshScreen("show menu");
}

void showActivity(Screen screen) {
  helpVisible = false;
  if (screen != Screen::Squish) squishRelaxing = false;
  currentScreen = screen;
  if (screen == Screen::RipplePond && pond.count() > 0) {
    nextRippleAgeAtMs = millis() + kRippleAgeIntervalMs;
  }
  drawCurrentScreen();
  refreshScreen("show activity");
}

void toggleHelp() {
  helpVisible = !helpVisible;
  if (currentScreen == Screen::Squish) {
    squishRelaxing = !helpVisible && squish.level() > 0;
    if (squishRelaxing) nextSquishStepAtMs = millis() + kSquishStepMs;
  }
  drawCurrentScreen();
  refreshScreen(helpVisible ? "show help" : "close help");
}

bool bubbleCellForPoint(int x, int y, int& row, int& column) {
  constexpr int left = 42;
  constexpr int top = 128;
  constexpr int cell = 66;
  column = (x - left) / cell;
  row = (y - top) / cell;
  return x >= left && y >= top && column >= 0 &&
         column < BubbleWrap::kColumns && row >= 0 && row < BubbleWrap::kRows;
}

bool flipCellForPoint(int x, int y, int& row, int& column) {
  constexpr int left = 48;
  constexpr int top = 126;
  constexpr int cellWidth = 48;
  constexpr int cellHeight = 46;
  column = (x - left) / cellWidth;
  row = (y - top) / cellHeight;
  return x >= left && y >= top && column >= 0 && column < FlipDots::kColumns &&
         row >= 0 && row < FlipDots::kRows;
}

void handleTouchPoint(const Gt911Touch::Point& point, bool first) {
  recordActivity();
  if (first) {
    if (helpVisible) {
      toggleHelp();
      return;
    }
    if (currentScreen == Screen::Menu) {
      for (size_t index = 0; index < kActivityCount; ++index) {
        if (kMenuCards[index].contains(point.x, point.y)) {
          hardware::beep();
          showActivity(activityScreen(index));
          return;
        }
      }
      return;
    }
    if (kBackButton.contains(point.x, point.y)) {
      hardware::beep();
      showMenu();
      return;
    }
    if (kHelpButton.contains(point.x, point.y)) {
      hardware::beep();
      toggleHelp();
      return;
    }
    if (kResetButton.contains(point.x, point.y)) {
      hardware::beep();
      resetCurrentActivity();
      return;
    }
  }

  if (!kActivityArea.contains(point.x, point.y)) return;
  switch (currentScreen) {
    case Screen::BubbleWrap: {
      if (!first) return;
      int row = 0;
      int column = 0;
      if (bubbleCellForPoint(point.x, point.y, row, column) &&
          bubbles.pop(row, column)) {
        hardware::beep();
        drawBubbleWrap();
        touchDirty = true;
      }
      break;
    }
    case Screen::ZenRake:
      if (!first) {
        const int dx = point.x - touchLast.x;
        const int dy = point.y - touchLast.y;
        if (dx * dx + dy * dy >= 64 &&
            rake.add(touchLast.x, touchLast.y, point.x, point.y)) {
          drawZenRake();
          touchDirty = true;
        }
      }
      break;
    case Screen::FlipDots: {
      int row = 0;
      int column = 0;
      if (!flipCellForPoint(point.x, point.y, row, column)) return;
      const int cell = row * FlipDots::kColumns + column;
      if (cell != lastFlipCell && flipDots.toggle(row, column)) {
        lastFlipCell = cell;
        drawFlipDots();
        touchDirty = true;
      }
      break;
    }
    case Screen::RipplePond:
      if (first && point.x >= 28 && point.x <= 452 && point.y >= 134 &&
          point.y <= 674) {
        pond.add(point.x, point.y);
        nextRippleAgeAtMs = millis() + kRippleAgeIntervalMs;
        drawRipplePond();
        touchDirty = true;
      }
      break;
    case Screen::PointlessCounter:
      if (first && counter.increment()) {
        hardware::beep();
        drawPointlessCounter();
        touchDirty = true;
      }
      break;
    case Screen::Squish:
      if (first) {
        squishRelaxing = false;
        nextSquishStepAtMs = millis();
        drawSquish();
        touchDirty = true;
      }
      break;
    case Screen::Menu:
      break;
  }
}

void pollTouch() {
  if (!touchReady) return;
  Gt911Touch::Point point = {};
  const Gt911Touch::PollResult result = touch.poll(point);
  if (result == Gt911Touch::PollResult::None) return;
  if (result == Gt911Touch::PollResult::Release) {
    if (touchActive) {
      touchActive = false;
      lastFlipCell = -1;
      if (currentScreen == Screen::Squish && !helpVisible &&
          kActivityArea.contains(touchStart.x, touchStart.y)) {
        squishRelaxing = true;
        nextSquishStepAtMs = millis() + kSquishStepMs;
      } else if (touchDirty) {
        refreshRegion(kContentRegion, "touch activity");
      }
      touchDirty = false;
    }
    return;
  }

  const bool first = !touchActive;
  if (first) {
    touchActive = true;
    touchStart = point;
    touchLast = point;
    lastFlipCell = -1;
  }
  handleTouchPoint(point, first);
  touchLast = point;
}

void updateAnimations() {
  if (helpVisible) return;
  const uint32_t now = millis();
  if (currentScreen == Screen::RipplePond && pond.count() > 0 &&
      static_cast<int32_t>(now - nextRippleAgeAtMs) >= 0) {
    pond.advance();
    nextRippleAgeAtMs = now + kRippleAgeIntervalMs;
    drawRipplePond();
    refreshRegion(kContentRegion, "age ripples");
  }

  if (currentScreen != Screen::Squish) return;
  if (touchActive && static_cast<int32_t>(now - nextSquishStepAtMs) >= 0) {
    nextSquishStepAtMs = now + kSquishStepMs;
    if (squish.inflate()) {
      drawSquish();
      refreshRegion(kContentRegion, "squish");
    }
  } else if (squishRelaxing &&
             static_cast<int32_t>(now - nextSquishStepAtMs) >= 0) {
    nextSquishStepAtMs = now + kSquishStepMs;
    if (squish.relax()) {
      drawSquish();
      refreshRegion(kContentRegion, "relax squish");
    } else {
      squishRelaxing = false;
    }
  }
}

void handleButton(const ButtonEvent& event) {
  recordActivity();
  if (event.button->pin == board::PIN_BUTTON_0) {
    if (event.heldMs >= kDeepSleepHoldMs) {
      hardware::beep();
      powerDownAndSleep();
    } else if (helpVisible) {
      hardware::beep();
      toggleHelp();
    } else if (currentScreen != Screen::Menu) {
      hardware::beep();
      showMenu();
    }
    return;
  }

  if (helpVisible || currentScreen == Screen::Menu) return;
  const size_t index = activityIndex(currentScreen);
  if (event.button->pin == board::PIN_BUTTON_1) {
    hardware::beep();
    showActivity(activityScreen((index + kActivityCount - 1) % kActivityCount));
  } else if (event.button->pin == board::PIN_BUTTON_2) {
    hardware::beep();
    showActivity(activityScreen((index + 1) % kActivityCount));
  }
}

void checkBatteryAndSleepIfNeeded() {
  const uint32_t now = millis();
  if (nextBatteryCheckAtMs != 0 &&
      static_cast<int32_t>(now - nextBatteryCheckAtMs) < 0) {
    return;
  }
  nextBatteryCheckAtMs = now + kBatteryCheckIntervalMs;

  const battery::FuelGaugeReading gauge = battery::readBq27220();
  pinMode(board::PIN_EXTERNAL_POWER, INPUT);
  const bool externalPower =
      digitalRead(board::PIN_EXTERNAL_POWER) == HIGH;
  const int updatedPercent = gauge.valid ? gauge.percent : -1;
  const bool changed = !batterySampled || batteryPercent != updatedPercent ||
                       externalPowerPresent != externalPower;
  batterySampled = true;
  batteryPercent = updatedPercent;
  externalPowerPresent = externalPower;

  if (gauge.valid &&
      low_battery::shouldWarn(true, gauge.valid, externalPower, gauge.percent)) {
    powerDownAndSleep(true, gauge.percent);
  }
  if (changed && fastRefresh.ready()) {
    drawBatteryStatus();
    refreshRegion(kStatusRegion, "battery status");
  }
}

void sleepAfterInactivityIfNeeded() {
  if (!inputHandlingActive() &&
      static_cast<uint32_t>(millis() - lastActivityAtMs) >=
          kInactivitySleepMs) {
    powerDownAndSleep();
  }
}

void idleInLightSleep() {
  if (!lightSleepReady || inputHandlingActive()) {
    delay(5);
    return;
  }

  const uint32_t now = millis();
  const int32_t batteryRemaining =
      static_cast<int32_t>(nextBatteryCheckAtMs - now);
  const int32_t inactivityRemaining = static_cast<int32_t>(
      kInactivitySleepMs - static_cast<uint32_t>(now - lastActivityAtMs));
  int32_t animationRemaining = INT32_MAX;
  if (currentScreen == Screen::RipplePond && pond.count() > 0) {
    animationRemaining = static_cast<int32_t>(nextRippleAgeAtMs - now);
  }
  const int32_t remaining =
      std::min(batteryRemaining, std::min(inactivityRemaining,
                                         animationRemaining));
  if (remaining <= 0) return;
  if (esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(remaining) * 1000ULL) !=
      ESP_OK) {
    disableLightSleepWake();
    return;
  }
  LOG.flush();
  const esp_err_t result = esp_light_sleep_start();
#if USB_SCREEN_CAPTURE_ENABLED
  if (result == ESP_OK &&
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UART) {
    usbScreenCapture.serveFor(epaper, kScreenWidth, kScreenHeight, 300);
  }
#endif
  if (result != ESP_OK && result != ESP_ERR_SLEEP_REJECT &&
      result != ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION) {
    disableLightSleepWake();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

}  // namespace

void setup() {
  power_latch::holdOn();
  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  usbScreenCapture.begin(Serial1);
  delay(50);
  LOG.printf("\n[fiddle] %s firmware %s\n", kAppName,
             board::FIRMWARE_VERSION);
  hardware::beep();

  const bool resumed = restoreResumeState();
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);

  epaper_setup::begin(epaper);
  checkBatteryAndSleepIfNeeded();
  sdCardReady = sd_card::mount(epaper.getSPIinstance(), "/sticky-fiddle");
  if (sdCardReady && sd_ota::hasUpdate()) {
    drawStatus("UPDATING FIRMWARE", "Do not power off");
    epaper.update();
    if (sd_ota::apply() == sd_ota::Result::Applied) {
      delay(1000);
      ESP.restart();
    }
    drawStatus("UPDATE FAILED", "Current firmware is safe");
    epaper.update();
    delay(2500);
  }
  SD.end();
  sdCardReady = false;
  peripheral_power::disableSd();

  if (!resumed) currentScreen = Screen::Menu;
  drawCurrentScreen();
  epaper.update();
  sd_ota::confirmRunningImage();

  const E1005FastRefresh::Result refreshResult = fastRefresh.begin();
  if (refreshResult != E1005FastRefresh::Result::Ok) {
    LOG.printf("[fiddle] fast refresh unavailable: %s\n",
               E1005FastRefresh::resultMessage(refreshResult));
  }
  configureButtons();
  touchReady =
      refreshResult == E1005FastRefresh::Result::Ok && touch.begin(touchWire);
  if (!touchReady) LOG.println("[fiddle] touch initialization failed");
  recordActivity();
  lightSleepReady = configureLightSleepWake();
}

void loop() {
  usbScreenCapture.poll(epaper, kScreenWidth, kScreenHeight);
  checkBatteryAndSleepIfNeeded();
  pollTouch();
  ButtonEvent event;
  if (pollButtonEvent(event)) handleButton(event);
  updateAnimations();
  sleepAfterInactivityIfNeeded();
  idleInLightSleep();
}
