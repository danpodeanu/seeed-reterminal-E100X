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
#include "game_2048.h"
#include "gt911_touch.h"
#include "hardware.h"
#include "lights_out_game.h"
#include "low_battery.h"
#include "mini_minesweeper_game.h"
#include "nonogram_game.h"
#include "peripheral_power.h"
#include "pipe_connect_game.h"
#include "power_latch.h"
#include "repo_qr.h"
#include "reversi_game.h"
#include "sd_card.h"
#include "sd_ota.h"
#ifdef ENABLE_SCREENSHOT_GESTURE
#include "screenshot_bmp.h"
#endif
#include "text_render.h"

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
constexpr int k2048GridLeft = 40;
constexpr int k2048GridTop = 150;
constexpr int k2048CellSize = 100;
constexpr int k2048GridSize = k2048CellSize * Game2048::kSize;
constexpr int kPipeGridLeft = 30;
constexpr int kPipeGridTop = 130;
constexpr int kPipeCellSize = 70;
constexpr int kPipeGridSize = kPipeCellSize * PipeConnectGame::kSize;
constexpr int kMinesGridLeft = 30;
constexpr int kMinesGridTop = 130;
constexpr int kMinesCellSize = 70;
constexpr int kMinesGridSize =
    kMinesCellSize * MiniMinesweeperGame::kSize;
constexpr int kNonogramGridLeft = 125;
constexpr int kNonogramGridTop = 190;
constexpr int kNonogramCellSize = 64;
constexpr int kNonogramGridSize = kNonogramCellSize * NonogramGame::kSize;
constexpr int kReversiGridLeft = 32;
constexpr int kReversiGridTop = 150;
constexpr int kReversiCellSize = 52;
constexpr int kReversiGridSize = kReversiCellSize * ReversiGame::kSize;
constexpr int kStatusDividerY = 48;
constexpr int kSwipeThreshold = 45;
constexpr int kMinesTouchMoveTolerance = 20;
constexpr int kReversiAiDepth = 3;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kButtonLongPressMs = 1200;
#ifdef ENABLE_SCREENSHOT_GESTURE
constexpr uint32_t kScreenshotHoldMs = 5000;
#endif
constexpr uint32_t kMinesFlagHoldMs = 650;
constexpr uint32_t kBatteryCheckIntervalMs = 60000;
constexpr uint32_t kInactivitySleepMs = 5UL * 60UL * 1000UL;
constexpr int kLowBatteryThresholdPct = 10;
constexpr uint32_t kPersistedStateMagic = 0x47414D45;
constexpr uint16_t kPersistedStateVersion = 8;
constexpr uint8_t kLightsOutSavedFlag = 1U << 0;
constexpr uint8_t k2048SavedFlag = 1U << 1;
constexpr uint8_t kPipeConnectSavedFlag = 1U << 2;
constexpr uint8_t kMinesweeperSavedFlag = 1U << 3;
constexpr uint8_t kNonogramSavedFlag = 1U << 4;
constexpr uint8_t kReversiSavedFlag = 1U << 5;

constexpr uint32_t makeNonogramSolution(uint8_t row0, uint8_t row1,
                                        uint8_t row2, uint8_t row3,
                                        uint8_t row4) {
  return static_cast<uint32_t>(row0) |
         (static_cast<uint32_t>(row1) << 5) |
         (static_cast<uint32_t>(row2) << 10) |
         (static_cast<uint32_t>(row3) << 15) |
         (static_cast<uint32_t>(row4) << 20);
}

constexpr uint32_t kNonogramPuzzles[] = {
    makeNonogramSolution(0b00100, 0b01110, 0b11111, 0b01110, 0b00100),
    makeNonogramSolution(0b01010, 0b11111, 0b11111, 0b01110, 0b00100),
    makeNonogramSolution(0b00100, 0b00100, 0b11111, 0b00100, 0b00100),
    makeNonogramSolution(0b11111, 0b10001, 0b10001, 0b10001, 0b11111),
    makeNonogramSolution(0b00100, 0b01100, 0b11111, 0b01100, 0b00100),
    makeNonogramSolution(0b00100, 0b01110, 0b11111, 0b00100, 0b01110),
    makeNonogramSolution(0b00100, 0b01110, 0b11111, 0b10101, 0b11111),
};
constexpr size_t kNonogramPuzzleCount =
    sizeof(kNonogramPuzzles) / sizeof(kNonogramPuzzles[0]);

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

constexpr Rect kLightsOutMenuCard = {40, 112, 190, 190};
constexpr Rect k2048MenuCard = {250, 112, 190, 190};
constexpr Rect kPipeConnectMenuCard = {40, 320, 190, 190};
constexpr Rect kMinesweeperMenuCard = {250, 320, 190, 190};
constexpr Rect kNonogramMenuCard = {40, 528, 190, 190};
constexpr Rect kReversiMenuCard = {250, 528, 190, 190};
constexpr Rect kBackButton = {8, 6, 48, 36};
constexpr Rect kNewButton = {30, 688, 190, 66};
constexpr Rect kResetButton = {260, 688, 190, 66};
constexpr Rect kCenteredNewButton = {145, 688, 190, 66};
constexpr E1005FastRefresh::Region kBatteryStatusRegion = {390, 0, 90, 48};
constexpr E1005FastRefresh::Region kBoardRegion = {30, 130, 420, 550};
constexpr E1005FastRefresh::Region k2048BoardRegion = {30, 112, 420, 553};
constexpr E1005FastRefresh::Region kPipeBoardRegion = {25, 112, 430, 553};
constexpr E1005FastRefresh::Region kMinesBoardRegion = {25, 112, 430, 553};
constexpr E1005FastRefresh::Region kNonogramBoardRegion = {20, 64, 440, 584};
constexpr E1005FastRefresh::Region kReversiBoardRegion = {25, 104, 430, 560};
constexpr E1005FastRefresh::Region kReversiModeRegion = {25, 104, 430, 650};

enum class Screen {
  Menu,
  LightsOut,
  Game2048,
  PipeConnect,
  Minesweeper,
  Nonogram,
  Reversi,
};

enum class ReversiMode : uint8_t {
  SinglePlayer,
  TwoPlayer,
};

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint8_t screen;
  uint8_t flags;
  uint8_t reversiMode;
  LightsOutGame::Snapshot lightsOut;
  Game2048::Snapshot game2048;
  PipeConnectGame::Snapshot pipeConnect;
  MiniMinesweeperGame::Snapshot minesweeper;
  NonogramGame::Snapshot nonogram;
  ReversiGame::Snapshot reversi;
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
#ifdef ENABLE_SCREENSHOT_GESTURE
  Screenshot,
#endif
};

const char* buttonPressTypeName(ButtonPressType type) {
  switch (type) {
    case ButtonPressType::Short:
      return "short";
    case ButtonPressType::Long:
      return "long";
#ifdef ENABLE_SCREENSHOT_GESTURE
    case ButtonPressType::Screenshot:
      return "screenshot";
#endif
  }
  return "unknown";
}

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
Game2048 game2048;
PipeConnectGame pipeConnect;
MiniMinesweeperGame minesweeper;
NonogramGame nonogram;
ReversiGame reversi;
ReversiMode reversiMode = ReversiMode::SinglePlayer;
Screen currentScreen = Screen::Menu;
bool touchReady = false;
bool touchActive = false;
bool touchActionHandled = false;
bool lightSleepReady = false;
#ifdef ENABLE_SCREENSHOT_GESTURE
bool sdReady = false;
#endif
bool lightsOutSaved = false;
bool game2048Saved = false;
bool pipeConnectSaved = false;
bool minesweeperSaved = false;
bool nonogramSaved = false;
bool reversiSaved = false;
bool batteryStatusSampled = false;
bool externalPowerPresent = false;
int batteryPercent = -1;
uint32_t nextBatteryCheckAtMs = 0;
uint32_t lastActivityAtMs = 0;
uint32_t touchStartedAtMs = 0;
Gt911Touch::Point touchStart = {};
Gt911Touch::Point touchLast = {};

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
#ifdef ENABLE_SCREENSHOT_GESTURE
      const uint32_t holdThreshold =
          button.pin == board::PIN_BUTTON_0 ? kScreenshotHoldMs
                                            : kButtonLongPressMs;
      if (level == LOW && button.stableLevel == LOW &&
          !button.longPressReported &&
          now - button.pressedAtMs >= holdThreshold) {
        button.longPressReported = true;
        event = {&button, button.pin == board::PIN_BUTTON_0
                              ? ButtonPressType::Screenshot
                              : ButtonPressType::Long};
        return true;
      }
