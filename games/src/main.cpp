#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>

#include "app_logger.h"
#include "battery_gauge.h"
#include "board_pins.h"
#include "driver.h"
#include "e1005_fast_refresh.h"
#include "epaper_setup.h"
#include "gt911_touch.h"
#include "hardware.h"
#include "lights_out_game.h"
#include "low_battery.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "sd_card.h"
#include "sd_ota.h"

#if RETERMINAL_MODEL != 1005
#error "The Games app supports only reTerminal E1005"
#endif

TimestampedLogger appLog(Serial1);
EPaper epaper;

namespace {

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 800;
constexpr int kGridLeft = 40;
constexpr int kGridTop = 150;
constexpr int kCellSize = 80;
constexpr int kGridSize = kCellSize * LightsOutGame::kSize;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kButtonLongPressMs = 1200;
constexpr uint32_t kBatteryCheckIntervalMs = 60000;
constexpr uint32_t kInactivitySleepMs = 5UL * 60UL * 1000UL;
constexpr int kLowBatteryThresholdPct = 10;
constexpr uint32_t kPersistedStateMagic = 0x47414D45;
constexpr uint16_t kPersistedStateVersion = 2;
constexpr uint8_t kLightsOutSavedFlag = 1U << 0;

struct Rect {
  int x;
  int y;
  int width;
  int height;

  bool contains(int pointX, int pointY) const {
    return pointX >= x && pointX < x + width &&
           pointY >= y && pointY < y + height;
  }
};

constexpr Rect kMenuGameCard = {40, 165, 190, 190};
constexpr Rect kBackButton = {24, 24, 104, 54};
constexpr Rect kNewButton = {30, 688, 190, 66};
constexpr Rect kResetButton = {260, 688, 190, 66};
constexpr E1005FastRefresh::Region kFullScreen = {0, 0, kScreenWidth,
                                                   kScreenHeight};
constexpr E1005FastRefresh::Region kBoardRegion = {30, 130, 420, 550};

enum class Screen {
  Menu,
  LightsOut,
};

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint8_t screen;
  uint8_t flags;
  LightsOutGame::Snapshot lightsOut;
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
  bool longPressReported;
};

ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "OK / power", HIGH, HIGH, 0, 0, false},
    {board::PIN_BUTTON_1, "UP", HIGH, HIGH, 0, 0, false},
    {board::PIN_BUTTON_2, "DOWN", HIGH, HIGH, 0, 0, false},
};

enum class ButtonPressType {
  Short,
  Long,
};

struct ButtonEvent {
  ButtonState* button;
  ButtonPressType type;
};

enum class SleepScreen {
  Resume,
  Charge,
};

TwoWire touchWire(1);
Gt911Touch touch;
E1005FastRefresh fastRefresh(epaper);
LightsOutGame lightsOut;
Screen currentScreen = Screen::Menu;
bool touchReady = false;
bool touchActive = false;
bool lightSleepReady = false;
bool lightsOutSaved = false;
uint32_t nextBatteryCheckAtMs = 0;
uint32_t lastActivityAtMs = 0;

void configureButtons() {
  for (ButtonState& button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableLevel = digitalRead(button.pin);
    button.sampledLevel = button.stableLevel;
    button.changedAtMs = millis();
    button.pressedAtMs =
        button.stableLevel == LOW ? button.changedAtMs : 0;
    button.longPressReported = false;
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
      if (level == LOW && button.stableLevel == LOW &&
         !button.longPressReported &&
         now - button.pressedAtMs >= kButtonLongPressMs) {
        button.longPressReported = true;
        event = {&button, ButtonPressType::Long};
        return true;
      }
      continue;
    }
    button.stableLevel = level;
    if (level == LOW) {
      button.pressedAtMs = now;
      button.longPressReported = false;
    } else if (!button.longPressReported) {
      event = {&button, ButtonPressType::Short};
      return true;
    }
  }
  return false;
}