#else
      if (level == LOW && button.stableLevel == LOW &&
         !button.longPressReported &&
         now - button.pressedAtMs >= kButtonLongPressMs) {
        button.longPressReported = true;
        event = {&button, ButtonPressType::Long};
        return true;
      }
#endif
      continue;
    }
    button.stableLevel = level;
    if (level == LOW) {
      button.pressedAtMs = now;
      button.longPressReported = false;
    } else if (!button.longPressReported) {
#ifdef ENABLE_SCREENSHOT_GESTURE
      const bool longPress =
          button.pin == board::PIN_BUTTON_0 &&
          now - button.pressedAtMs >= kButtonLongPressMs;
      event = {&button, longPress ? ButtonPressType::Long
                                  : ButtonPressType::Short};
#else
      event = {&button, ButtonPressType::Short};
#endif
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
  state.reversiMode = static_cast<uint8_t>(reversiMode);
  if (lightsOutSaved) {
    state.flags |= kLightsOutSavedFlag;
    state.lightsOut = lightsOut.snapshot();
  }
  if (game2048Saved) {
    state.flags |= k2048SavedFlag;
    state.game2048 = game2048.snapshot();
  }
  if (pipeConnectSaved) {
    state.flags |= kPipeConnectSavedFlag;
    state.pipeConnect = pipeConnect.snapshot();
  }
  if (minesweeperSaved) {
    state.flags |= kMinesweeperSavedFlag;
    state.minesweeper = minesweeper.snapshot();
  }
  if (nonogramSaved) {
    state.flags |= kNonogramSavedFlag;
    state.nonogram = nonogram.snapshot();
  }
  if (reversiSaved) {
    state.flags |= kReversiSavedFlag;
    state.reversi = reversi.snapshot();
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
      state.screen > static_cast<uint8_t>(Screen::Reversi) ||
      state.reversiMode > static_cast<uint8_t>(ReversiMode::TwoPlayer) ||
      (state.flags & ~(kLightsOutSavedFlag | k2048SavedFlag |
                       kPipeConnectSavedFlag | kMinesweeperSavedFlag |
                       kNonogramSavedFlag | kReversiSavedFlag)) != 0) {
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
  const bool has2048Save = (state.flags & k2048SavedFlag) != 0;
  if (has2048Save && !game2048.restore(state.game2048)) {
    LOG.println("[games] saved 2048 state is invalid");
    return false;
  }
  if (savedScreen == Screen::Game2048 && !has2048Save) {
    LOG.println("[games] saved screen has no 2048 game");
    return false;
  }
  const bool hasPipeConnectSave =
      (state.flags & kPipeConnectSavedFlag) != 0;
  if (hasPipeConnectSave && !pipeConnect.restore(state.pipeConnect)) {
    LOG.println("[games] saved Pipe Connect state is invalid");
    return false;
  }
  if (savedScreen == Screen::PipeConnect && !hasPipeConnectSave) {
    LOG.println("[games] saved screen has no Pipe Connect game");
    return false;
  }
  const bool hasMinesweeperSave =
      (state.flags & kMinesweeperSavedFlag) != 0;
  if (hasMinesweeperSave && !minesweeper.restore(state.minesweeper)) {
    LOG.println("[games] saved Minesweeper state is invalid");
    return false;
  }
  if (savedScreen == Screen::Minesweeper && !hasMinesweeperSave) {
    LOG.println("[games] saved screen has no Minesweeper game");
    return false;
  }
  const bool hasNonogramSave = (state.flags & kNonogramSavedFlag) != 0;
  if (hasNonogramSave && !nonogram.restore(state.nonogram)) {
    LOG.println("[games] saved Nonogram state is invalid");
    return false;
  }
  if (savedScreen == Screen::Nonogram && !hasNonogramSave) {
    LOG.println("[games] saved screen has no Nonogram game");
    return false;
  }
  const bool hasReversiSave = (state.flags & kReversiSavedFlag) != 0;
  if (hasReversiSave && !reversi.restore(state.reversi)) {
    LOG.println("[games] saved Reversi state is invalid");
    return false;
  }
  if (savedScreen == Screen::Reversi && !hasReversiSave) {
    LOG.println("[games] saved screen has no Reversi game");
    return false;
  }
  lightsOutSaved = hasLightsOutSave;
  game2048Saved = has2048Save;
  pipeConnectSaved = hasPipeConnectSave;
  minesweeperSaved = hasMinesweeperSave;
  nonogramSaved = hasNonogramSave;
  reversiSaved = hasReversiSave;
  reversiMode = static_cast<ReversiMode>(state.reversiMode);
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

void drawCenteredNumber(uint32_t value, int x, int y, int font,
                        uint16_t foreground, uint16_t background) {
  const int opticalYOffset = font == 6 ? 6 : font == 4 ? 3 : 0;
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(foreground, background, true);
  epaper.drawNumber(static_cast<long>(value), x, y + opticalYOffset, font);
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

void drawButton(const Rect& rect, const char* label) {
  epaper.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 8, TFT_BLACK);
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(TFT_WHITE, TFT_BLACK, true);
  epaper.drawString(label, rect.x + rect.width / 2,
                    rect.y + rect.height / 2 + 3, 4);
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

void drawLightsOutMenuCard() {
  epaper.fillRoundRect(kLightsOutMenuCard.x, kLightsOutMenuCard.y,
                      kLightsOutMenuCard.width, kLightsOutMenuCard.height, 14,
                      TFT_WHITE);
  epaper.drawRoundRect(kLightsOutMenuCard.x, kLightsOutMenuCard.y,
                      kLightsOutMenuCard.width, kLightsOutMenuCard.height, 14,
                      TFT_BLACK);

  constexpr bool lights[2][2] = {
      {true, false},
      {false, true},
  };
  const int gridLeft = kLightsOutMenuCard.x + 15;
  const int gridTop = kLightsOutMenuCard.y + 15;
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      drawLightsOutCell(gridLeft + column * kCellSize,
                        gridTop + row * kCellSize, lights[row][column]);
    }
  }

}

void draw2048Tile(int x, int y, int slotSize, uint32_t value) {
  const int inset = 4;
  const int size = slotSize - inset * 2;
  const Rect tile = {x + inset, y + inset, size, size};
  if (value == 0) {
    epaper.fillRoundRect(tile.x, tile.y, tile.width, tile.height, 8, TFT_WHITE);
    epaper.drawRoundRect(tile.x, tile.y, tile.width, tile.height, 8, TFT_BLACK);
    return;
  }

  const bool solid =
      value >= 128 || value == 4 || value == 16 || value == 64;
  if (solid) {
    epaper.fillRoundRect(tile.x, tile.y, tile.width, tile.height, 8, TFT_BLACK);
  } else {
    epaper.fillRoundRect(tile.x, tile.y, tile.width, tile.height, 8, TFT_WHITE);
    epaper.drawRoundRect(tile.x, tile.y, tile.width, tile.height, 8, TFT_BLACK);
  }
  const int font = value < 100 ? 6 : value < 10000 ? 4 : 2;
  const int centerX = x + slotSize / 2;
  const int centerY = y + slotSize / 2;
  drawCenteredNumber(value, centerX, centerY, font,
                     solid ? TFT_WHITE : TFT_BLACK,
                     solid ? TFT_BLACK : TFT_WHITE);
}

void draw2048MenuCard() {
  epaper.fillRoundRect(k2048MenuCard.x, k2048MenuCard.y, k2048MenuCard.width,
                      k2048MenuCard.height, 14, TFT_WHITE);
  epaper.drawRoundRect(k2048MenuCard.x, k2048MenuCard.y, k2048MenuCard.width,
                      k2048MenuCard.height, 14, TFT_BLACK);

  constexpr uint32_t tiles[2][2] = {
      {2, 4},
      {8, 16},
  };
  const int gridLeft = k2048MenuCard.x + 15;
  const int gridTop = k2048MenuCard.y + 15;
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      draw2048Tile(gridLeft + column * kCellSize,
                   gridTop + row * kCellSize, kCellSize,
                   tiles[row][column]);
    }
  }

}