bool inputHandlingActive() {
  if (touchActive) return true;
  for (const ButtonState& button : buttons) {
    if (button.sampledLevel != button.stableLevel ||
        button.stableLevel == LOW) {
      return true;
    }
  }
  return false;
}

void recordActivity() { lastActivityAtMs = millis(); }

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
  state.magic = kPersistedStateMagic;
  state.version = kPersistedStateVersion;
  state.screen = static_cast<uint8_t>(currentScreen);
  if (lightsOutSaved) {
    state.flags |= kLightsOutSavedFlag;
    state.lightsOut = lightsOut.snapshot();
  }
  state.checksum = stateChecksum(state);
  persistedState = state;
}

bool restoreResumeState() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return false;

  const PersistedState state = persistedState;
  if (state.magic != kPersistedStateMagic ||
      state.version != kPersistedStateVersion ||
      state.checksum != stateChecksum(state) ||
      state.screen > static_cast<uint8_t>(Screen::LightsOut) ||
      (state.flags & ~kLightsOutSavedFlag) != 0) {
    LOG.println("[games] saved resume state is invalid");
    return false;
  }

  const Screen savedScreen = static_cast<Screen>(state.screen);
  const bool hasLightsOutSave = (state.flags & kLightsOutSavedFlag) != 0;
  if (hasLightsOutSave && !lightsOut.restore(state.lightsOut)) {
    LOG.println("[games] saved Lights Out state is invalid");
    return false;
  }
  if (savedScreen == Screen::LightsOut && !hasLightsOutSave) {
    LOG.println("[games] saved screen has no Lights Out game");
    return false;
  }
  lightsOutSaved = hasLightsOutSave;
  currentScreen = savedScreen;
  return true;
}

void disableLightSleepWake() {
  for (const ButtonState& button : buttons) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(button.pin));
  }
  if (board::PIN_TOUCH_INTERRUPT >= 0) {
    gpio_wakeup_disable(
        static_cast<gpio_num_t>(board::PIN_TOUCH_INTERRUPT));
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
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
  configured = esp_sleep_enable_gpio_wakeup() == ESP_OK && configured;
  if (!configured) {
    disableLightSleepWake();
    LOG.println("[games] light-sleep GPIO wake unavailable");
  }
  return configured;
}

void idleInLightSleep() {
  if (!lightSleepReady) {
    delay(5);
    return;
  }
  if (inputHandlingActive()) {
    delay(5);
    return;
  }

  const uint32_t now = millis();
  const int32_t batteryRemainingMs =
      static_cast<int32_t>(nextBatteryCheckAtMs - now);
  const int32_t inactivityRemainingMs = static_cast<int32_t>(
      kInactivitySleepMs - static_cast<uint32_t>(now - lastActivityAtMs));
  if (batteryRemainingMs <= 0 || inactivityRemainingMs <= 0) return;
  const uint32_t sleepMs = std::min(
      static_cast<uint32_t>(batteryRemainingMs),
      static_cast<uint32_t>(inactivityRemainingMs));
  const uint64_t timerUs = static_cast<uint64_t>(sleepMs) * 1000ULL;
  if (esp_sleep_enable_timer_wakeup(timerUs) != ESP_OK) {
    LOG.println("[games] light-sleep timer wake unavailable");
    disableLightSleepWake();
    return;
  }

  LOG.println("[games] entering light sleep");
  LOG.flush();
  const esp_err_t sleepResult = esp_light_sleep_start();
  if (sleepResult == ESP_OK) {
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    LOG.printf("[games] exited light sleep (wake=%s)\n",
               cause == ESP_SLEEP_WAKEUP_GPIO
                   ? "gpio"
                   : cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : "other");
  } else if (sleepResult == ESP_ERR_SLEEP_REJECT ||
             sleepResult == ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION) {
    LOG.printf("[games] light sleep deferred: %s\n",
               esp_err_to_name(sleepResult));
    delay(10);
  } else {
    LOG.printf("[games] light sleep failed: %s\n",
               esp_err_to_name(sleepResult));
    disableLightSleepWake();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

void drawCentered(const String& text, int x, int y, int font) {
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.drawString(text, x, y, font);
}

void drawGamesLogo(int centerX, int centerY, int width) {
  const int height = width * 5 / 8;
  const int left = centerX - width / 2;
  const int top = centerY - height / 2;
  const int radius = height / 4;
  epaper.fillRoundRect(left, top, width, height, radius, TFT_BLACK);

  const int controlSize = height / 3;
  const int controlX = left + width / 4;
  const int controlY = centerY;
  const int controlThickness = std::max(4, controlSize / 3);
  epaper.fillRect(controlX - controlSize / 2,
                  controlY - controlThickness / 2, controlSize,
                  controlThickness, TFT_WHITE);
  epaper.fillRect(controlX - controlThickness / 2,
                  controlY - controlSize / 2, controlThickness,
                  controlSize, TFT_WHITE);

  const int buttonRadius = std::max(4, height / 11);
  epaper.fillCircle(left + width * 3 / 4 - buttonRadius,
                    centerY + buttonRadius, buttonRadius, TFT_WHITE);
  epaper.fillCircle(left + width * 3 / 4 + buttonRadius,
                    centerY - buttonRadius, buttonRadius, TFT_WHITE);
}

bool roundedRectContains(const Rect& rect, int radius, int x, int y) {
  const int left = rect.x;
  const int right = rect.x + rect.width - 1;
  const int top = rect.y;
  const int bottom = rect.y + rect.height - 1;
  if (x >= left + radius && x <= right - radius) return true;
  if (y >= top + radius && y <= bottom - radius) return true;

  const int centerX = x < left + radius ? left + radius : right - radius;
  const int centerY = y < top + radius ? top + radius : bottom - radius;
  const int dx = x - centerX;
  const int dy = y - centerY;
  return dx * dx + dy * dy <= radius * radius;
}

void fillDarkGrayRoundRect(const Rect& rect, int radius) {
  epaper.fillRoundRect(rect.x, rect.y, rect.width, rect.height, radius,
                      TFT_BLACK);
  for (int y = rect.y; y < rect.y + rect.height; y += 2) {
    for (int x = rect.x; x < rect.x + rect.width; x += 2) {
      if (roundedRectContains(rect, radius, x, y)) {
        epaper.drawPixel(x, y, TFT_WHITE);
      }
    }
  }
}

void drawButton(const Rect& rect, const char* label) {
  fillDarkGrayRoundRect(rect, 8);
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(TFT_WHITE);
  epaper.drawString(label, rect.x + rect.width / 2,
                    rect.y + rect.height / 2, 4);
}

void drawLightsOutCell(int x, int y, bool on) {
  const int inset = 4;
  const int size = kCellSize - inset * 2;
  x += inset;
  y += inset;
  if (on) {
    epaper.fillRoundRect(x, y, size, size, 8, TFT_BLACK);
    epaper.drawCircle(x + size / 2, y + size / 2, 12, TFT_WHITE);
    epaper.fillCircle(x + size / 2, y + size / 2, 5, TFT_WHITE);
  } else {
    epaper.fillRoundRect(x, y, size, size, 8, TFT_WHITE);
    epaper.drawRoundRect(x, y, size, size, 8, TFT_BLACK);
    epaper.drawRoundRect(x + 1, y + 1, size - 2, size - 2, 7, TFT_BLACK);
  }
}

void drawMenuGameCard() {
  epaper.fillRoundRect(kMenuGameCard.x, kMenuGameCard.y,
                      kMenuGameCard.width, kMenuGameCard.height, 14,
                      TFT_WHITE);
  epaper.drawRoundRect(kMenuGameCard.x, kMenuGameCard.y,
                      kMenuGameCard.width, kMenuGameCard.height, 14,
                      TFT_BLACK);

  constexpr bool lights[2][2] = {
      {true, false},
      {false, true},
  };
  const int gridLeft = kMenuGameCard.x + 15;
  const int gridTop = kMenuGameCard.y + 15;
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      drawLightsOutCell(gridLeft + column * kCellSize,
                        gridTop + row * kCellSize, lights[row][column]);
    }
  }

  const Rect title = {
      kMenuGameCard.x + 8,
      kMenuGameCard.y + kMenuGameCard.height / 2 - 27,
      kMenuGameCard.width - 16,
      54,
  };
  fillDarkGrayRoundRect(title, 8);
  epaper.setTextColor(TFT_WHITE);
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString("LIGHTS OUT", kMenuGameCard.x + kMenuGameCard.width / 2,
                    kMenuGameCard.y + kMenuGameCard.height / 2, 4);
}