void drawPipeTile(int x, int y, int size, uint8_t edges) {
  epaper.fillRect(x, y, size, size, TFT_WHITE);
  epaper.drawRect(x, y, size, size, TFT_BLACK);

  const int centerX = x + size / 2;
  const int centerY = y + size / 2;
  const int outerThickness = std::max(12, size / 4);
  const int innerThickness = std::max(5, outerThickness / 2);
  const int outerHalf = outerThickness / 2;
  const int innerHalf = innerThickness / 2;
  const int collarDepth = std::max(5, size / 9);
  const int collarExtra = std::max(2, size / 18);

  // The black casing and wider edge collars give each route the silhouette of
  // joined pipework; the white core is drawn afterward as its highlight.
  if ((edges & PipeConnectGame::North) != 0) {
    epaper.fillRect(centerX - outerHalf, y, outerThickness,
                   centerY - y + 1, TFT_BLACK);
    epaper.fillRect(centerX - outerHalf - collarExtra, y,
                    outerThickness + collarExtra * 2, collarDepth, TFT_BLACK);
  }
  if ((edges & PipeConnectGame::East) != 0) {
    epaper.fillRect(centerX, centerY - outerHalf,
                   x + size - centerX, outerThickness, TFT_BLACK);
    epaper.fillRect(x + size - collarDepth,
                    centerY - outerHalf - collarExtra, collarDepth,
                    outerThickness + collarExtra * 2, TFT_BLACK);
  }
  if ((edges & PipeConnectGame::South) != 0) {
    epaper.fillRect(centerX - outerHalf, centerY, outerThickness,
                   y + size - centerY, TFT_BLACK);
    epaper.fillRect(centerX - outerHalf - collarExtra,
                    y + size - collarDepth,
                    outerThickness + collarExtra * 2, collarDepth, TFT_BLACK);
  }
  if ((edges & PipeConnectGame::West) != 0) {
    epaper.fillRect(x, centerY - outerHalf, centerX - x + 1,
                   outerThickness, TFT_BLACK);
    epaper.fillRect(x, centerY - outerHalf - collarExtra, collarDepth,
                    outerThickness + collarExtra * 2, TFT_BLACK);
  }
  epaper.fillCircle(centerX, centerY, outerHalf + 2, TFT_BLACK);

  if ((edges & PipeConnectGame::North) != 0) {
    epaper.fillRect(centerX - innerHalf, y, innerThickness,
                   centerY - y + 1, TFT_WHITE);
  }
  if ((edges & PipeConnectGame::East) != 0) {
    epaper.fillRect(centerX, centerY - innerHalf,
                   x + size - centerX, innerThickness, TFT_WHITE);
  }
  if ((edges & PipeConnectGame::South) != 0) {
    epaper.fillRect(centerX - innerHalf, centerY, innerThickness,
                   y + size - centerY, TFT_WHITE);
  }
  if ((edges & PipeConnectGame::West) != 0) {
    epaper.fillRect(x, centerY - innerHalf, centerX - x + 1,
                   innerThickness, TFT_WHITE);
  }
  epaper.fillCircle(centerX, centerY, innerHalf + 1, TFT_WHITE);
}

void drawPipeConnectMenuCard() {
  epaper.fillRoundRect(kPipeConnectMenuCard.x, kPipeConnectMenuCard.y,
                       kPipeConnectMenuCard.width,
                       kPipeConnectMenuCard.height, 14, TFT_WHITE);
  epaper.drawRoundRect(kPipeConnectMenuCard.x, kPipeConnectMenuCard.y,
                       kPipeConnectMenuCard.width,
                       kPipeConnectMenuCard.height, 14, TFT_BLACK);

  constexpr uint8_t pipes[3][3] = {
      {PipeConnectGame::East,
       PipeConnectGame::East | PipeConnectGame::West,
       PipeConnectGame::South | PipeConnectGame::West},
      {PipeConnectGame::East | PipeConnectGame::South,
       PipeConnectGame::East | PipeConnectGame::West,
       PipeConnectGame::North | PipeConnectGame::West},
      {PipeConnectGame::North | PipeConnectGame::East,
       PipeConnectGame::East | PipeConnectGame::West,
       PipeConnectGame::West},
  };
  constexpr int kPreviewCellSize = 50;
  const int gridLeft = kPipeConnectMenuCard.x + 20;
  const int gridTop = kPipeConnectMenuCard.y + 20;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      drawPipeTile(gridLeft + column * kPreviewCellSize,
                   gridTop + row * kPreviewCellSize, kPreviewCellSize,
                   pipes[row][column]);
    }
  }
}

void drawMineSymbol(int centerX, int centerY, uint16_t color) {
  constexpr int kRadius = 11;
  constexpr int kRayLength = 17;
  constexpr int kRayThickness = 3;
  epaper.fillRect(centerX - kRayLength, centerY - kRayThickness / 2,
                  kRayLength * 2 + 1, kRayThickness, color);
  epaper.fillRect(centerX - kRayThickness / 2, centerY - kRayLength,
                  kRayThickness, kRayLength * 2 + 1, color);
  epaper.drawLine(centerX - 13, centerY - 13, centerX + 13, centerY + 13,
                  color);
  epaper.drawLine(centerX - 13, centerY + 13, centerX + 13, centerY - 13,
                  color);
  epaper.fillCircle(centerX, centerY, kRadius, color);
  epaper.fillCircle(centerX + 4, centerY - 4, 2,
                    color == TFT_BLACK ? TFT_WHITE : TFT_BLACK);
}

void drawFlagSymbol(int centerX, int centerY, uint16_t color) {
  const int poleX = centerX - 6;
  epaper.fillRect(poleX, centerY - 17, 4, 31, color);
  epaper.fillTriangle(poleX + 4, centerY - 16, poleX + 4, centerY - 2,
                      centerX + 14, centerY - 9, color);
  epaper.fillRect(centerX - 13, centerY + 12, 23, 4, color);
}

void drawMinesweeperMenuCard() {
  epaper.fillRoundRect(kMinesweeperMenuCard.x, kMinesweeperMenuCard.y,
                       kMinesweeperMenuCard.width,
                       kMinesweeperMenuCard.height, 14, TFT_WHITE);
  epaper.drawRoundRect(kMinesweeperMenuCard.x, kMinesweeperMenuCard.y,
                       kMinesweeperMenuCard.width,
                       kMinesweeperMenuCard.height, 14, TFT_BLACK);

  constexpr int kPreviewCellSize = 50;
  const int gridLeft = kMinesweeperMenuCard.x + 20;
  const int gridTop = kMinesweeperMenuCard.y + 20;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      const int x = gridLeft + column * kPreviewCellSize;
      const int y = gridTop + row * kPreviewCellSize;
      epaper.fillRect(x, y, kPreviewCellSize, kPreviewCellSize, TFT_WHITE);
      epaper.drawRect(x, y, kPreviewCellSize, kPreviewCellSize, TFT_BLACK);
    }
  }
  drawMineSymbol(gridLeft + kPreviewCellSize * 3 / 2,
                 gridTop + kPreviewCellSize * 3 / 2, TFT_BLACK);
  drawCenteredNumber(1, gridLeft + kPreviewCellSize / 2,
                     gridTop + kPreviewCellSize / 2, 4, TFT_BLACK, TFT_WHITE);
  drawCenteredNumber(2, gridLeft + kPreviewCellSize * 5 / 2,
                     gridTop + kPreviewCellSize / 2, 4, TFT_BLACK, TFT_WHITE);
  epaper.fillRect(gridLeft, gridTop + kPreviewCellSize * 2,
                  kPreviewCellSize, kPreviewCellSize, TFT_BLACK);
  epaper.drawRect(gridLeft + 4, gridTop + kPreviewCellSize * 2 + 4,
                  kPreviewCellSize - 8, kPreviewCellSize - 8, TFT_WHITE);
}

void drawNonogramMarkCell(int x, int y, int size,
                          NonogramGame::CellState state) {
  epaper.fillRect(x, y, size, size, TFT_WHITE);
  epaper.drawRect(x, y, size, size, TFT_BLACK);
  if (state == NonogramGame::CellState::Filled) {
    constexpr int kInset = 4;
    epaper.fillRect(x + kInset, y + kInset, size - kInset * 2,
                   size - kInset * 2, TFT_BLACK);
  } else if (state == NonogramGame::CellState::Crossed) {
    const int inset = std::max(7, size / 5);
    for (int offset = -1; offset <= 1; ++offset) {
      epaper.drawLine(x + inset, y + inset + offset, x + size - inset - 1,
                      y + size - inset - 1 + offset, TFT_BLACK);
      epaper.drawLine(x + inset, y + size - inset - 1 + offset,
                      x + size - inset - 1, y + inset + offset, TFT_BLACK);
    }
  }
}

void drawNonogramMenuCard() {
  epaper.fillRoundRect(kNonogramMenuCard.x, kNonogramMenuCard.y,
                       kNonogramMenuCard.width, kNonogramMenuCard.height, 14,
                       TFT_WHITE);
  epaper.drawRoundRect(kNonogramMenuCard.x, kNonogramMenuCard.y,
                       kNonogramMenuCard.width, kNonogramMenuCard.height, 14,
                       TFT_BLACK);

  constexpr int kPreviewCellSize = 30;
  const int gridLeft = kNonogramMenuCard.x + 20;
  const int gridTop = kNonogramMenuCard.y + 20;
  const uint32_t solution = kNonogramPuzzles[0];
  for (int row = 0; row < NonogramGame::kSize; ++row) {
    for (int column = 0; column < NonogramGame::kSize; ++column) {
      const bool filled =
          (solution & (1UL << (row * NonogramGame::kSize + column))) != 0;
      drawNonogramMarkCell(
          gridLeft + column * kPreviewCellSize,
          gridTop + row * kPreviewCellSize, kPreviewCellSize,
          filled ? NonogramGame::CellState::Filled
                 : NonogramGame::CellState::Blank);
    }
  }
}

void drawReversiMarkCell(int x, int y, int size, ReversiGame::Disc disc,
                         bool legalMove = false) {
  epaper.fillRect(x, y, size, size, TFT_WHITE);
  epaper.drawRect(x, y, size, size, TFT_BLACK);
  const int centerX = x + size / 2;
  const int centerY = y + size / 2;
  const int radius = size / 2 - std::max(4, size / 10);
  if (disc == ReversiGame::Disc::Black) {
    epaper.fillCircle(centerX, centerY, radius, TFT_BLACK);
  } else if (disc == ReversiGame::Disc::White) {
    epaper.fillCircle(centerX, centerY, radius, TFT_BLACK);
    epaper.fillCircle(centerX, centerY, radius - std::max(3, size / 12),
                      TFT_WHITE);
  } else if (legalMove) {
    epaper.fillCircle(centerX, centerY, std::max(3, size / 14), TFT_BLACK);
  }
}

void drawReversiMenuCard() {
  epaper.fillRoundRect(kReversiMenuCard.x, kReversiMenuCard.y,
                       kReversiMenuCard.width, kReversiMenuCard.height, 14,
                       TFT_WHITE);
  epaper.drawRoundRect(kReversiMenuCard.x, kReversiMenuCard.y,
                       kReversiMenuCard.width, kReversiMenuCard.height, 14,
                       TFT_BLACK);

  constexpr int kPreviewCellSize = 36;
  const int gridLeft = kReversiMenuCard.x + 23;
  const int gridTop = kReversiMenuCard.y + 23;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      ReversiGame::Disc disc = ReversiGame::Disc::Empty;
      if ((row == 1 && column == 1) || (row == 2 && column == 2)) {
        disc = ReversiGame::Disc::White;
      } else if ((row == 1 && column == 2) ||
                 (row == 2 && column == 1)) {
        disc = ReversiGame::Disc::Black;
      }
      drawReversiMarkCell(gridLeft + column * kPreviewCellSize,
                          gridTop + row * kPreviewCellSize, kPreviewCellSize,
                          disc);
    }
  }
}