void drawMenu() {
  epaper.fillSprite(TFT_WHITE);
  drawGamesLogo(kScreenWidth / 2, 60, 110);
  drawMenuGameCard();
}

void drawStatus(const char* title, const char* detail) {
  epaper.fillSprite(TFT_WHITE);
  drawCentered(title, kScreenWidth / 2, 330, 4);
  drawCentered(detail, kScreenWidth / 2, 390, 4);
}

void drawSleepSplash() {
  epaper.fillSprite(TFT_WHITE);
  drawGamesLogo(kScreenWidth / 2, 390, 280);
}

void drawChargeSplash(int batteryPercent) {
  epaper.fillSprite(TFT_WHITE);
  drawCentered("BATTERY LOW", kScreenWidth / 2, 130, 4);
  drawCentered(String(batteryPercent) + "% remaining", kScreenWidth / 2, 180,
               4);
  drawGamesLogo(kScreenWidth / 2, 390, 280);
  drawCentered("PLEASE CHARGE", kScreenWidth / 2, 545, 4);
  drawCentered("Connect USB-C", kScreenWidth / 2, 595, 4);
  drawCentered("Press OK after charging", kScreenWidth / 2, 645, 4);
}

void drawLightsOutBoard() {
  epaper.fillRect(kBoardRegion.x, kBoardRegion.y, kBoardRegion.width,
                  kBoardRegion.height, TFT_WHITE);

  for (int row = 0; row < LightsOutGame::kSize; ++row) {
    for (int column = 0; column < LightsOutGame::kSize; ++column) {
      drawLightsOutCell(kGridLeft + column * kCellSize,
                        kGridTop + row * kCellSize,
                        lightsOut.isOn(row, column));
    }
  }

  if (lightsOut.solved()) {
    drawCentered("SOLVED!", kScreenWidth / 2, 590, 4);
    drawCentered(String(lightsOut.moves()) + " moves - tap NEW",
                 kScreenWidth / 2, 630, 4);
  } else {
    drawCentered(String(lightsOut.moves()) + " moves", kScreenWidth / 2,
                 590, 4);
  }
}

void drawLightsOut() {
  epaper.fillSprite(TFT_WHITE);
  drawButton(kBackButton, "GAMES");
  drawCentered("LIGHTS OUT", 292, 51, 4);
  drawLightsOutBoard();
  drawButton(kNewButton, "NEW");
  drawButton(kResetButton, "RESET");
}

uint32_t randomScramble() {
  uint32_t mask = esp_random() & LightsOutGame::kCellMask;
  int pressed = 0;
  for (int index = 0; index < LightsOutGame::kCellCount; ++index) {
    if ((mask & (1UL << index)) != 0) ++pressed;
  }
  if (pressed < 7) mask ^= 0x0155AA15UL & LightsOutGame::kCellMask;
  return mask;
}

void startNewPuzzle() {
  lightsOut.startPuzzle(randomScramble());
  lightsOutSaved = true;
}

bool recoverFullRefresh() {
  epaper.sleep();
  epaper.update();
  const E1005FastRefresh::Result result = fastRefresh.begin();
  if (result == E1005FastRefresh::Result::Ok) return true;
  LOG.printf("[games] cannot restore fast refresh: %s\n",
             E1005FastRefresh::resultMessage(result));
  touchReady = false;
  return false;
}