void drawBatteryStatus() {
  constexpr int kCenterY = 24;
  constexpr int kEdgeInset = 6;
  constexpr int kGaugeWidth = 22;
  constexpr int kGaugeHeight = 12;
  constexpr int kTerminalWidth = 5;
  constexpr int kTerminalHeight = 5;
  constexpr int kOutline = 1;
  const int gaugeX =
      kScreenWidth - kEdgeInset - kTerminalWidth - kGaugeWidth;
  const int gaugeY = kCenterY + 2 - kGaugeHeight / 2;
  const String percent =
      batteryPercent >= 0 ? String(batteryPercent) + "%" : "--%";

  epaper.setFreeFont(&FreeSansBold9pt7b);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(percent, gaugeX - 9, kCenterY, 1);
  text_render::drawBatteryGauge(
      epaper, gaugeX, gaugeY, kGaugeWidth, kGaugeHeight, batteryPercent,
      kOutline, kTerminalWidth, kTerminalHeight, TFT_BLACK, TFT_WHITE,
      externalPowerPresent);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

void drawStatusBar() {
  drawBatteryStatus();
  epaper.drawFastHLine(0, kStatusDividerY, kScreenWidth, TFT_BLACK);
}

void drawBackIndicator() {
  epaper.fillRoundRect(kBackButton.x, kBackButton.y, kBackButton.width,
                       kBackButton.height, 8, TFT_BLACK);
  const int centerY = kBackButton.y + kBackButton.height / 2;
  const int tipX = kBackButton.x + 9;
  const int headBaseX = tipX + 13;
  const int tailX = kBackButton.x + kBackButton.width - 9;
  epaper.fillTriangle(tipX, centerY, headBaseX, centerY - 11, headBaseX,
                      centerY + 11, TFT_WHITE);
  epaper.fillRect(headBaseX - 1, centerY - 3, tailX - headBaseX + 1, 7,
                  TFT_WHITE);
}

void drawGameStatusBar(const char* title) {
  drawBackIndicator();
  drawCentered(title, kScreenWidth / 2, 24, 4);
  drawStatusBar();
}

void drawMenu() {
  epaper.fillSprite(TFT_WHITE);
  drawGamesLogo(kScreenWidth / 2, 80, 72);
  drawLightsOutMenuCard();
  draw2048MenuCard();
  drawPipeConnectMenuCard();
  drawMinesweeperMenuCard();
  drawNonogramMenuCard();
  drawReversiMenuCard();
  drawStatusBar();
}

void drawStatus(const char* title, const char* detail) {
  epaper.fillSprite(TFT_WHITE);
  drawCentered(title, kScreenWidth / 2, 330, 4);
  drawCentered(detail, kScreenWidth / 2, 390, 4);
}

void drawRepoQr() {
  constexpr int kScale = 2;
  constexpr int kQuietModules = 4;
  constexpr int kQrPixels =
      (repo_qr::kModules + kQuietModules * 2) * kScale;
  constexpr int kMargin = 12;
  const int left = kScreenWidth - kQrPixels - kMargin;
  const int top = kScreenHeight - kQrPixels - kMargin;
  epaper.fillRect(left, top, kQrPixels, kQrPixels, TFT_WHITE);
  for (int row = 0; row < repo_qr::kModules; ++row) {
    for (int column = 0; column < repo_qr::kModules; ++column) {
      const uint32_t mask = 1UL << (repo_qr::kModules - 1 - column);
      if ((repo_qr::kRows[row] & mask) == 0) continue;
      epaper.fillRect(left + (column + kQuietModules) * kScale,
                      top + (row + kQuietModules) * kScale, kScale, kScale,
                      TFT_BLACK);
    }
  }
}

void drawSleepSplash() {
  epaper.fillSprite(TFT_WHITE);
  drawGamesLogo(kScreenWidth / 2, 390, 280);
  drawRepoQr();
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
  drawLightsOutBoard();
  drawButton(kNewButton, "NEW");
  drawButton(kResetButton, "RESET");
  drawGameStatusBar("LIGHTS OUT");
}

void draw2048Board() {
  epaper.fillRect(k2048BoardRegion.x, k2048BoardRegion.y,
                  k2048BoardRegion.width, k2048BoardRegion.height, TFT_WHITE);
  drawCentered("SCORE " + String(game2048.score()), 135, 128, 4);
  drawCentered("BEST " + String(game2048.bestScore()), 350, 128, 4);

  for (int row = 0; row < Game2048::kSize; ++row) {
    for (int column = 0; column < Game2048::kSize; ++column) {
      draw2048Tile(k2048GridLeft + column * k2048CellSize,
                   k2048GridTop + row * k2048CellSize, k2048CellSize,
                   game2048.at(row, column));
    }
  }

  if (game2048.gameOver()) {
    drawCentered("NO MOVES - TAP NEW", kScreenWidth / 2, 610, 4);
  } else if (game2048.won()) {
    drawCentered("2048! KEEP GOING", kScreenWidth / 2, 610, 4);
  }
}

void draw2048() {
  epaper.fillSprite(TFT_WHITE);
  draw2048Board();
  drawButton(kCenteredNewButton, "NEW");
  drawGameStatusBar("2048");
}

void drawPipeConnectBoard() {
  epaper.fillRect(kPipeBoardRegion.x, kPipeBoardRegion.y,
                  kPipeBoardRegion.width, kPipeBoardRegion.height, TFT_WHITE);
  for (int row = 0; row < PipeConnectGame::kSize; ++row) {
    for (int column = 0; column < PipeConnectGame::kSize; ++column) {
      drawPipeTile(kPipeGridLeft + column * kPipeCellSize,
                   kPipeGridTop + row * kPipeCellSize, kPipeCellSize,
                   pipeConnect.at(row, column));
    }
  }

  if (pipeConnect.solved()) {
    drawCentered("CONNECTED IN " + String(pipeConnect.moves()) + " TAPS",
                 kScreenWidth / 2, 610, 4);
  } else {
    drawCentered("CONNECT EVERY PIPE", kScreenWidth / 2, 610, 4);
  }
}

void drawPipeConnect() {
  epaper.fillSprite(TFT_WHITE);
  drawPipeConnectBoard();
  drawButton(kNewButton, "NEW");
  drawButton(kResetButton, "RESET");
  drawGameStatusBar("PIPE CONNECT");
}

void drawMinesweeperCell(int row, int column) {
  const int x = kMinesGridLeft + column * kMinesCellSize;
  const int y = kMinesGridTop + row * kMinesCellSize;
  const int centerX = x + kMinesCellSize / 2;
  const int centerY = y + kMinesCellSize / 2;
  const bool mine = minesweeper.isMine(row, column);
  const bool revealed = minesweeper.isRevealed(row, column);
  const bool flagged = minesweeper.isFlagged(row, column);
  const bool showMine = mine && minesweeper.gameOver();

  if (revealed && mine) {
    epaper.fillRect(x, y, kMinesCellSize, kMinesCellSize, TFT_BLACK);
    epaper.drawRect(x + 4, y + 4, kMinesCellSize - 8,
                    kMinesCellSize - 8, TFT_WHITE);
    drawMineSymbol(centerX, centerY, TFT_WHITE);
    return;
  }
  if (showMine) {
    epaper.fillRect(x, y, kMinesCellSize, kMinesCellSize, TFT_WHITE);
    epaper.drawRect(x, y, kMinesCellSize, kMinesCellSize, TFT_BLACK);
    drawMineSymbol(centerX, centerY, TFT_BLACK);
    return;
  }
  if (!revealed) {
    epaper.fillRect(x, y, kMinesCellSize, kMinesCellSize, TFT_BLACK);
    epaper.drawRect(x + 4, y + 4, kMinesCellSize - 8,
                    kMinesCellSize - 8, TFT_WHITE);
    if (flagged) drawFlagSymbol(centerX, centerY, TFT_WHITE);
    return;
  }

  epaper.fillRect(x, y, kMinesCellSize, kMinesCellSize, TFT_WHITE);
  epaper.drawRect(x, y, kMinesCellSize, kMinesCellSize, TFT_BLACK);
  const int adjacent = minesweeper.adjacentMines(row, column);
  if (adjacent > 0) {
    drawCenteredNumber(adjacent, centerX, centerY, 4, TFT_BLACK, TFT_WHITE);
  }
}

void drawMinesweeperBoard() {
  epaper.fillRect(kMinesBoardRegion.x, kMinesBoardRegion.y,
                  kMinesBoardRegion.width, kMinesBoardRegion.height,
                  TFT_WHITE);
  for (int row = 0; row < MiniMinesweeperGame::kSize; ++row) {
    for (int column = 0; column < MiniMinesweeperGame::kSize; ++column) {
      drawMinesweeperCell(row, column);
    }
  }

  if (minesweeper.won()) {
    drawCentered("FIELD CLEARED!", kScreenWidth / 2, 610, 4);
  } else if (minesweeper.lost()) {
    drawCentered("MINE HIT - TAP NEW", kScreenWidth / 2, 610, 4);
  } else {
    drawCentered("6 MINES", kScreenWidth / 2, 610, 4);
  }
}

void drawMinesweeper() {
  epaper.fillSprite(TFT_WHITE);
  drawMinesweeperBoard();
  drawButton(kCenteredNewButton, "NEW");
  drawGameStatusBar("MINESWEEPER");
}

void drawNonogramCell(int row, int column) {
  drawNonogramMarkCell(kNonogramGridLeft + column * kNonogramCellSize,
                       kNonogramGridTop + row * kNonogramCellSize,
                       kNonogramCellSize, nonogram.at(row, column));
}

void drawNonogramClues() {
  uint8_t clues[NonogramGame::kSize] = {};
  for (int column = 0; column < NonogramGame::kSize; ++column) {
    const int count = nonogram.columnClues(column, clues);
    for (int index = 0; index < count; ++index) {
      const int centerY =
          kNonogramGridTop - 21 - (count - index - 1) * 31;
      drawCenteredNumber(
          clues[index],
          kNonogramGridLeft + column * kNonogramCellSize +
              kNonogramCellSize / 2,
          centerY, 4, TFT_BLACK, TFT_WHITE);
    }
  }

  for (int row = 0; row < NonogramGame::kSize; ++row) {
    const int count = nonogram.rowClues(row, clues);
    for (int index = 0; index < count; ++index) {
      const int centerX =
          kNonogramGridLeft - 21 - (count - index - 1) * 31;
      drawCenteredNumber(
          clues[index], centerX,
          kNonogramGridTop + row * kNonogramCellSize +
              kNonogramCellSize / 2,
          4, TFT_BLACK, TFT_WHITE);
    }
  }
}

void drawNonogramBoard() {
  epaper.fillRect(kNonogramBoardRegion.x, kNonogramBoardRegion.y,
                  kNonogramBoardRegion.width, kNonogramBoardRegion.height,
                  TFT_WHITE);
  drawNonogramClues();
  for (int row = 0; row < NonogramGame::kSize; ++row) {
    for (int column = 0; column < NonogramGame::kSize; ++column) {
      drawNonogramCell(row, column);
    }
  }
  if (nonogram.solved()) {
    drawCentered("PUZZLE SOLVED!", kScreenWidth / 2, 590, 4);
  }
}

void drawNonogram() {
  epaper.fillSprite(TFT_WHITE);
  drawNonogramBoard();
  drawButton(kNewButton, "NEW");
  drawButton(kResetButton, "RESET");
  drawGameStatusBar("NONOGRAM");
}

void drawReversiCell(int row, int column) {
  drawReversiMarkCell(kReversiGridLeft + column * kReversiCellSize,
                      kReversiGridTop + row * kReversiCellSize,
                      kReversiCellSize, reversi.at(row, column),
                      reversi.isLegalMove(row, column));
}

void drawReversiBoard(const char* status = nullptr) {
  epaper.fillRect(kReversiBoardRegion.x, kReversiBoardRegion.y,
                  kReversiBoardRegion.width, kReversiBoardRegion.height,
                  TFT_WHITE);
  drawCentered("BLACK " + String(reversi.score(ReversiGame::Disc::Black)) +
                   "    WHITE " +
                   String(reversi.score(ReversiGame::Disc::White)),
               kScreenWidth / 2, 120, 4);
  for (int row = 0; row < ReversiGame::kSize; ++row) {
    for (int column = 0; column < ReversiGame::kSize; ++column) {
      drawReversiCell(row, column);
    }
  }

  if (reversi.gameOver()) {
    const ReversiGame::Disc winner = reversi.winner();
    const String result =
        winner == ReversiGame::Disc::Black
            ? "BLACK WINS"
            : winner == ReversiGame::Disc::White ? "WHITE WINS" : "DRAW";
    drawCentered(result, kScreenWidth / 2, 610, 4);
  } else if (status != nullptr) {
    drawCentered(status, kScreenWidth / 2, 610, 4);
  } else {
    drawCentered(reversi.currentPlayer() == ReversiGame::Disc::Black
                     ? "BLACK TO MOVE"
                     : "WHITE TO MOVE",
                 kScreenWidth / 2, 610, 4);
  }
}

const char* reversiModeLabel() {
  return reversiMode == ReversiMode::SinglePlayer ? "1 PLAYER" : "2 PLAYERS";
}

void drawReversi() {
  epaper.fillSprite(TFT_WHITE);
  drawReversiBoard();
  drawButton(kNewButton, "NEW");
  drawButton(kResetButton, reversiModeLabel());
  drawGameStatusBar("REVERSI");
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

void startNew2048() {
  game2048.start(esp_random(), esp_random());
  game2048Saved = true;
}

void startNewPipeConnect() {
  pipeConnect.start(esp_random());
  pipeConnectSaved = true;
}

void startNewMinesweeper() {
  minesweeper.start(esp_random());
  minesweeperSaved = true;
}

void startNewNonogram() {
  size_t puzzleIndex = esp_random() % kNonogramPuzzleCount;
  if (nonogramSaved &&
      kNonogramPuzzles[puzzleIndex] == nonogram.snapshot().solution) {
    puzzleIndex = (puzzleIndex + 1) % kNonogramPuzzleCount;
  }
  nonogram.start(kNonogramPuzzles[puzzleIndex]);
  nonogramSaved = true;
}

void startNewReversi() {
  reversi.start();
  reversiSaved = true;
}

void playReversiComputerTurns() {
  while (reversiMode == ReversiMode::SinglePlayer &&
         reversi.currentPlayer() == ReversiGame::Disc::White &&
         !reversi.gameOver()) {
    int row = -1;
    int column = -1;
    if (!reversi.chooseBestMove(kReversiAiDepth, row, column)) {
      LOG.println("[games] Reversi AI could not find a legal move");
      return;
    }
    LOG.printf("[games] Reversi AI plays row=%d column=%d\n", row, column);
    const ReversiGame::MoveResult result = reversi.play(row, column);
    if (result == ReversiGame::MoveResult::Illegal) {
      LOG.println("[games] Reversi AI selected an illegal move");
      return;
    }
  }
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

bool refreshScreen(const char* action) {
  const uint32_t startedAtMs = millis();
  epaper.sleep();
  epaper.update();
  const E1005FastRefresh::Result result = fastRefresh.begin();
  if (result != E1005FastRefresh::Result::Ok) {
    LOG.printf("[games] %s full refresh recovery failed: %s\n", action,
               E1005FastRefresh::resultMessage(result));
    touchReady = false;
    return false;
  }
  LOG.printf("[games] %s full refresh=%lu ms\n", action,
             static_cast<unsigned long>(millis() - startedAtMs));
  return true;
}

void showMenu() {
  if (currentScreen == Screen::LightsOut && lightsOutSaved) {
    LOG.println("[games] auto-saving Lights Out");
  } else if (currentScreen == Screen::Game2048 && game2048Saved) {
    LOG.println("[games] auto-saving 2048");
  } else if (currentScreen == Screen::PipeConnect && pipeConnectSaved) {
    LOG.println("[games] auto-saving Pipe Connect");
  } else if (currentScreen == Screen::Minesweeper && minesweeperSaved) {
    LOG.println("[games] auto-saving Minesweeper");
  } else if (currentScreen == Screen::Nonogram && nonogramSaved) {
    LOG.println("[games] auto-saving Nonogram");
  } else if (currentScreen == Screen::Reversi && reversiSaved) {
    LOG.println("[games] auto-saving Reversi");
  }
  currentScreen = Screen::Menu;
  saveResumeState();
  drawMenu();
  refreshScreen("menu");
}

void showLightsOut() {
  if (!lightsOutSaved) startNewPuzzle();
  currentScreen = Screen::LightsOut;
  drawLightsOut();
  refreshScreen("lights-out screen");
}

void show2048() {
  if (!game2048Saved) startNew2048();
  currentScreen = Screen::Game2048;
  draw2048();
  refreshScreen("2048 screen");
}

void showPipeConnect() {
  if (!pipeConnectSaved) startNewPipeConnect();
  currentScreen = Screen::PipeConnect;
  drawPipeConnect();
  refreshScreen("Pipe Connect screen");
}

void showMinesweeper() {
  if (!minesweeperSaved) startNewMinesweeper();
  currentScreen = Screen::Minesweeper;
  drawMinesweeper();
  refreshScreen("Minesweeper screen");
}

void showNonogram() {
  if (!nonogramSaved) startNewNonogram();
  currentScreen = Screen::Nonogram;
  drawNonogram();
  refreshScreen("Nonogram screen");
}

void showReversi() {
  if (!reversiSaved) startNewReversi();
  playReversiComputerTurns();
  currentScreen = Screen::Reversi;
  drawReversi();
  refreshScreen("Reversi screen");
}

void updateLightsOut(const char* action) {
  drawLightsOutBoard();
  refreshRegion(kBoardRegion, action);
}

void update2048(const char* action) {
  draw2048Board();
  refreshRegion(k2048BoardRegion, action);
}

void updatePipeConnect(const char* action) {
  drawPipeConnectBoard();
  refreshRegion(kPipeBoardRegion, action);
}

void updatePipeConnectCell(int row, int column, const char* action) {
  const int x = kPipeGridLeft + column * kPipeCellSize;
  const int y = kPipeGridTop + row * kPipeCellSize;
  drawPipeTile(x, y, kPipeCellSize, pipeConnect.at(row, column));
  const E1005FastRefresh::Region cellRegion = {
      static_cast<uint16_t>(x),
      static_cast<uint16_t>(y),
      static_cast<uint16_t>(kPipeCellSize),
      static_cast<uint16_t>(kPipeCellSize),
  };
  refreshRegion(cellRegion, action);
}

void updateMinesweeper(const char* action) {
  drawMinesweeperBoard();
  refreshRegion(kMinesBoardRegion, action);
}

void updateMinesweeperCell(int row, int column, const char* action) {
  drawMinesweeperCell(row, column);
  const E1005FastRefresh::Region cellRegion = {
      kMinesGridLeft + column * kMinesCellSize,
      kMinesGridTop + row * kMinesCellSize,
      kMinesCellSize,
      kMinesCellSize,
  };
  refreshRegion(cellRegion, action);
}

void updateNonogram(const char* action) {
  drawNonogramBoard();
  refreshRegion(kNonogramBoardRegion, action);
}

void updateNonogramCell(int row, int column, const char* action) {
  drawNonogramCell(row, column);
  const E1005FastRefresh::Region cellRegion = {
      kNonogramGridLeft + column * kNonogramCellSize,
      kNonogramGridTop + row * kNonogramCellSize,
      kNonogramCellSize,
      kNonogramCellSize,
  };
  refreshRegion(cellRegion, action);
}

void updateReversi(const char* action, const char* status = nullptr) {
  drawReversiBoard(status);
  refreshRegion(kReversiBoardRegion, action);
}

void updateReversiMode(const char* action) {
  drawReversiBoard();
  drawButton(kNewButton, "NEW");
  drawButton(kResetButton, reversiModeLabel());
  refreshRegion(kReversiModeRegion, action);
}

void handleMenuTouch(const Gt911Touch::Point& point) {
  if (kLightsOutMenuCard.contains(point.x, point.y)) {
    hardware::beep();
    showLightsOut();
  } else if (k2048MenuCard.contains(point.x, point.y)) {
    hardware::beep();
    show2048();
  } else if (kPipeConnectMenuCard.contains(point.x, point.y)) {
    hardware::beep();
    showPipeConnect();
  } else if (kMinesweeperMenuCard.contains(point.x, point.y)) {
    hardware::beep();
    showMinesweeper();
  } else if (kNonogramMenuCard.contains(point.x, point.y)) {
    hardware::beep();
    showNonogram();
  } else if (kReversiMenuCard.contains(point.x, point.y)) {
    hardware::beep();
    showReversi();
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

void handlePipeConnectTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    startNewPipeConnect();
    updatePipeConnect("new Pipe Connect game");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    pipeConnect.reset();
    updatePipeConnect("reset Pipe Connect game");
    return;
  }
  if (point.x < kPipeGridLeft ||
      point.x >= kPipeGridLeft + kPipeGridSize ||
      point.y < kPipeGridTop ||
      point.y >= kPipeGridTop + kPipeGridSize) {
    return;
  }

  const int column = (point.x - kPipeGridLeft) / kPipeCellSize;
  const int row = (point.y - kPipeGridTop) / kPipeCellSize;
  if (pipeConnect.rotate(row, column)) {
    if (pipeConnect.solved()) {
      updatePipeConnect("Pipe Connect solved");
    } else {
      updatePipeConnectCell(row, column, "Pipe Connect rotation");
    }
  }
}

void handleNonogramTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    startNewNonogram();
    updateNonogram("new Nonogram puzzle");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    nonogram.reset();
    updateNonogram("reset Nonogram puzzle");
    return;
  }
  if (point.x < kNonogramGridLeft ||
      point.x >= kNonogramGridLeft + kNonogramGridSize ||
      point.y < kNonogramGridTop ||
      point.y >= kNonogramGridTop + kNonogramGridSize) {
    return;
  }

  const int column = (point.x - kNonogramGridLeft) / kNonogramCellSize;
  const int row = (point.y - kNonogramGridTop) / kNonogramCellSize;
  if (nonogram.cycle(row, column)) {
    if (nonogram.solved()) {
      updateNonogram("Nonogram solved");
    } else {
      updateNonogramCell(row, column, "Nonogram mark");
    }
  }
}

void handleReversiTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    startNewReversi();
    updateReversi("new Reversi game");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    reversiMode = reversiMode == ReversiMode::SinglePlayer
                       ? ReversiMode::TwoPlayer
                       : ReversiMode::SinglePlayer;
    startNewReversi();
    updateReversiMode("Reversi mode");
    return;
  }
  if (point.x < kReversiGridLeft ||
      point.x >= kReversiGridLeft + kReversiGridSize ||
      point.y < kReversiGridTop ||
      point.y >= kReversiGridTop + kReversiGridSize) {
    return;
  }

  const int column = (point.x - kReversiGridLeft) / kReversiCellSize;
  const int row = (point.y - kReversiGridTop) / kReversiCellSize;
  if (reversiMode == ReversiMode::SinglePlayer &&
      reversi.currentPlayer() != ReversiGame::Disc::Black) {
    LOG.println("[games] ignoring Reversi touch during computer turn");
    return;
  }
  const ReversiGame::MoveResult result = reversi.play(row, column);
  if (result != ReversiGame::MoveResult::Illegal) {
    const bool humanOpponentPassed =
        result == ReversiGame::MoveResult::OpponentPassed;
    if (reversiMode == ReversiMode::SinglePlayer &&
        reversi.currentPlayer() == ReversiGame::Disc::White &&
        !reversi.gameOver()) {
      playReversiComputerTurns();
    }
    const char* passStatus =
        humanOpponentPassed
            ? reversi.currentPlayer() == ReversiGame::Disc::Black
                  ? "WHITE PASSES - BLACK AGAIN"
                  : "BLACK PASSES - WHITE AGAIN"
            : nullptr;
    updateReversi(result == ReversiGame::MoveResult::GameOver
                       ? "Reversi game over"
                       : humanOpponentPassed ? "Reversi pass"
                                             : "Reversi move",
                   passStatus);
  }
}

bool handleMinesweeperTouchStart(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    showMenu();
    return true;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    startNewMinesweeper();
    updateMinesweeper("new Minesweeper game");
    return true;
  }
  return point.x < kMinesGridLeft ||
         point.x >= kMinesGridLeft + kMinesGridSize ||
         point.y < kMinesGridTop ||
         point.y >= kMinesGridTop + kMinesGridSize;
}

void handleMinesweeperCellTouch(const Gt911Touch::Point& start,
                                const Gt911Touch::Point& end,
                                uint32_t heldMs) {
  const int dx = static_cast<int>(end.x) - static_cast<int>(start.x);
  const int dy = static_cast<int>(end.y) - static_cast<int>(start.y);
  if (dx < -kMinesTouchMoveTolerance || dx > kMinesTouchMoveTolerance ||
      dy < -kMinesTouchMoveTolerance || dy > kMinesTouchMoveTolerance) {
    return;
  }
  const int column = (start.x - kMinesGridLeft) / kMinesCellSize;
  const int row = (start.y - kMinesGridTop) / kMinesCellSize;
  if (heldMs >= kMinesFlagHoldMs) {
    if (minesweeper.toggleFlag(row, column)) {
      updateMinesweeperCell(row, column, "Minesweeper flag");
    }
    return;
  }

  const MiniMinesweeperGame::RevealResult result =
      minesweeper.reveal(row, column);
  if (result != MiniMinesweeperGame::RevealResult::NoChange) {
    updateMinesweeper(result == MiniMinesweeperGame::RevealResult::Won
                          ? "Minesweeper won"
                          : result == MiniMinesweeperGame::RevealResult::Lost
                                ? "Minesweeper mine"
                                : "Minesweeper reveal");
  }
}