bool refreshRegion(const E1005FastRefresh::Region& region,
                   const char* action) {
  E1005FastRefresh::Timing timing;
  const E1005FastRefresh::Result result = fastRefresh.refresh(region, timing);
  if (result != E1005FastRefresh::Result::Ok) {
    LOG.printf("[games] %s refresh failed: %s\n", action,
               E1005FastRefresh::resultMessage(result));
    return recoverFullRefresh();
  }
  LOG.printf(
      "[games] %s refresh=%lu us "
      "(prepare=%lu transfer=%lu panel=%lu reseed=%lu)\n",
      action, static_cast<unsigned long>(timing.totalUs),
      static_cast<unsigned long>(timing.prepareUs),
      static_cast<unsigned long>(timing.transferUs),
      static_cast<unsigned long>(timing.panelUs),
      static_cast<unsigned long>(timing.reseedUs));
  return true;
}

void showMenu() {
  if (currentScreen == Screen::LightsOut && lightsOutSaved) {
    LOG.println("[games] auto-saving Lights Out");
  }
  currentScreen = Screen::Menu;
  saveResumeState();
  drawMenu();
  refreshRegion(kFullScreen, "menu");
}

void showLightsOut() {
  if (!lightsOutSaved) startNewPuzzle();
  currentScreen = Screen::LightsOut;
  drawLightsOut();
  refreshRegion(kFullScreen, "lights-out screen");
}

void updateLightsOut(const char* action) {
  drawLightsOutBoard();
  refreshRegion(kBoardRegion, action);
}

void handleMenuTouch(const Gt911Touch::Point& point) {
  if (kMenuGameCard.contains(point.x, point.y)) {
    showLightsOut();
  }
}

void handleLightsOutTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    startNewPuzzle();
    updateLightsOut("new puzzle");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    lightsOut.reset();
    updateLightsOut("reset puzzle");
    return;
  }
  if (point.x < kGridLeft || point.x >= kGridLeft + kGridSize ||
      point.y < kGridTop || point.y >= kGridTop + kGridSize) {
    return;
  }

  const int column = (point.x - kGridLeft) / kCellSize;
  const int row = (point.y - kGridTop) / kCellSize;
  if (lightsOut.press(row, column)) {
    updateLightsOut("move");
  }
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
  recordActivity();
  if (currentScreen == Screen::Menu) {
    handleMenuTouch(point);
  } else {
    handleLightsOutTouch(point);
  }
}