bool handle2048TouchStart(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    showMenu();
    return true;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    startNew2048();
    update2048("new 2048 game");
    return true;
  }
  return point.x < k2048GridLeft ||
         point.x >= k2048GridLeft + k2048GridSize ||
         point.y < k2048GridTop ||
         point.y >= k2048GridTop + k2048GridSize;
}

void handle2048Swipe(const Gt911Touch::Point& start,
                     const Gt911Touch::Point& end) {
  const int dx = static_cast<int>(end.x) - static_cast<int>(start.x);
  const int dy = static_cast<int>(end.y) - static_cast<int>(start.y);
  const int absX = dx < 0 ? -dx : dx;
  const int absY = dy < 0 ? -dy : dy;
  if (std::max(absX, absY) < kSwipeThreshold) return;

  Game2048::Direction direction;
  const char* action;
  if (absX > absY) {
    direction = dx < 0 ? Game2048::Direction::Left
                       : Game2048::Direction::Right;
    action = dx < 0 ? "2048 swipe left" : "2048 swipe right";
  } else {
    direction =
        dy < 0 ? Game2048::Direction::Up : Game2048::Direction::Down;
    action = dy < 0 ? "2048 swipe up" : "2048 swipe down";
  }

  if (game2048.move(direction, esp_random())) {
    update2048(action);
  } else {
    LOG.printf("[games] %s did not change the board\n", action);
  }
}