void powerDownAndSleep(SleepScreen screen = SleepScreen::Resume,
                       int batteryPercent = -1) {
  disableLightSleepWake();
  while (digitalRead(board::PIN_BUTTON_0) == LOW) delay(10);
  const bool wakePinReady = hardware::configureWakePin(board::PIN_BUTTON_0);
  const uint64_t wakeMask = 1ULL << board::PIN_BUTTON_0;
  const esp_err_t wakeResult =
      wakePinReady
          ? esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  LOG.printf("[games] wake config: %s\n", esp_err_to_name(wakeResult));
  if (wakeResult != ESP_OK) {
    LOG.println("[games] refusing deep sleep without an OK-button wake source");
    LOG.flush();
    delay(250);
    ESP.restart();
  }

  saveResumeState();
  if (screen == SleepScreen::Charge) {
    drawChargeSplash(batteryPercent);
  } else {
    drawSleepSplash();
  }
  hardware::beep();
  LOG.printf("[games] entering deep sleep (%s)\n",
             screen == SleepScreen::Charge ? "low battery" : "requested");
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

void handleButton(const ButtonEvent& event) {
  ButtonState& button = *event.button;
  recordActivity();
  LOG.printf("[games] %s %s press\n", button.name,
             event.type == ButtonPressType::Long ? "long" : "short");

  if (button.pin != board::PIN_BUTTON_0) {
    LOG.printf("[games] ignoring %s button\n", button.name);
    return;
  }
  if (event.type == ButtonPressType::Long) {
    while (digitalRead(button.pin) == LOW) delay(10);
    powerDownAndSleep();
  } else if (currentScreen == Screen::LightsOut) {
    showMenu();
  } else {
    LOG.println("[games] ignoring OK on game selection");
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
  if (!gauge.valid) {
    LOG.println("[battery] BQ27220 battery gauge unavailable");
    return;
  }

  LOG.printf("[battery] %d%% (%.3fV), external_power=%s\n", gauge.percent,
             gauge.voltage, externalPower ? "yes" : "no");
  if (low_battery::shouldWarn(true, gauge.valid, externalPower, gauge.percent,
                              kLowBatteryThresholdPct)) {
    LOG.printf("[battery] below %d%%; requesting recharge\n",
               kLowBatteryThresholdPct);
    powerDownAndSleep(SleepScreen::Charge, gauge.percent);
  }
}

void sleepAfterInactivityIfNeeded() {
  if (inputHandlingActive() ||
      static_cast<uint32_t>(millis() - lastActivityAtMs) <
          kInactivitySleepMs) {
    return;
  }
  LOG.println("[games] five minutes inactive; entering deep sleep");
  powerDownAndSleep();
}

}  // namespace

void setup() {
  power_latch::holdOn();
  LOG.begin(115200, SERIAL_8N1, board::PIN_LOG_RX, board::PIN_LOG_TX);
  delay(50);
  LOG.println();
  LOG.println("[games] reTerminal E1005 Games");
  hardware::beep();
  const bool resumed = restoreResumeState();
  LOG.printf("[games] boot mode: %s\n", resumed ? "resume" : "cold");

  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);

  epaper_setup::begin(epaper);
  checkBatteryAndSleepIfNeeded();
  const bool sdReady = sd_card::mount(epaper.getSPIinstance(), "/games");
  if (sdReady && sd_ota::hasUpdate()) {
    drawStatus("UPDATING FIRMWARE", "DO NOT POWER OFF");
    epaper.update();
    const sd_ota::Result updateResult = sd_ota::apply();
    if (updateResult == sd_ota::Result::Applied) {
      delay(1000);
      ESP.restart();
    }
    drawStatus("UPDATE FAILED", "CURRENT FIRMWARE IS SAFE");
    epaper.update();
    delay(2500);
  }

  if (resumed && currentScreen == Screen::LightsOut) {
    drawLightsOut();
  } else {
    currentScreen = Screen::Menu;
    drawMenu();
  }
  LOG.printf("[games] refreshing %s\n",
             currentScreen == Screen::LightsOut ? "saved game" : "menu");
  epaper.update();
  sd_ota::confirmRunningImage();

  const E1005FastRefresh::Result refreshResult = fastRefresh.begin();
  if (refreshResult != E1005FastRefresh::Result::Ok) {
    LOG.printf("[games] fast refresh unavailable: %s\n",
               E1005FastRefresh::resultMessage(refreshResult));
  }

  configureButtons();
  touchReady =
      refreshResult == E1005FastRefresh::Result::Ok && touch.begin(touchWire);
  if (touchReady) {
    LOG.printf("[touch] GT%s ready at 0x%02X, sensor=%ux%u\n",
               touch.productId(), touch.address(), touch.sensorWidth(),
               touch.sensorHeight());
  } else {
    LOG.println("[touch] GT911 initialization failed");
  }
  recordActivity();
  lightSleepReady = configureLightSleepWake();
  LOG.printf("[games] idle light sleep: %s\n",
             lightSleepReady ? "enabled" : "unavailable");
}

void loop() {
  checkBatteryAndSleepIfNeeded();
  pollTouch();
  ButtonEvent event = {};
  if (pollButtonEvent(event)) {
    handleButton(event);
  }
  sleepAfterInactivityIfNeeded();
  idleInLightSleep();
}