void pollTouch() {
  if (!touchReady) return;

  Gt911Touch::Point point = {};
  const Gt911Touch::PollResult result = touch.poll(point);
  if (result == Gt911Touch::PollResult::Release) {
    if (touchActive && currentScreen == Screen::Game2048 &&
        !touchActionHandled) {
      handle2048Swipe(touchStart, touchLast);
    } else if (touchActive && currentScreen == Screen::Minesweeper &&
               !touchActionHandled) {
      handleMinesweeperCellTouch(touchStart, touchLast,
                                 millis() - touchStartedAtMs);
    }
    touchActive = false;
    touchActionHandled = false;
    return;
  }
  if (result != Gt911Touch::PollResult::Touch) return;
  recordActivity();
  if (touchActive) {
    touchLast = point;
    return;
  }

  touchActive = true;
  touchActionHandled = false;
  touchStart = point;
  touchLast = point;
  touchStartedAtMs = millis();
  if (currentScreen == Screen::Menu) {
    handleMenuTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::LightsOut) {
    handleLightsOutTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::PipeConnect) {
    handlePipeConnectTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Minesweeper) {
    touchActionHandled = handleMinesweeperTouchStart(point);
  } else if (currentScreen == Screen::Nonogram) {
    handleNonogramTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Reversi) {
    handleReversiTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Game2048) {
    touchActionHandled = handle2048TouchStart(point);
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
             buttonPressTypeName(event.type));

  if (button.pin != board::PIN_BUTTON_0) {
    LOG.printf("[games] ignoring %s button\n", button.name);
    return;
  }
#ifdef ENABLE_SCREENSHOT_GESTURE
  if (event.type == ButtonPressType::Screenshot) {
    hardware::beep();
    if (!sdReady) {
      LOG.println("[screenshot] request ignored: SD card is unavailable");
    } else if (!screenshot::saveScreenshotBmp(
                   epaper, kScreenWidth, kScreenHeight)) {
      LOG.println("[screenshot] capture failed");
    }
    return;
  }
#endif
  if (event.type == ButtonPressType::Long) {
    while (digitalRead(button.pin) == LOW) delay(10);
    powerDownAndSleep();
  } else if (currentScreen != Screen::Menu) {
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
  const int updatedPercent = gauge.valid ? gauge.percent : -1;
  const bool statusChanged =
      !batteryStatusSampled || batteryPercent != updatedPercent ||
      externalPowerPresent != externalPower;
  batteryStatusSampled = true;
  batteryPercent = updatedPercent;
  externalPowerPresent = externalPower;

  if (!gauge.valid) {
    LOG.println("[battery] BQ27220 battery gauge unavailable");
  } else {
    LOG.printf("[battery] %d%% (%.3fV), external_power=%s\n", gauge.percent,
               gauge.voltage, externalPower ? "yes" : "no");
    if (low_battery::shouldWarn(true, gauge.valid, externalPower,
                                gauge.percent, kLowBatteryThresholdPct)) {
      LOG.printf("[battery] below %d%%; requesting recharge\n",
                 kLowBatteryThresholdPct);
      powerDownAndSleep(SleepScreen::Charge, gauge.percent);
    }
  }

  if (statusChanged && fastRefresh.ready()) {
    epaper.fillRect(kBatteryStatusRegion.x, kBatteryStatusRegion.y,
                   kBatteryStatusRegion.width, kBatteryStatusRegion.height,
                   TFT_WHITE);
    drawBatteryStatus();
    refreshRegion(kBatteryStatusRegion, "battery status");
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
#ifdef ENABLE_SCREENSHOT_GESTURE
  sdReady = sd_card::mount(epaper.getSPIinstance(), "/games");
#else
  const bool sdReady = sd_card::mount(epaper.getSPIinstance(), "/games");
#endif
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
  } else if (resumed && currentScreen == Screen::Game2048) {
    draw2048();
  } else if (resumed && currentScreen == Screen::PipeConnect) {
    drawPipeConnect();
  } else if (resumed && currentScreen == Screen::Minesweeper) {
    drawMinesweeper();
  } else if (resumed && currentScreen == Screen::Nonogram) {
    drawNonogram();
  } else if (resumed && currentScreen == Screen::Reversi) {
    playReversiComputerTurns();
    drawReversi();
  } else {
    currentScreen = Screen::Menu;
    drawMenu();
  }
  const char* screenName =
      currentScreen == Screen::LightsOut
          ? "saved Lights Out"
          : currentScreen == Screen::Game2048
                ? "saved 2048"
                : currentScreen == Screen::PipeConnect
                      ? "saved Pipe Connect"
                      : currentScreen == Screen::Minesweeper
                            ? "saved Minesweeper"
                            : currentScreen == Screen::Nonogram
                                  ? "saved Nonogram"
                                  : currentScreen == Screen::Reversi
                                        ? "saved Reversi"
                                        : "menu";
  LOG.printf("[games] refreshing %s\n", screenName);
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
