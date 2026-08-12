#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/uart.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

#include "app_logger.h"
#include "battery_gauge.h"
#include "board_pins.h"
#include "connect_four_game.h"
#include "crossword_game.h"
#include "driver.h"
#include "dither.h"
#include "dots_and_boxes_game.h"
#include "double_tap_tracker.h"
#include "e1005_fast_refresh.h"
#include "epaper_setup.h"
#include "epub_archive.h"
#include "epub_browser_logic.h"
#include "epub_cover.h"
#include "epub_latin_fonts.h"
#include "epub_text.h"
#include "falling_blocks_game.h"
#include "game_2048.h"
#include "game_help_text.h"
#include "game_language_store.h"
#include "game_localization.h"
#include "game_progress_store.h"
#include "game_ui_fonts.h"
#include "gt911_touch.h"
#include "hardware.h"
#include "image_loader.h"
#include "klondike_game.h"
#include "lights_out_game.h"
#include "low_battery.h"
#include "mahjong_solitaire_game.h"
#include "menu_edge_swipe.h"
#include "mini_minesweeper_game.h"
#include "nonogram_game.h"
#include "ok_button_action.h"
#include "peg_solitaire_game.h"
#include "peripheral_power.h"
#include "pipe_connect_game.h"
#include "power_latch.h"
#include "repo_qr.h"
#include "reversi_game.h"
#include "sd_card.h"
#include "sd_card_identity.h"
#include "sd_ota.h"
#include "sd_readonly_browser.h"
#include "slitherlink_game.h"
#include "sokoban_game.h"
#include "sudoku_game.h"
#include "text_render.h"
#include "usb_screen_capture.h"
#include "word_search_game.h"

#if RETERMINAL_MODEL != 1005
#error "Sticky Arcade supports only reTerminal E1005"
#endif

TimestampedLogger appLog(Serial1);
EPaper epaper;
usb_screen_capture::Server usbScreenCapture;

namespace {

using game_localization::Language;
using game_localization::TextId;

constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 800;
constexpr char kAppName[] = "Sticky Arcade";
constexpr char kBrandName[] = "STICKY ARCADE";
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
constexpr int kDotsGridLeft = 80;
constexpr int kDotsGridTop = 155;
constexpr int kDotsSpacing = 80;
constexpr int kDotsGridSize = kDotsSpacing * DotsAndBoxesGame::kBoxSize;
constexpr int kSokobanGridTop = 140;
constexpr int kSokobanMaxBoardPixels = 420;
constexpr int kPegGridLeft = 44;
constexpr int kPegGridTop = 140;
constexpr int kPegCellSize = 56;
constexpr int kPegGridSize = kPegCellSize * PegSolitaireGame::kSize;
constexpr int kSlitherlinkGridLeft = 60;
constexpr int kSlitherlinkGridTop = 150;
constexpr int kSlitherlinkCellSize = 72;
constexpr int kSlitherlinkGridSize =
    kSlitherlinkCellSize * SlitherlinkGame::kSize;
constexpr int kSudokuGridLeft = 42;
constexpr int kSudokuGridTop = 112;
constexpr int kSudokuCellSize = 44;
constexpr int kSudokuGridSize = kSudokuCellSize * SudokuGame::kSize;
constexpr int kCrosswordGridTop = 92;
constexpr int kCrosswordMaxGridPixels = 360;
constexpr int kKlondikeCardWidth = 58;
constexpr int kKlondikeCardHeight = 78;
constexpr int kKlondikeColumnStep = 66;
constexpr int kKlondikeGridLeft = 10;
constexpr int kKlondikeTableauTop = 176;
constexpr int kMahjongTileWidth = 34;
constexpr int kMahjongTileHeight = 48;
constexpr int kMahjongColumnStep = 37;
constexpr int kMahjongRowStep = 58;
constexpr int kMahjongGridLeft = 16;
constexpr int kMahjongGridTop = 98;
constexpr int kMahjongLayerOffset = 5;
constexpr int kFallingGridLeft = 18;
constexpr int kFallingGridTop = 104;
constexpr int kFallingCellSize = 34;
constexpr int kFallingGridWidth =
    kFallingCellSize * FallingBlocksGame::kWidth;
constexpr int kFallingGridHeight =
    kFallingCellSize * FallingBlocksGame::kHeight;
constexpr int kConnectFourGridLeft = 37;
constexpr int kConnectFourGridTop = 150;
constexpr int kConnectFourCellSize = 58;
constexpr int kConnectFourGridWidth =
    kConnectFourCellSize * ConnectFourGame::kColumns;
constexpr int kConnectFourGridHeight =
    kConnectFourCellSize * ConnectFourGame::kRows;
constexpr int kWordSearchGridLeft = 33;
constexpr int kWordSearchGridTop = 92;
constexpr int kWordSearchCellSize = 46;
constexpr int kWordSearchGridSize =
    kWordSearchCellSize * WordSearchGame::kSize;
constexpr int kKeyboardKeyHeight = 42;
constexpr int kKeyboardFirstRowY = 586;
constexpr int kKeyboardRowGap = 6;
constexpr int kStatusDividerY = 48;
constexpr int kSwipeThreshold = 45;
constexpr int kMenuSwipeEdgeWidth = 40;
constexpr int kMinesTouchMoveTolerance = 20;
constexpr int kReversiAiDepth = 3;
constexpr int kConnectFourAiDepth = 5;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kMinesFlagHoldMs = 650;
constexpr uint32_t kKlondikeDoubleTapMs = 800;
constexpr uint16_t kKlondikeWasteTapTarget = 0x100;
constexpr uint16_t kKlondikeTableauTapTarget = 0x200;
constexpr uint32_t kBatteryCheckIntervalMs = 60000;
constexpr uint32_t kInactivitySleepMs = 5UL * 60UL * 1000UL;
constexpr int kLowBatteryThresholdPct = 5;
constexpr uint32_t kPersistedStateMagic = 0x47414D45;
constexpr uint16_t kPersistedStateVersion = 18;
constexpr uint32_t kLightsOutSavedFlag = 1UL << 0;
constexpr uint32_t k2048SavedFlag = 1UL << 1;
constexpr uint32_t kPipeConnectSavedFlag = 1UL << 2;
constexpr uint32_t kMinesweeperSavedFlag = 1UL << 3;
constexpr uint32_t kNonogramSavedFlag = 1UL << 4;
constexpr uint32_t kReversiSavedFlag = 1UL << 5;
constexpr uint32_t kDotsAndBoxesSavedFlag = 1UL << 6;
constexpr uint32_t kSokobanSavedFlag = 1UL << 7;
constexpr uint32_t kPegSolitaireSavedFlag = 1UL << 8;
constexpr uint32_t kSlitherlinkSavedFlag = 1UL << 9;
constexpr uint32_t kSudokuSavedFlag = 1UL << 10;
constexpr uint32_t kCrosswordSavedFlag = 1UL << 11;
constexpr uint32_t kKlondikeSavedFlag = 1UL << 12;
constexpr uint32_t kMahjongSavedFlag = 1UL << 13;
constexpr uint32_t kFallingBlocksSavedFlag = 1UL << 14;
constexpr uint32_t kConnectFourSavedFlag = 1UL << 15;
constexpr uint32_t kWordSearchSavedFlag = 1UL << 16;
constexpr char k2048HighScoreKey[] = "2048_best";
constexpr char kSokobanProgressKey[] = "sokoban_level";
constexpr char kReaderCjkFont16Path[] = "/fonts/epub_cjk_16.vlw";
constexpr char kReaderCjkFont16Name[] = "fonts/epub_cjk_16";
constexpr char kReaderCjkFont24Path[] = "/fonts/epub_cjk_24.vlw";
constexpr char kReaderCjkFont24Name[] = "fonts/epub_cjk_24";
constexpr char kReaderLatinFont16Path[] = "/fonts/sans_bold_16.vlw";
constexpr char kReaderLatinFont16Name[] = "fonts/sans_bold_16";
constexpr char kReaderLatinFont24Path[] = "/fonts/sans_bold_24.vlw";
constexpr char kReaderLatinFont24Name[] = "fonts/sans_bold_24";
constexpr size_t kReaderPathCapacity = 192;
constexpr size_t kReaderTextWidth = kScreenWidth - 36;
constexpr size_t kReaderLinesPerPage = 20;
constexpr int kReaderLineHeight = 32;
constexpr int kBrowserRowsPerPage = 8;
constexpr int kBrowserRowTop = 96;
constexpr int kBrowserRowHeight = 82;
constexpr int kReaderCoverLeft = 18;
constexpr int kReaderCoverTop = 64;
constexpr int kReaderCoverMaximumWidth = kScreenWidth - kReaderCoverLeft * 2;
constexpr int kReaderCoverMaximumHeight = 680;
constexpr int kMenuPreviewSize = 190;

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

constexpr Rect kMenuCardSlots[] = {
    {40, 62, 190, 218},  {250, 62, 190, 218},
    {40, 292, 190, 218}, {250, 292, 190, 218},
    {40, 522, 190, 218}, {250, 522, 190, 218},
};
constexpr Rect kLanguageButtons[] = {
    {60, 145, 360, 72},
    {60, 245, 360, 72},
    {60, 345, 360, 72},
    {60, 445, 360, 72},
    {60, 545, 360, 72},
};
Rect kLightsOutMenuCard = kMenuCardSlots[0];
Rect k2048MenuCard = kMenuCardSlots[1];
Rect kPipeConnectMenuCard = kMenuCardSlots[2];
Rect kMinesweeperMenuCard = kMenuCardSlots[3];
Rect kNonogramMenuCard = kMenuCardSlots[4];
Rect kReversiMenuCard = kMenuCardSlots[5];
Rect kDotsAndBoxesMenuCard = kMenuCardSlots[0];
Rect kSokobanMenuCard = kMenuCardSlots[1];
Rect kPegSolitaireMenuCard = kMenuCardSlots[2];
Rect kSlitherlinkMenuCard = kMenuCardSlots[3];
Rect kSudokuMenuCard = kMenuCardSlots[4];
Rect kCrosswordMenuCard = kMenuCardSlots[5];
Rect kKlondikeMenuCard = kMenuCardSlots[0];
Rect kMahjongMenuCard = kMenuCardSlots[1];
Rect kEpubReaderMenuCard = kMenuCardSlots[2];
Rect kFallingBlocksMenuCard = kMenuCardSlots[3];
Rect kConnectFourMenuCard = kMenuCardSlots[4];
Rect kWordSearchMenuCard = kMenuCardSlots[5];
constexpr Rect kPreviousPageButton = {8, 756, 48, 36};
constexpr Rect kNextPageButton = {424, 756, 48, 36};
constexpr Rect kBackButton = {8, 6, 48, 36};
constexpr Rect kSettingsButton = {8, 6, 36, 36};
constexpr Rect kHelpButton = {344, 6, 36, 36};
constexpr Rect kNewButton = {30, 688, 190, 66};
constexpr Rect kResetButton = {260, 688, 190, 66};
constexpr Rect kCenteredNewButton = {145, 688, 190, 66};
constexpr Rect kKlondikeStockSlot = {10, 72, kKlondikeCardWidth,
                                     kKlondikeCardHeight};
constexpr Rect kKlondikeWasteSlot = {80, 72, kKlondikeCardWidth,
                                     kKlondikeCardHeight};
constexpr E1005FastRefresh::Region kBatteryStatusRegion = {390, 0, 90, 48};
constexpr E1005FastRefresh::Region kBoardRegion = {30, 80, 420, 600};
constexpr E1005FastRefresh::Region k2048BoardRegion = {30, 80, 420, 585};
constexpr E1005FastRefresh::Region kPipeBoardRegion = {25, 80, 430, 585};
constexpr E1005FastRefresh::Region kMinesBoardRegion = {25, 80, 430, 585};
constexpr E1005FastRefresh::Region kNonogramBoardRegion = {20, 64, 440, 584};
constexpr E1005FastRefresh::Region kReversiBoardRegion = {25, 80, 430, 584};
constexpr E1005FastRefresh::Region kReversiModeRegion = {25, 80, 430, 674};
constexpr E1005FastRefresh::Region kDotsBoardRegion = {25, 80, 430, 584};
constexpr E1005FastRefresh::Region kSokobanBoardRegion = {25, 80, 430, 674};
constexpr E1005FastRefresh::Region kPegBoardRegion = {25, 80, 430, 584};
constexpr E1005FastRefresh::Region kSlitherlinkBoardRegion = {25, 80, 430, 584};
constexpr E1005FastRefresh::Region kSudokuBoardRegion = {20, 64, 440, 610};
constexpr E1005FastRefresh::Region kCrosswordBoardRegion = {20, 64, 440, 690};
constexpr E1005FastRefresh::Region kKlondikeBoardRegion = {5, 56, 470, 620};
constexpr E1005FastRefresh::Region kMahjongBoardRegion = {8, 64, 464, 612};
constexpr E1005FastRefresh::Region kFallingBlocksBoardRegion = {8, 64, 464,
                                                               620};
constexpr E1005FastRefresh::Region kConnectFourBoardRegion = {25, 80, 430, 584};
constexpr E1005FastRefresh::Region kConnectFourModeRegion = {25, 80, 430, 674};
constexpr E1005FastRefresh::Region kWordSearchBoardRegion = {20, 64, 440, 624};
constexpr E1005FastRefresh::Region kReaderRegion = {0, 48, 480, 752};

enum class Screen {
  Menu,
  LightsOut,
  Game2048,
  PipeConnect,
  Minesweeper,
  Nonogram,
  Reversi,
  DotsAndBoxes,
  Sokoban,
  PegSolitaire,
  Slitherlink,
  Sudoku,
  Crossword,
  Klondike,
  MahjongSolitaire,
  FallingBlocks,
  ConnectFour,
  WordSearch,
  EpubBrowser,
  EpubReading,
};

enum class MenuPage : uint8_t {
  First,
  Second,
  Third,
};

enum class GameId : uint8_t {
  LightsOut,
  Game2048,
  PipeConnect,
  Minesweeper,
  Nonogram,
  Reversi,
  DotsAndBoxes,
  Sokoban,
  PegSolitaire,
  Slitherlink,
  Sudoku,
  Crossword,
  Klondike,
  MahjongSolitaire,
  FallingBlocks,
  EpubReader,
  ConnectFour,
  WordSearch,
  Count,
};

constexpr size_t kGameCount = static_cast<size_t>(GameId::Count);
constexpr uint8_t kGameOrder[kGameCount] = {
    static_cast<uint8_t>(GameId::FallingBlocks),
    static_cast<uint8_t>(GameId::ConnectFour),
    static_cast<uint8_t>(GameId::Klondike),
    static_cast<uint8_t>(GameId::MahjongSolitaire),
    static_cast<uint8_t>(GameId::Game2048),
    static_cast<uint8_t>(GameId::EpubReader),
    static_cast<uint8_t>(GameId::Minesweeper),
    static_cast<uint8_t>(GameId::Sudoku),
    static_cast<uint8_t>(GameId::Reversi),
    static_cast<uint8_t>(GameId::WordSearch),
    static_cast<uint8_t>(GameId::Crossword),
    static_cast<uint8_t>(GameId::Sokoban),
    static_cast<uint8_t>(GameId::DotsAndBoxes),
    static_cast<uint8_t>(GameId::PegSolitaire),
    static_cast<uint8_t>(GameId::LightsOut),
    static_cast<uint8_t>(GameId::Nonogram),
    static_cast<uint8_t>(GameId::PipeConnect),
    static_cast<uint8_t>(GameId::Slitherlink),
};
static_assert(sizeof(kGameOrder) / sizeof(kGameOrder[0]) == kGameCount);
constexpr size_t kGamesPerMenuPage =
    sizeof(kMenuCardSlots) / sizeof(kMenuCardSlots[0]);
constexpr size_t kMenuPageCount =
    (kGameCount + kGamesPerMenuPage - 1) / kGamesPerMenuPage;
static_assert(kMenuPageCount == 3);
constexpr bool validGameOrder() {
  bool seen[kGameCount] = {};
  for (const uint8_t game : kGameOrder) {
    if (game >= kGameCount || seen[game]) return false;
    seen[game] = true;
  }
  return true;
}
constexpr bool gameIsOnFirstMenuPage(GameId game) {
  for (size_t position = 0; position < kGamesPerMenuPage; ++position) {
    if (kGameOrder[position] == static_cast<uint8_t>(game)) return true;
  }
  return false;
}
static_assert(validGameOrder());
static_assert(gameIsOnFirstMenuPage(GameId::EpubReader));
const char* screenName(Screen screen) {
  switch (screen) {
    case Screen::Menu:
      return "menu";
    case Screen::LightsOut:
      return "saved Lights Out";
    case Screen::Game2048:
      return "saved 2048";
    case Screen::PipeConnect:
      return "saved Pipe Connect";
    case Screen::Minesweeper:
      return "saved Minesweeper";
    case Screen::Nonogram:
      return "saved Nonogram";
    case Screen::Reversi:
      return "saved Reversi";
    case Screen::DotsAndBoxes:
      return "saved Dots and Boxes";
    case Screen::Sokoban:
      return "saved Sokoban";
    case Screen::PegSolitaire:
      return "saved Peg Solitaire";
    case Screen::Slitherlink:
      return "saved Slitherlink";
    case Screen::Sudoku:
      return "saved Sudoku";
    case Screen::Crossword:
      return "saved Crossword";
    case Screen::Klondike:
      return "saved Klondike";
    case Screen::MahjongSolitaire:
      return "saved Mahjong Solitaire";
    case Screen::FallingBlocks:
      return "saved Falling Blocks";
    case Screen::ConnectFour:
      return "saved Connect Four";
    case Screen::WordSearch:
      return "saved Word Search";
    case Screen::EpubBrowser:
      return "EPUB browser";
    case Screen::EpubReading:
      return "saved EPUB page";
  }
  return "unknown";
}

enum class ReversiMode : uint8_t {
  SinglePlayer,
  TwoPlayer,
};

struct ReaderResume {
  char browserPath[kReaderPathCapacity];
  char bookPath[kReaderPathCapacity];
  uint16_t chapter;
  uint32_t pageStart;
  uint8_t coverVisible;
  uint32_t cardSectorCount;
  uint32_t cardFingerprint;
};

struct PersistedState {
  uint32_t magic;
  uint16_t version;
  uint8_t screen;
  uint8_t menuPage;
  uint32_t flags;
  uint8_t reversiMode;
  uint8_t connectFourMode;
  LightsOutGame::Snapshot lightsOut;
  Game2048::Snapshot game2048;
  PipeConnectGame::Snapshot pipeConnect;
  MiniMinesweeperGame::Snapshot minesweeper;
  NonogramGame::Snapshot nonogram;
  ReversiGame::Snapshot reversi;
  DotsAndBoxesGame::Snapshot dotsAndBoxes;
  SokobanGame::Snapshot sokoban;
  PegSolitaireGame::Snapshot pegSolitaire;
  SlitherlinkGame::Snapshot slitherlink;
  SudokuGame::Snapshot sudoku;
  CrosswordGame::Snapshot crossword;
  KlondikeGame::Snapshot klondike;
  MahjongSolitaireGame::Snapshot mahjong;
  FallingBlocksGame::Snapshot fallingBlocks;
  ConnectFourGame::Snapshot connectFour;
  WordSearchGame::Snapshot wordSearch;
  ReaderResume reader;
  // Retained so version 18 RTC state keeps its existing layout.
  uint32_t legacyPlayCounts[kGameCount];
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

ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "OK / power", HIGH, HIGH, 0, 0},
    {board::PIN_BUTTON_1, "UP", HIGH, HIGH, 0, 0},
    {board::PIN_BUTTON_2, "DOWN", HIGH, HIGH, 0, 0},
};

struct ButtonEvent {
  ButtonState* button;
  uint32_t heldMs;
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
DotsAndBoxesGame dotsAndBoxes;
SokobanGame sokoban;
PegSolitaireGame pegSolitaire;
SlitherlinkGame slitherlink;
SudokuGame sudoku;
CrosswordGame crossword;
KlondikeGame klondike;
MahjongSolitaireGame mahjong;
FallingBlocksGame fallingBlocks;
ConnectFourGame connectFour;
WordSearchGame wordSearch;
EpubArchive epubArchive;
SdReadonlyBrowser sdBrowser;
DoubleTapTracker klondikeDoubleTap;
uint16_t sokobanCompletedLevelCount = 0;
bool sokobanProgressSaveFailed = false;
uint32_t stored2048HighScore = 0;
ReversiMode reversiMode = ReversiMode::SinglePlayer;
ReversiMode connectFourMode = ReversiMode::SinglePlayer;
Screen currentScreen = Screen::Menu;
MenuPage currentMenuPage = MenuPage::First;
bool touchReady = false;
bool touchActive = false;
bool touchActionHandled = false;
bool lightSleepReady = false;
Language currentLanguage = Language::English;
bool languageSelected = false;
bool languageSelectionVisible = false;
bool helpPaneVisible = false;
bool lightsOutSaved = false;
bool game2048Saved = false;
bool pipeConnectSaved = false;
bool minesweeperSaved = false;
bool nonogramSaved = false;
bool reversiSaved = false;
bool dotsAndBoxesSaved = false;
bool sokobanSaved = false;
bool pegSolitaireSaved = false;
bool slitherlinkSaved = false;
bool sudokuSaved = false;
bool crosswordSaved = false;
bool klondikeSaved = false;
bool mahjongSaved = false;
bool fallingBlocksSaved = false;
bool connectFourSaved = false;
bool wordSearchSaved = false;
bool sdCardReady = false;
bool readerCjkFont16Available = false;
bool readerCjkFont24Available = false;
bool readerLatinFont16Available = false;
bool readerLatinFont24Available = false;
int browserPageStart = 0;
String browserMessage;
String readerBrowserPath = "/";
String readerBookPath;
String readerFolderCoverPath;
EpubChapterText readerChapterText;
bool readerChapterRequiresNonAscii = false;
bool readerChapterRequiresCjk = false;
int readerChapterIndex = 0;
size_t readerPageStart = 0;
bool readerCoverVisible = false;
sd_card_identity::Identity readerCardIdentity;
bool crosswordKeyboardVisible = false;
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

const char* tr(TextId id) {
  return game_localization::text(currentLanguage, id);
}

const char* gameName(GameId game) {
  switch (game) {
    case GameId::LightsOut:
      return "Lights Out";
    case GameId::Game2048:
      return "2048";
    case GameId::PipeConnect:
      return "Pipe Connect";
    case GameId::Minesweeper:
      return "Minesweeper";
    case GameId::Nonogram:
      return "Nonogram";
    case GameId::Reversi:
      return "Reversi";
    case GameId::DotsAndBoxes:
      return "Dots and Boxes";
    case GameId::Sokoban:
      return "Sokoban";
    case GameId::PegSolitaire:
      return "Peg Solitaire";
    case GameId::Slitherlink:
      return "Slitherlink";
    case GameId::Sudoku:
      return "Sudoku";
    case GameId::Crossword:
      return "Crossword";
    case GameId::Klondike:
      return "Klondike";
    case GameId::MahjongSolitaire:
      return "Mahjong Solitaire";
    case GameId::FallingBlocks:
      return "Falling Blocks";
    case GameId::EpubReader:
      return "EPUB Reader";
    case GameId::ConnectFour:
      return "Connect Four";
    case GameId::WordSearch:
      return "Word Search";
    case GameId::Count:
      break;
  }
  return "unknown";
}

const char* shortEnglishGameName(GameId game) {
  switch (game) {
    case GameId::DotsAndBoxes:
      return "Dots + Boxes";
    case GameId::PegSolitaire:
      return "Peg Solo";
    case GameId::MahjongSolitaire:
      return "Mahjong";
    case GameId::FallingBlocks:
      return "Blocks";
    default:
      return gameName(game);
  }
}

TextId gameTextId(GameId game) {
  switch (game) {
    case GameId::LightsOut:
      return TextId::LightsOut;
    case GameId::Game2048:
      return TextId::Game2048;
    case GameId::PipeConnect:
      return TextId::PipeConnect;
    case GameId::Minesweeper:
      return TextId::Minesweeper;
    case GameId::Nonogram:
      return TextId::Nonogram;
    case GameId::Reversi:
      return TextId::Reversi;
    case GameId::DotsAndBoxes:
      return TextId::DotsAndBoxes;
    case GameId::Sokoban:
      return TextId::Sokoban;
    case GameId::PegSolitaire:
      return TextId::PegSolitaire;
    case GameId::Slitherlink:
      return TextId::Slitherlink;
    case GameId::Sudoku:
      return TextId::Sudoku;
    case GameId::Crossword:
      return TextId::Crossword;
    case GameId::Klondike:
      return TextId::Klondike;
    case GameId::MahjongSolitaire:
      return TextId::MahjongSolitaire;
    case GameId::FallingBlocks:
      return TextId::FallingBlocks;
    case GameId::EpubReader:
      return TextId::EpubReader;
    case GameId::ConnectFour:
      return TextId::ConnectFour;
    case GameId::WordSearch:
      return TextId::WordSearch;
    case GameId::Count:
      break;
  }
  return TextId::LightsOut;
}

GameId gameForScreen(Screen screen) {
  switch (screen) {
    case Screen::LightsOut:
      return GameId::LightsOut;
    case Screen::Game2048:
      return GameId::Game2048;
    case Screen::PipeConnect:
      return GameId::PipeConnect;
    case Screen::Minesweeper:
      return GameId::Minesweeper;
    case Screen::Nonogram:
      return GameId::Nonogram;
    case Screen::Reversi:
      return GameId::Reversi;
    case Screen::DotsAndBoxes:
      return GameId::DotsAndBoxes;
    case Screen::Sokoban:
      return GameId::Sokoban;
    case Screen::PegSolitaire:
      return GameId::PegSolitaire;
    case Screen::Slitherlink:
      return GameId::Slitherlink;
    case Screen::Sudoku:
      return GameId::Sudoku;
    case Screen::Crossword:
      return GameId::Crossword;
    case Screen::Klondike:
      return GameId::Klondike;
    case Screen::MahjongSolitaire:
      return GameId::MahjongSolitaire;
    case Screen::FallingBlocks:
      return GameId::FallingBlocks;
    case Screen::ConnectFour:
      return GameId::ConnectFour;
    case Screen::WordSearch:
      return GameId::WordSearch;
    case Screen::EpubBrowser:
    case Screen::EpubReading:
      return GameId::EpubReader;
    case Screen::Menu:
      return GameId::Count;
  }
  return GameId::Count;
}

GameId orderedGameAt(size_t position) {
  return static_cast<GameId>(kGameOrder[position]);
}

MenuPage menuPageForGame(GameId game) {
  for (size_t position = 0; position < kGameCount; ++position) {
    if (orderedGameAt(position) == game) {
      return static_cast<MenuPage>(position / kGamesPerMenuPage);
    }
  }
  return MenuPage::First;
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

bool copyReaderPath(char* destination, size_t capacity, const String& path) {
  if (path.length() >= capacity) {
    destination[0] = '\0';
    return false;
  }
  memcpy(destination, path.c_str(), path.length() + 1);
  return true;
}

void saveResumeState() {
  PersistedState state = {};
  state.magic = kPersistedStateMagic;
  state.version = kPersistedStateVersion;
  state.screen = static_cast<uint8_t>(currentScreen);
  state.menuPage = static_cast<uint8_t>(currentMenuPage);
  state.reversiMode = static_cast<uint8_t>(reversiMode);
  state.connectFourMode = static_cast<uint8_t>(connectFourMode);
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
  if (dotsAndBoxesSaved) {
    state.flags |= kDotsAndBoxesSavedFlag;
    state.dotsAndBoxes = dotsAndBoxes.snapshot();
  }
  if (sokobanSaved) {
    state.flags |= kSokobanSavedFlag;
    state.sokoban = sokoban.snapshot();
  }
  if (pegSolitaireSaved) {
    state.flags |= kPegSolitaireSavedFlag;
    state.pegSolitaire = pegSolitaire.snapshot();
  }
  if (slitherlinkSaved) {
    state.flags |= kSlitherlinkSavedFlag;
    state.slitherlink = slitherlink.snapshot();
  }
  if (sudokuSaved) {
    state.flags |= kSudokuSavedFlag;
    state.sudoku = sudoku.snapshot();
  }
  if (crosswordSaved) {
    state.flags |= kCrosswordSavedFlag;
    state.crossword = crossword.snapshot();
  }
  if (klondikeSaved) {
    state.flags |= kKlondikeSavedFlag;
    state.klondike = klondike.snapshot();
  }
  if (mahjongSaved) {
    state.flags |= kMahjongSavedFlag;
    state.mahjong = mahjong.snapshot();
  }
  if (fallingBlocksSaved) {
    state.flags |= kFallingBlocksSavedFlag;
    state.fallingBlocks = fallingBlocks.snapshot();
  }
  if (connectFourSaved) {
    state.flags |= kConnectFourSavedFlag;
    state.connectFour = connectFour.snapshot();
  }
  if (wordSearchSaved) {
    state.flags |= kWordSearchSavedFlag;
    state.wordSearch = wordSearch.snapshot();
  }
  const bool savedBrowserPath =
      copyReaderPath(state.reader.browserPath, sizeof(state.reader.browserPath),
                     readerBrowserPath);
  const bool savedBookPath =
      copyReaderPath(state.reader.bookPath, sizeof(state.reader.bookPath),
                     readerBookPath);
  state.reader.chapter = static_cast<uint16_t>(readerChapterIndex);
  state.reader.pageStart = static_cast<uint32_t>(readerPageStart);
  state.reader.coverVisible = readerCoverVisible ? 1 : 0;
  if (readerCardIdentity.valid()) {
    state.reader.cardSectorCount = readerCardIdentity.sectorCount;
    state.reader.cardFingerprint = readerCardIdentity.fingerprint;
  }
  if ((currentScreen == Screen::EpubBrowser && !savedBrowserPath) ||
      (currentScreen == Screen::EpubReading && !savedBookPath)) {
    state.screen = static_cast<uint8_t>(Screen::EpubBrowser);
    state.reader.browserPath[0] = '/';
    state.reader.browserPath[1] = '\0';
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
      state.screen > static_cast<uint8_t>(Screen::EpubReading) ||
      state.menuPage > static_cast<uint8_t>(MenuPage::Third) ||
      state.reversiMode > static_cast<uint8_t>(ReversiMode::TwoPlayer) ||
      state.connectFourMode > static_cast<uint8_t>(ReversiMode::TwoPlayer) ||
      (state.flags & ~(kLightsOutSavedFlag | k2048SavedFlag |
                       kPipeConnectSavedFlag | kMinesweeperSavedFlag |
                       kNonogramSavedFlag | kReversiSavedFlag |
                       kDotsAndBoxesSavedFlag | kSokobanSavedFlag |
                       kPegSolitaireSavedFlag | kSlitherlinkSavedFlag |
                       kSudokuSavedFlag | kCrosswordSavedFlag |
                       kKlondikeSavedFlag | kMahjongSavedFlag |
                       kFallingBlocksSavedFlag | kConnectFourSavedFlag |
                       kWordSearchSavedFlag)) != 0 ||
      memchr(state.reader.browserPath, '\0',
             sizeof(state.reader.browserPath)) == nullptr ||
      memchr(state.reader.bookPath, '\0', sizeof(state.reader.bookPath)) ==
          nullptr ||
      state.reader.chapter >= EpubArchive::kMaximumSpineItems ||
      state.reader.pageStart > EpubArchive::kMaximumChapterBytes) {
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
  const bool hasDotsAndBoxesSave =
      (state.flags & kDotsAndBoxesSavedFlag) != 0;
  if (hasDotsAndBoxesSave &&
      !dotsAndBoxes.restore(state.dotsAndBoxes)) {
    LOG.println("[games] saved Dots and Boxes state is invalid");
    return false;
  }
  if (savedScreen == Screen::DotsAndBoxes && !hasDotsAndBoxesSave) {
    LOG.println("[games] saved screen has no Dots and Boxes game");
    return false;
  }
  const bool hasSokobanSave = (state.flags & kSokobanSavedFlag) != 0;
  if (hasSokobanSave && !sokoban.restore(state.sokoban)) {
    LOG.println("[games] saved Sokoban state is invalid");
    return false;
  }
  if (savedScreen == Screen::Sokoban && !hasSokobanSave) {
    LOG.println("[games] saved screen has no Sokoban game");
    return false;
  }
  const bool hasPegSolitaireSave =
      (state.flags & kPegSolitaireSavedFlag) != 0;
  if (hasPegSolitaireSave &&
      !pegSolitaire.restore(state.pegSolitaire)) {
    LOG.println("[games] saved Peg Solitaire state is invalid");
    return false;
  }
  if (savedScreen == Screen::PegSolitaire && !hasPegSolitaireSave) {
    LOG.println("[games] saved screen has no Peg Solitaire game");
    return false;
  }
  const bool hasSlitherlinkSave =
      (state.flags & kSlitherlinkSavedFlag) != 0;
  if (hasSlitherlinkSave &&
      !slitherlink.restore(state.slitherlink)) {
    LOG.println("[games] saved Slitherlink state is invalid");
    return false;
  }
  if (savedScreen == Screen::Slitherlink && !hasSlitherlinkSave) {
    LOG.println("[games] saved screen has no Slitherlink game");
    return false;
  }
  const bool hasSudokuSave = (state.flags & kSudokuSavedFlag) != 0;
  if (hasSudokuSave && !sudoku.restore(state.sudoku)) {
    LOG.println("[games] saved Sudoku state is invalid");
    return false;
  }
  if (savedScreen == Screen::Sudoku && !hasSudokuSave) {
    LOG.println("[games] saved screen has no Sudoku game");
    return false;
  }
  const bool hasCrosswordSave = (state.flags & kCrosswordSavedFlag) != 0;
  if (hasCrosswordSave && !crossword.restore(state.crossword)) {
    LOG.println("[games] saved Crossword state is invalid");
    return false;
  }
  if (savedScreen == Screen::Crossword && !hasCrosswordSave) {
    LOG.println("[games] saved screen has no Crossword game");
    return false;
  }
  const bool hasKlondikeSave = (state.flags & kKlondikeSavedFlag) != 0;
  if (hasKlondikeSave && !klondike.restore(state.klondike)) {
    LOG.println("[games] saved Klondike state is invalid");
    return false;
  }
  if (savedScreen == Screen::Klondike && !hasKlondikeSave) {
    LOG.println("[games] saved screen has no Klondike game");
    return false;
  }
  const bool hasMahjongSave = (state.flags & kMahjongSavedFlag) != 0;
  if (hasMahjongSave && !mahjong.restore(state.mahjong)) {
    LOG.println("[games] saved Mahjong Solitaire state is invalid");
    return false;
  }
  if (savedScreen == Screen::MahjongSolitaire && !hasMahjongSave) {
    LOG.println("[games] saved screen has no Mahjong Solitaire game");
    return false;
  }
  const bool hasFallingBlocksSave =
      (state.flags & kFallingBlocksSavedFlag) != 0;
  if (hasFallingBlocksSave &&
      !fallingBlocks.restore(state.fallingBlocks)) {
    LOG.println("[games] saved Falling Blocks state is invalid");
    return false;
  }
  if (savedScreen == Screen::FallingBlocks && !hasFallingBlocksSave) {
    LOG.println("[games] saved screen has no Falling Blocks game");
    return false;
  }
  const bool hasConnectFourSave =
      (state.flags & kConnectFourSavedFlag) != 0;
  if (hasConnectFourSave && !connectFour.restore(state.connectFour)) {
    LOG.println("[games] saved Connect Four state is invalid");
    return false;
  }
  if (savedScreen == Screen::ConnectFour && !hasConnectFourSave) {
    LOG.println("[games] saved screen has no Connect Four game");
    return false;
  }
  const bool hasWordSearchSave = (state.flags & kWordSearchSavedFlag) != 0;
  if (hasWordSearchSave && !wordSearch.restore(state.wordSearch)) {
    LOG.println("[games] saved Word Search state is invalid");
    return false;
  }
  if (savedScreen == Screen::WordSearch && !hasWordSearchSave) {
    LOG.println("[games] saved screen has no Word Search game");
    return false;
  }
  if ((savedScreen == Screen::EpubBrowser ||
       savedScreen == Screen::EpubReading) &&
      state.reader.browserPath[0] != '/') {
    LOG.println("[games] saved EPUB browser path is invalid");
    return false;
  }
  if (savedScreen == Screen::EpubReading &&
      state.reader.bookPath[0] != '/') {
    LOG.println("[games] saved EPUB book path is invalid");
    return false;
  }
  if (state.reader.coverVisible > 1) {
    LOG.println("[games] saved EPUB cover state is invalid");
    return false;
  }
  lightsOutSaved = hasLightsOutSave;
  game2048Saved = has2048Save;
  pipeConnectSaved = hasPipeConnectSave;
  minesweeperSaved = hasMinesweeperSave;
  nonogramSaved = hasNonogramSave;
  reversiSaved = hasReversiSave;
  dotsAndBoxesSaved = hasDotsAndBoxesSave;
  sokobanSaved = hasSokobanSave;
  pegSolitaireSaved = hasPegSolitaireSave;
  slitherlinkSaved = hasSlitherlinkSave;
  sudokuSaved = hasSudokuSave;
  crosswordSaved = hasCrosswordSave;
  klondikeSaved = hasKlondikeSave;
  mahjongSaved = hasMahjongSave;
  fallingBlocksSaved = hasFallingBlocksSave;
  connectFourSaved = hasConnectFourSave;
  wordSearchSaved = hasWordSearchSave;
  readerBrowserPath =
      state.reader.browserPath[0] == '\0' ? "/" : state.reader.browserPath;
  readerBookPath = state.reader.bookPath;
  readerChapterIndex = state.reader.chapter;
  readerPageStart = state.reader.pageStart;
  readerCoverVisible = state.reader.coverVisible != 0;
  readerCardIdentity = {
      state.reader.cardSectorCount,
      state.reader.cardFingerprint,
  };
  crosswordKeyboardVisible = false;
  reversiMode = static_cast<ReversiMode>(state.reversiMode);
  connectFourMode = static_cast<ReversiMode>(state.connectFourMode);
  currentMenuPage = static_cast<MenuPage>(state.menuPage);
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
  const esp_err_t thresholdResult = uart_set_wakeup_threshold(UART_NUM_1, 3);
  const esp_err_t captureWakeResult =
      thresholdResult == ESP_OK ? esp_sleep_enable_uart_wakeup(UART_NUM_1)
                                : thresholdResult;
  if (captureWakeResult != ESP_OK) {
    LOG.printf("[games] USB serial wake unavailable: %s\n",
               esp_err_to_name(captureWakeResult));
  }
#endif
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
    const char* wakeName = "other";
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      wakeName = "gpio";
    } else if (cause == ESP_SLEEP_WAKEUP_UART) {
      wakeName = "uart";
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
      wakeName = "timer";
    }
    LOG.printf("[games] exited light sleep (wake=%s)\n", wakeName);
#if USB_SCREEN_CAPTURE_ENABLED
    if (cause == ESP_SLEEP_WAKEUP_UART) {
      // The bytes which trigger ESP32-S3 UART wake are not retained. Stay
      // awake briefly so the host's repeated full command can be received.
      usbScreenCapture.serveFor(epaper, kScreenWidth, kScreenHeight, 300);
    }
#endif
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

bool containsNonAscii(const String& text) {
  for (size_t index = 0; index < text.length(); ++index) {
    if (static_cast<uint8_t>(text[index]) >= 0x80) return true;
  }
  return false;
}

bool containsNonAscii(const char* text, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (static_cast<uint8_t>(text[index]) >= 0x80) return true;
  }
  return false;
}

const uint8_t* smoothFontFor(int pixelSize) {
  if (pixelSize >= 32) return game_ui_fonts::kGameUiFont32;
  if (pixelSize >= 24) return game_ui_fonts::kGameUiFont24;
  return game_ui_fonts::kGameUiFont16;
}

void drawCenteredText(const String& text, int x, int y, int font,
                      uint16_t foreground, uint16_t background, int maxWidth,
                      bool forceSmoothFont = false) {
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(foreground, background, true);
  if (!forceSmoothFont && !containsNonAscii(text)) {
    int selectedFont = font == 6 ? 4 : font;
    if (selectedFont == 4 && epaper.textWidth(text, selectedFont) > maxWidth) {
      selectedFont = 2;
    }
    epaper.drawString(text, x, y, selectedFont);
    return;
  }

  int pixelSize = font >= 6 ? 32 : font >= 4 ? 24 : 16;
  epaper.loadFont(smoothFontFor(pixelSize));
  if (pixelSize > 16 && epaper.textWidth(text) > maxWidth) {
    epaper.unloadFont();
    pixelSize = pixelSize > 24 ? 24 : 16;
    epaper.loadFont(smoothFontFor(pixelSize));
    if (pixelSize > 16 && epaper.textWidth(text) > maxWidth) {
      epaper.unloadFont();
      epaper.loadFont(smoothFontFor(16));
    }
  }
  epaper.drawString(text, x, y);
  epaper.unloadFont();
}

void drawCentered(const String& text, int x, int y, int font) {
  drawCenteredText(text, x, y, font, TFT_BLACK, TFT_WHITE,
                   kScreenWidth - 16);
}

void drawStickyArcadeBrand(int centerY, int font) {
  drawCenteredText(kBrandName, kScreenWidth / 2, centerY, font, TFT_BLACK,
                   TFT_WHITE, kScreenWidth - 32, true);
}

void drawCenteredNumber(uint32_t value, int x, int y, int font,
                        uint16_t foreground, uint16_t background) {
  const int opticalYOffset = font == 6 ? 6 : font == 4 ? 3 : 0;
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(foreground, background, true);
  epaper.drawNumber(static_cast<long>(value), x, y + opticalYOffset, font);
}

void drawArcadeLogo(int centerX, int centerY, int width) {
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
  const bool smoothFont = languageSelectionVisible ||
                          currentLanguage != Language::English;
  epaper.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 8, TFT_BLACK);
  drawCenteredText(label, rect.x + rect.width / 2,
                   rect.y + rect.height / 2 + (smoothFont ? -2 : 3), 4,
                   TFT_WHITE, TFT_BLACK, rect.width - 12, smoothFont);
}

void drawMenuCardFrame(const Rect& card) {
  epaper.fillRoundRect(card.x, card.y, card.width, kMenuPreviewSize, 14,
                      TFT_WHITE);
  epaper.drawRoundRect(card.x, card.y, card.width, kMenuPreviewSize, 14,
                      TFT_BLACK);
}

Rect& menuCardFor(GameId game) {
  switch (game) {
    case GameId::LightsOut:
      return kLightsOutMenuCard;
    case GameId::Game2048:
      return k2048MenuCard;
    case GameId::PipeConnect:
      return kPipeConnectMenuCard;
    case GameId::Minesweeper:
      return kMinesweeperMenuCard;
    case GameId::Nonogram:
      return kNonogramMenuCard;
    case GameId::Reversi:
      return kReversiMenuCard;
    case GameId::DotsAndBoxes:
      return kDotsAndBoxesMenuCard;
    case GameId::Sokoban:
      return kSokobanMenuCard;
    case GameId::PegSolitaire:
      return kPegSolitaireMenuCard;
    case GameId::Slitherlink:
      return kSlitherlinkMenuCard;
    case GameId::Sudoku:
      return kSudokuMenuCard;
    case GameId::Crossword:
      return kCrosswordMenuCard;
    case GameId::Klondike:
      return kKlondikeMenuCard;
    case GameId::MahjongSolitaire:
      return kMahjongMenuCard;
    case GameId::FallingBlocks:
      return kFallingBlocksMenuCard;
    case GameId::EpubReader:
      return kEpubReaderMenuCard;
    case GameId::ConnectFour:
      return kConnectFourMenuCard;
    case GameId::WordSearch:
      return kWordSearchMenuCard;
    case GameId::Count:
      break;
  }
  return kLightsOutMenuCard;
}

const char* klondikeRankLabel(uint8_t card) {
  static constexpr const char* kLabels[KlondikeGame::kRankCount] = {
      "A", "2", "3", "4",  "5", "6", "7",
      "8", "9", "10", "J", "Q", "K",
  };
  const int rank = KlondikeGame::rank(card);
  return rank >= 0 ? kLabels[rank] : "";
}

void drawKlondikeSuit(int suit, int centerX, int centerY, int size,
                       uint16_t color) {
  const int radius = std::max(2, size / 4);
  if (suit == 0) {
    epaper.fillCircle(centerX, centerY - radius, radius, color);
    epaper.fillCircle(centerX - radius, centerY + radius / 2, radius, color);
    epaper.fillCircle(centerX + radius, centerY + radius / 2, radius, color);
    epaper.fillRect(centerX - std::max(1, radius / 3), centerY,
                   std::max(2, radius * 2 / 3), radius * 2, color);
  } else if (suit == 1) {
    epaper.fillTriangle(centerX, centerY - size / 2, centerX - size / 3,
                       centerY, centerX + size / 3, centerY, color);
    epaper.fillTriangle(centerX, centerY + size / 2, centerX - size / 3,
                       centerY, centerX + size / 3, centerY, color);
  } else if (suit == 2) {
    epaper.fillCircle(centerX - radius, centerY - radius / 2, radius, color);
    epaper.fillCircle(centerX + radius, centerY - radius / 2, radius, color);
    epaper.fillTriangle(centerX - radius * 2, centerY - radius / 2,
                       centerX + radius * 2, centerY - radius / 2, centerX,
                       centerY + size / 2, color);
  } else {
    epaper.fillTriangle(centerX, centerY - size / 2, centerX - size / 2,
                       centerY + radius, centerX + size / 2,
                       centerY + radius, color);
    epaper.fillCircle(centerX - radius, centerY + radius / 2, radius, color);
    epaper.fillCircle(centerX + radius, centerY + radius / 2, radius, color);
    epaper.fillRect(centerX - std::max(1, radius / 3), centerY,
                   std::max(2, radius * 2 / 3), radius * 2, color);
  }
}

void drawKlondikeCard(int x, int y, int width, int height, uint8_t card,
                      bool faceUp, bool selected = false) {
  epaper.fillRoundRect(x, y, width, height, 5,
                      faceUp ? TFT_WHITE : TFT_BLACK);
  epaper.drawRoundRect(x, y, width, height, 5, TFT_BLACK);
  if (!faceUp) {
    epaper.drawRoundRect(x + 4, y + 4, width - 8, height - 8, 4, TFT_WHITE);
    for (int offset = 9; offset < width - 5; offset += 10) {
      epaper.drawLine(x + offset, y + 6, x + 5, y + offset + 1, TFT_WHITE);
    }
    return;
  }

  drawCentered(klondikeRankLabel(card), x + width / 2,
               y + std::max(13, height / 5), width < 50 ? 2 : 4);
  drawKlondikeSuit(KlondikeGame::cardSuit(card), x + width / 2,
                    y + height * 3 / 5, std::max(12, width / 3), TFT_BLACK);
  if (selected) {
    epaper.drawRoundRect(x + 2, y + 2, width - 4, height - 4, 4, TFT_BLACK);
    epaper.drawRoundRect(x + 4, y + 4, width - 8, height - 8, 3, TFT_BLACK);
  }
}

void drawMahjongSymbol(int suit, int centerX, int centerY, int size,
                       uint16_t color) {
  if (suit == 0) {
    epaper.drawCircle(centerX, centerY, size / 3, color);
    epaper.fillCircle(centerX, centerY, std::max(1, size / 9), color);
  } else if (suit == 1) {
    const int gap = std::max(2, size / 5);
    for (int offset = -gap; offset <= gap; offset += gap) {
      epaper.fillRect(centerX + offset - 1, centerY - size / 3, 3,
                     size * 2 / 3, color);
    }
  } else if (suit == 2) {
    epaper.drawRect(centerX - size / 3, centerY - size / 3, size * 2 / 3,
                   size * 2 / 3, color);
    epaper.fillRect(centerX - 2, centerY - size / 4, 4, size / 2, color);
  } else {
    epaper.fillTriangle(centerX, centerY - size / 3, centerX - size / 3,
                       centerY + size / 3, centerX + size / 3,
                       centerY + size / 3, color);
  }
}

void drawMahjongTile(int x, int y, int width, int height, uint8_t suit,
                     uint8_t rank, bool selected = false) {
  epaper.fillRoundRect(x, y, width, height, 3, TFT_WHITE);
  epaper.drawRoundRect(x, y, width, height, 3, TFT_BLACK);
  if (selected) {
    epaper.drawRoundRect(x + 2, y + 2, width - 4, height - 4, 2, TFT_BLACK);
    epaper.drawRoundRect(x + 4, y + 4, width - 8, height - 8, 2, TFT_BLACK);
  }
  drawMahjongSymbol(suit, x + width / 2, y + height / 3,
                    std::max(10, width / 2), TFT_BLACK);
  drawCenteredNumber(rank, x + width / 2, y + height * 3 / 4, 2, TFT_BLACK,
                     TFT_WHITE);
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
                      kLightsOutMenuCard.width, kMenuPreviewSize, 14,
                      TFT_WHITE);
  epaper.drawRoundRect(kLightsOutMenuCard.x, kLightsOutMenuCard.y,
                      kLightsOutMenuCard.width, kMenuPreviewSize, 14,
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
                      kMenuPreviewSize, 14, TFT_WHITE);
  epaper.drawRoundRect(k2048MenuCard.x, k2048MenuCard.y, k2048MenuCard.width,
                      kMenuPreviewSize, 14, TFT_BLACK);

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
                       kPipeConnectMenuCard.width, kMenuPreviewSize, 14,
                       TFT_WHITE);
  epaper.drawRoundRect(kPipeConnectMenuCard.x, kPipeConnectMenuCard.y,
                       kPipeConnectMenuCard.width, kMenuPreviewSize, 14,
                       TFT_BLACK);

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
                       kMinesweeperMenuCard.width, kMenuPreviewSize, 14,
                       TFT_WHITE);
  epaper.drawRoundRect(kMinesweeperMenuCard.x, kMinesweeperMenuCard.y,
                       kMinesweeperMenuCard.width, kMenuPreviewSize, 14,
                       TFT_BLACK);

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
                       kNonogramMenuCard.width, kMenuPreviewSize, 14,
                       TFT_WHITE);
  epaper.drawRoundRect(kNonogramMenuCard.x, kNonogramMenuCard.y,
                       kNonogramMenuCard.width, kMenuPreviewSize, 14,
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
                       kReversiMenuCard.width, kMenuPreviewSize, 14,
                       TFT_WHITE);
  epaper.drawRoundRect(kReversiMenuCard.x, kReversiMenuCard.y,
                       kReversiMenuCard.width, kMenuPreviewSize, 14,
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

void drawDotsAndBoxesMenuCard() {
  drawMenuCardFrame(kDotsAndBoxesMenuCard);
  constexpr int kSpacing = 44;
  constexpr int kDotRadius = 5;
  const int left = kDotsAndBoxesMenuCard.x + 29;
  const int top = kDotsAndBoxesMenuCard.y + 29;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int x = left + column * kSpacing;
      const int y = top + row * kSpacing;
      if (column < 3 && (row + column) % 3 != 1) {
        epaper.fillRect(x, y - 2, kSpacing, 5, TFT_BLACK);
      }
      if (row < 3 && (row * 2 + column) % 4 != 1) {
        epaper.fillRect(x - 2, y, 5, kSpacing, TFT_BLACK);
      }
    }
  }
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      epaper.fillCircle(left + column * kSpacing, top + row * kSpacing,
                        kDotRadius, TFT_BLACK);
    }
  }
}

void drawSokobanMenuCard() {
  drawMenuCardFrame(kSokobanMenuCard);
  constexpr int kSize = 44;
  const int left = kSokobanMenuCard.x + 29;
  const int top = kSokobanMenuCard.y + 29;
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      const int x = left + column * kSize;
      const int y = top + row * kSize;
      epaper.drawRect(x, y, kSize, kSize, TFT_BLACK);
      if ((row == 0 && column != 1) || (row == 2 && column == 2)) {
        epaper.fillRect(x + 4, y + 4, kSize - 8, kSize - 8, TFT_BLACK);
      }
    }
  }
  const int boxX = left + kSize + 7;
  const int boxY = top + kSize + 7;
  epaper.drawRect(boxX, boxY, kSize - 14, kSize - 14, TFT_BLACK);
  epaper.drawLine(boxX, boxY, boxX + kSize - 15, boxY + kSize - 15,
                  TFT_BLACK);
  epaper.drawLine(boxX + kSize - 15, boxY, boxX, boxY + kSize - 15,
                  TFT_BLACK);
  epaper.fillCircle(left + kSize / 2, top + kSize * 5 / 2, 11, TFT_BLACK);
  epaper.drawCircle(left + kSize * 5 / 2, top + kSize * 3 / 2, 12,
                    TFT_BLACK);
}

void drawPegSolitaireMenuCard() {
  drawMenuCardFrame(kPegSolitaireMenuCard);
  constexpr int kSpacing = 29;
  constexpr int kRadius = 9;
  const int left = kPegSolitaireMenuCard.x + 37;
  const int top = kPegSolitaireMenuCard.y + 37;
  for (int row = 0; row < 5; ++row) {
    for (int column = 0; column < 5; ++column) {
      if ((row < 2 || row > 2) && (column < 2 || column > 2)) continue;
      const int x = left + column * kSpacing;
      const int y = top + row * kSpacing;
      if (row == 2 && column == 2) {
        epaper.drawCircle(x, y, kRadius, TFT_BLACK);
      } else {
        epaper.fillCircle(x, y, kRadius, TFT_BLACK);
      }
    }
  }
}

void drawSlitherlinkMenuCard() {
  drawMenuCardFrame(kSlitherlinkMenuCard);
  constexpr int kSpacing = 42;
  constexpr int kDotRadius = 4;
  const int left = kSlitherlinkMenuCard.x + 32;
  const int top = kSlitherlinkMenuCard.y + 32;
  epaper.fillRect(left, top - 2, kSpacing * 2, 5, TFT_BLACK);
  epaper.fillRect(left + kSpacing * 2 - 2, top, 5, kSpacing * 2, TFT_BLACK);
  epaper.fillRect(left + kSpacing, top + kSpacing * 3 - 2, kSpacing * 2, 5,
                  TFT_BLACK);
  epaper.fillRect(left - 2, top + kSpacing, 5, kSpacing * 2, TFT_BLACK);
  drawCenteredNumber(2, left + kSpacing / 2, top + kSpacing / 2, 4,
                     TFT_BLACK, TFT_WHITE);
  drawCenteredNumber(3, left + kSpacing * 3 / 2,
                     top + kSpacing * 3 / 2, 4, TFT_BLACK, TFT_WHITE);
  drawCenteredNumber(1, left + kSpacing * 5 / 2,
                     top + kSpacing * 5 / 2, 4, TFT_BLACK, TFT_WHITE);
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      epaper.fillCircle(left + column * kSpacing, top + row * kSpacing,
                        kDotRadius, TFT_BLACK);
    }
  }
}

void drawSudokuMenuCard() {
  drawMenuCardFrame(kSudokuMenuCard);
  constexpr int kPreviewSize = 36;
  const int left = kSudokuMenuCard.x + 23;
  const int top = kSudokuMenuCard.y + 23;
  constexpr uint8_t values[4][4] = {
      {5, 0, 3, 0},
      {0, 2, 0, 4},
      {3, 0, 4, 0},
      {0, 1, 0, 2},
  };
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int x = left + column * kPreviewSize;
      const int y = top + row * kPreviewSize;
      epaper.drawRect(x, y, kPreviewSize, kPreviewSize, TFT_BLACK);
      if (values[row][column] != 0) {
        drawCenteredNumber(values[row][column], x + kPreviewSize / 2,
                           y + kPreviewSize / 2, 4, TFT_BLACK, TFT_WHITE);
      }
    }
  }
  epaper.drawRect(left, top, kPreviewSize * 4, kPreviewSize * 4, TFT_BLACK);
  epaper.drawRect(left + 1, top + 1, kPreviewSize * 4 - 2,
                  kPreviewSize * 4 - 2, TFT_BLACK);
}

void drawCrosswordMenuCard() {
  drawMenuCardFrame(kCrosswordMenuCard);
  constexpr int kPreviewSize = 29;
  const int left = kCrosswordMenuCard.x + 23;
  const int top = kCrosswordMenuCard.y + 23;
  constexpr char cells[5][6] = {
      "SWEEP",
      "A#N#A",
      "WATER",
      "##EAT",
      "BERRY",
  };
  for (int row = 0; row < 5; ++row) {
    for (int column = 0; column < 5; ++column) {
      const int x = left + column * kPreviewSize;
      const int y = top + row * kPreviewSize;
      if (cells[row][column] == '#') {
        epaper.fillRect(x, y, kPreviewSize, kPreviewSize, TFT_BLACK);
      } else {
        epaper.drawRect(x, y, kPreviewSize, kPreviewSize, TFT_BLACK);
        drawCentered(String(cells[row][column]), x + kPreviewSize / 2,
                     y + kPreviewSize / 2, 2);
      }
    }
  }
}

void drawKlondikeMenuCard() {
  drawMenuCardFrame(kKlondikeMenuCard);
  const int left = kKlondikeMenuCard.x + 24;
  const int top = kKlondikeMenuCard.y + 43;
  drawKlondikeCard(left, top, 52, 92, 0, true);
  drawKlondikeCard(left + 45, top + 12, 52, 92, 25, true);
  drawKlondikeCard(left + 90, top + 24, 52, 92, 37, true);
}

void drawMahjongMenuCard() {
  drawMenuCardFrame(kMahjongMenuCard);
  constexpr int kWidth = 58;
  constexpr int kHeight = 66;
  const int left = kMahjongMenuCard.x + 27;
  const int top = kMahjongMenuCard.y + 23;
  drawMahjongTile(left, top, kWidth, kHeight, 0, 1);
  drawMahjongTile(left + 74, top, kWidth, kHeight, 1, 8);
  drawMahjongTile(left, top + 78, kWidth, kHeight, 2, 5);
  drawMahjongTile(left + 74, top + 78, kWidth, kHeight, 3, 3);
}

void drawFallingBlock(int x, int y, int size, uint8_t value) {
  epaper.fillRect(x, y, size, size, TFT_WHITE);
  epaper.drawRect(x, y, size, size, TFT_BLACK);
  if (value == 0) return;
  epaper.fillRect(x + 3, y + 3, size - 6, size - 6, TFT_BLACK);
  if ((value & 1U) == 0) {
    epaper.drawRect(x + 6, y + 6, size - 12, size - 12, TFT_WHITE);
  }
}

void drawFallingBlocksMenuCard() {
  drawMenuCardFrame(kFallingBlocksMenuCard);
  constexpr int kPreviewCell = 20;
  constexpr int kPreviewColumns = 8;
  constexpr int kPreviewRows = 7;
  const int left = kFallingBlocksMenuCard.x +
                   (kFallingBlocksMenuCard.width -
                    kPreviewColumns * kPreviewCell) /
                       2;
  const int top = kFallingBlocksMenuCard.y + 24;
  for (int row = 0; row < kPreviewRows; ++row) {
    for (int column = 0; column < kPreviewColumns; ++column) {
      const bool filled =
          row >= 5 || (row == 4 && (column == 0 || column >= 5)) ||
          (row == 3 && column >= 5) ||
          (row == 2 && column >= 2 && column <= 5);
      drawFallingBlock(left + column * kPreviewCell,
                       top + row * kPreviewCell, kPreviewCell,
                       filled ? static_cast<uint8_t>((row + column) % 7 + 1)
                              : 0);
    }
  }
}

void drawEpubReaderMenuCard() {
  drawMenuCardFrame(kEpubReaderMenuCard);
  const int left = kEpubReaderMenuCard.x + 42;
  const int top = kEpubReaderMenuCard.y + 24;
  constexpr int kBookWidth = 106;
  constexpr int kBookHeight = 142;
  epaper.fillRoundRect(left, top, kBookWidth, kBookHeight, 5, TFT_BLACK);
  epaper.fillRect(left + 8, top + 7, kBookWidth - 16, kBookHeight - 14,
                  TFT_WHITE);
  epaper.fillRect(left + 16, top + 26, kBookWidth - 32, 4, TFT_BLACK);
  epaper.fillRect(left + 16, top + 42, kBookWidth - 32, 4, TFT_BLACK);
  epaper.fillRect(left + 16, top + 58, kBookWidth - 45, 4, TFT_BLACK);
  drawCentered("EPUB", left + kBookWidth / 2, top + 103, 2);
}

void drawConnectFourMenuCard() {
  drawMenuCardFrame(kConnectFourMenuCard);
  constexpr int kPreviewCell = 24;
  const int left = kConnectFourMenuCard.x + 11;
  const int top = kConnectFourMenuCard.y + 22;
  for (int row = 0; row < 6; ++row) {
    for (int column = 0; column < 7; ++column) {
      const int centerX = left + column * kPreviewCell + kPreviewCell / 2;
      const int centerY = top + row * kPreviewCell + kPreviewCell / 2;
      epaper.drawCircle(centerX, centerY, 9, TFT_BLACK);
      const bool solid =
          (row == 5 && (column == 0 || column == 2 || column == 4)) ||
          (row == 4 && (column == 1 || column == 3));
      if (solid) epaper.fillCircle(centerX, centerY, 7, TFT_BLACK);
    }
  }
}

void drawWordSearchMenuCard() {
  drawMenuCardFrame(kWordSearchMenuCard);
  static constexpr char kPreview[] = "PLAYGAMEWORDCOIN";
  constexpr int kPreviewCell = 38;
  const int left = kWordSearchMenuCard.x + 19;
  const int top = kWordSearchMenuCard.y + 17;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int x = left + column * kPreviewCell;
      const int y = top + row * kPreviewCell;
      epaper.drawRect(x, y, kPreviewCell, kPreviewCell, TFT_BLACK);
      const char label[] = {kPreview[row * 4 + column], '\0'};
      drawCentered(label, x + kPreviewCell / 2, y + kPreviewCell / 2 + 2, 2);
    }
  }
}

void arrangeMenuCards() {
  for (size_t position = 0; position < kGameCount; ++position) {
    menuCardFor(orderedGameAt(position)) =
        kMenuCardSlots[position % kGamesPerMenuPage];
  }
}

void drawGameMenuCard(GameId game) {
  switch (game) {
    case GameId::LightsOut:
      drawLightsOutMenuCard();
      break;
    case GameId::Game2048:
      draw2048MenuCard();
      break;
    case GameId::PipeConnect:
      drawPipeConnectMenuCard();
      break;
    case GameId::Minesweeper:
      drawMinesweeperMenuCard();
      break;
    case GameId::Nonogram:
      drawNonogramMenuCard();
      break;
    case GameId::Reversi:
      drawReversiMenuCard();
      break;
    case GameId::DotsAndBoxes:
      drawDotsAndBoxesMenuCard();
      break;
    case GameId::Sokoban:
      drawSokobanMenuCard();
      break;
    case GameId::PegSolitaire:
      drawPegSolitaireMenuCard();
      break;
    case GameId::Slitherlink:
      drawSlitherlinkMenuCard();
      break;
    case GameId::Sudoku:
      drawSudokuMenuCard();
      break;
    case GameId::Crossword:
      drawCrosswordMenuCard();
      break;
    case GameId::Klondike:
      drawKlondikeMenuCard();
      break;
    case GameId::MahjongSolitaire:
      drawMahjongMenuCard();
      break;
    case GameId::FallingBlocks:
      drawFallingBlocksMenuCard();
      break;
    case GameId::EpubReader:
      drawEpubReaderMenuCard();
      break;
    case GameId::ConnectFour:
      drawConnectFourMenuCard();
      break;
    case GameId::WordSearch:
      drawWordSearchMenuCard();
      break;
    case GameId::Count:
      return;
  }

  const Rect& card = menuCardFor(game);
  String label = currentLanguage == Language::English
                     ? (game == GameId::EpubReader ? "Book Reader"
                                                   : gameName(game))
                     : tr(gameTextId(game));
  if (currentLanguage == Language::English &&
      epaper.textWidth(label, 4) > card.width - 16) {
    label = shortEnglishGameName(game);
  }
  drawCenteredText(label, card.x + card.width / 2,
                   card.y + kMenuPreviewSize + 18, 4, TFT_BLACK, TFT_WHITE,
                   card.width - 16, currentLanguage != Language::English);
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

void drawArrowButton(const Rect& button, bool pointsRight) {
  epaper.fillRoundRect(button.x, button.y, button.width, button.height, 8,
                      TFT_BLACK);
  const int centerY = button.y + button.height / 2;
  const int tipX =
      pointsRight ? button.x + button.width - 9 : button.x + 9;
  const int headBaseX = pointsRight ? tipX - 13 : tipX + 13;
  const int tailX =
      pointsRight ? button.x + 9 : button.x + button.width - 9;
  epaper.fillTriangle(tipX, centerY, headBaseX, centerY - 11, headBaseX,
                     centerY + 11, TFT_WHITE);
  if (pointsRight) {
    epaper.fillRect(tailX, centerY - 3, headBaseX - tailX + 1, 7, TFT_WHITE);
  } else {
    epaper.fillRect(headBaseX - 1, centerY - 3, tailX - headBaseX + 1, 7,
                   TFT_WHITE);
  }
}

void drawBackIndicator() {
  drawArrowButton(kBackButton, false);
}

void drawHelpIndicator() {
  const int centerX = kHelpButton.x + kHelpButton.width / 2;
  const int centerY = kHelpButton.y + kHelpButton.height / 2;
  epaper.fillRect(kHelpButton.x - 2, kHelpButton.y - 2,
                  kHelpButton.width + 4, kHelpButton.height + 4, TFT_WHITE);
  if (helpPaneVisible) {
    epaper.fillCircle(centerX, centerY, 17, TFT_BLACK);
    for (int offset = -1; offset <= 1; ++offset) {
      epaper.drawLine(centerX - 7, centerY - 7 + offset, centerX + 7,
                      centerY + 7 + offset, TFT_WHITE);
      epaper.drawLine(centerX - 7, centerY + 7 + offset, centerX + 7,
                      centerY - 7 + offset, TFT_WHITE);
    }
  } else {
    epaper.drawCircle(centerX, centerY, 17, TFT_BLACK);
    epaper.drawCircle(centerX, centerY, 16, TFT_BLACK);
    drawCenteredText("?", centerX, centerY + 2, 4, TFT_BLACK, TFT_WHITE, 24);
  }
}

void drawSettingsIndicator() {
  const int centerX = kSettingsButton.x + kSettingsButton.width / 2;
  const int centerY = kSettingsButton.y + kSettingsButton.height / 2;
  constexpr int kGlyphRadius = 14;
  constexpr int kToothLength = 6;
  constexpr int kToothThickness = 5;
  const int left = centerX - kGlyphRadius;
  const int top = centerY - kGlyphRadius;
  epaper.fillRect(kSettingsButton.x - 2, kSettingsButton.y - 2,
                  kSettingsButton.width + 4, kSettingsButton.height + 4,
                  TFT_WHITE);
  epaper.fillRect(centerX - kToothThickness / 2, top, kToothThickness,
                  kToothLength, TFT_BLACK);
  epaper.fillRect(centerX - kToothThickness / 2,
                  centerY + kGlyphRadius - kToothLength, kToothThickness,
                  kToothLength, TFT_BLACK);
  epaper.fillRect(left, centerY - kToothThickness / 2, kToothLength,
                  kToothThickness, TFT_BLACK);
  epaper.fillRect(centerX + kGlyphRadius - kToothLength,
                  centerY - kToothThickness / 2, kToothLength,
                  kToothThickness, TFT_BLACK);
  epaper.fillRect(centerX - 11, centerY - 11, 5, 5, TFT_BLACK);
  epaper.fillRect(centerX + 6, centerY - 11, 5, 5, TFT_BLACK);
  epaper.fillRect(centerX - 11, centerY + 6, 5, 5, TFT_BLACK);
  epaper.fillRect(centerX + 6, centerY + 6, 5, 5, TFT_BLACK);
  epaper.fillCircle(centerX, centerY, 9, TFT_BLACK);
  epaper.fillCircle(centerX, centerY, 4, TFT_WHITE);
}

void drawGameStatusBar(const char* title) {
  drawBackIndicator();
  drawCenteredText(title, kScreenWidth / 2, 24, 4, TFT_BLACK, TFT_WHITE, 250,
                   currentLanguage != Language::English);
  drawHelpIndicator();
  drawStatusBar();
}

game_help::Topic helpTopicForScreen() {
  switch (currentScreen) {
    case Screen::LightsOut:
      return game_help::Topic::LightsOut;
    case Screen::Game2048:
      return game_help::Topic::Game2048;
    case Screen::PipeConnect:
      return game_help::Topic::PipeConnect;
    case Screen::Minesweeper:
      return game_help::Topic::Minesweeper;
    case Screen::Nonogram:
      return game_help::Topic::Nonogram;
    case Screen::Reversi:
      return game_help::Topic::Reversi;
    case Screen::DotsAndBoxes:
      return game_help::Topic::DotsAndBoxes;
    case Screen::Sokoban:
      return game_help::Topic::Sokoban;
    case Screen::PegSolitaire:
      return game_help::Topic::PegSolitaire;
    case Screen::Slitherlink:
      return game_help::Topic::Slitherlink;
    case Screen::Sudoku:
      return game_help::Topic::Sudoku;
    case Screen::Crossword:
      return game_help::Topic::Crossword;
    case Screen::Klondike:
      return game_help::Topic::Klondike;
    case Screen::MahjongSolitaire:
      return game_help::Topic::MahjongSolitaire;
    case Screen::FallingBlocks:
      return game_help::Topic::FallingBlocks;
    case Screen::ConnectFour:
      return game_help::Topic::ConnectFour;
    case Screen::WordSearch:
      return game_help::Topic::WordSearch;
    case Screen::EpubBrowser:
    case Screen::EpubReading:
      return game_help::Topic::EpubReader;
    case Screen::Menu:
      break;
  }
  return game_help::Topic::LightsOut;
}

size_t helpCharacterWidth(uint32_t codepoint, epub_text::TextStyle) {
  char encoded[5] = {};
  if (codepoint <= 0x7F) {
    encoded[0] = static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    encoded[0] = static_cast<char>(0xC0 | (codepoint >> 6));
    encoded[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    encoded[0] = static_cast<char>(0xE0 | (codepoint >> 12));
    encoded[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    encoded[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    encoded[0] = static_cast<char>(0xF0 | (codepoint >> 18));
    encoded[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    encoded[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    encoded[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
  }
  return static_cast<size_t>(epaper.textWidth(encoded));
}

void drawJustifiedHelpLine(const std::string& line, int x, int y,
                           int maximumWidth, bool justify) {
  if (!justify || line.empty()) {
    epaper.drawString(line.c_str(), x, y);
    return;
  }

  const size_t spaceCount =
      static_cast<size_t>(std::count(line.begin(), line.end(), ' '));
  if (spaceCount > 0) {
    const int spaceWidth = epaper.textWidth(" ");
    int naturalWidth = static_cast<int>(spaceCount) * spaceWidth;
    size_t runStart = 0;
    for (size_t offset = 0; offset <= line.length(); ++offset) {
      if (offset < line.length() && line[offset] != ' ') continue;
      if (offset > runStart) {
        naturalWidth += epaper.textWidth(
            String(line.substr(runStart, offset - runStart).c_str()));
      }
      runStart = offset + 1;
    }

    const int extraWidth = std::max(0, maximumWidth - naturalWidth);
    const int extraPerSpace = extraWidth / static_cast<int>(spaceCount);
    const int extraRemainder = extraWidth % static_cast<int>(spaceCount);
    size_t spaceIndex = 0;
    runStart = 0;
    for (size_t offset = 0; offset <= line.length(); ++offset) {
      if (offset < line.length() && line[offset] != ' ') continue;
      if (offset > runStart) {
        const String run(line.substr(runStart, offset - runStart).c_str());
        epaper.drawString(run, x, y);
        x += epaper.textWidth(run);
      }
      if (offset < line.length()) {
        x += spaceWidth + extraPerSpace +
             (spaceIndex < static_cast<size_t>(extraRemainder) ? 1 : 0);
        ++spaceIndex;
      }
      runStart = offset + 1;
    }
    return;
  }

  size_t glyphCount = 0;
  int naturalWidth = 0;
  for (size_t offset = 0; offset < line.length();) {
    const size_t bytes =
        epub_text::utf8CharacterBytes(line.data(), line.length(), offset);
    naturalWidth +=
        epaper.textWidth(String(line.substr(offset, bytes).c_str()));
    offset += bytes;
    ++glyphCount;
  }
  if (glyphCount < 2) {
    epaper.drawString(line.c_str(), x, y);
    return;
  }

  const size_t gapCount = glyphCount - 1;
  const int extraWidth = std::max(0, maximumWidth - naturalWidth);
  const int extraPerGap = extraWidth / static_cast<int>(gapCount);
  const int extraRemainder = extraWidth % static_cast<int>(gapCount);
  size_t glyphIndex = 0;
  for (size_t offset = 0; offset < line.length();) {
    const size_t bytes =
        epub_text::utf8CharacterBytes(line.data(), line.length(), offset);
    const String glyph(line.substr(offset, bytes).c_str());
    epaper.drawString(glyph, x, y);
    x += epaper.textWidth(glyph);
    if (glyphIndex < gapCount) {
      x += extraPerGap +
           (glyphIndex < static_cast<size_t>(extraRemainder) ? 1 : 0);
    }
    offset += bytes;
    ++glyphIndex;
  }
}

void drawHelpPane() {
  constexpr int kPaneLeft = 14;
  constexpr int kPaneTop = 64;
  constexpr int kPaneWidth = kScreenWidth - kPaneLeft * 2;
  constexpr int kPaneHeight = 672;
  constexpr int kTextLeft = 28;
  constexpr int kTextRight = kPaneLeft + kPaneWidth - 14;
  constexpr int kTextWidth = kTextRight - kTextLeft;
  constexpr int kTextTop = 126;
  constexpr int kLineHeight = 28;

  epaper.fillRect(0, kStatusDividerY + 1, kScreenWidth,
                  kScreenHeight - kStatusDividerY - 1, TFT_WHITE);
  drawHelpIndicator();
  epaper.drawRoundRect(kPaneLeft, kPaneTop, kPaneWidth, kPaneHeight, 12,
                      TFT_BLACK);
  epaper.drawRoundRect(kPaneLeft + 1, kPaneTop + 1, kPaneWidth - 2,
                      kPaneHeight - 2, 11, TFT_BLACK);
  drawCenteredText(tr(TextId::HowToPlay), kScreenWidth / 2, 92, 4, TFT_BLACK,
                   TFT_WHITE, kPaneWidth - 32,
                   currentLanguage != Language::English);

  const char* instructions =
      game_help::text(currentLanguage, helpTopicForScreen());
  const size_t instructionLength = strlen(instructions);
  epaper.loadFont(currentLanguage == Language::ChineseSimplified
                       ? game_ui_fonts::kGameHelpFont24
                       : epub_latin_fonts::kRegular);
  const epub_text::TextPage page = epub_text::paginate(
      instructions, instructionLength, 0, kTextWidth,
      game_help::kMaximumLines, epub_text::TextStyle::Regular, true,
      helpCharacterWidth);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(TL_DATUM);
  for (size_t line = 0; line < page.lines.size(); ++line) {
    const bool justify =
        (line < page.justifyLines.size() && page.justifyLines[line]) ||
        (currentLanguage == Language::ChineseSimplified &&
         line + 1 < page.lines.size());
    drawJustifiedHelpLine(
        page.lines[line], kTextLeft,
        kTextTop + static_cast<int>(line) * kLineHeight, kTextWidth, justify);
  }
  epaper.unloadFont();
  epaper.setTextFont(2);
}

void drawMenu() {
  epaper.fillSprite(TFT_WHITE);
  drawSettingsIndicator();
  drawStickyArcadeBrand(24, 4);
  arrangeMenuCards();
  const size_t pageIndex = static_cast<size_t>(currentMenuPage);
  const size_t firstPosition = pageIndex * kGamesPerMenuPage;
  const size_t visibleGames =
      std::min(kGamesPerMenuPage, kGameCount - firstPosition);
  for (size_t slot = 0; slot < visibleGames; ++slot) {
    drawGameMenuCard(orderedGameAt(firstPosition + slot));
  }
  if (pageIndex > 0) drawArrowButton(kPreviousPageButton, false);
  if (pageIndex + 1 < kMenuPageCount) {
    drawArrowButton(kNextPageButton, true);
  }
  const String pageLabel =
      String(pageIndex + 1) + " / " + String(kMenuPageCount);
  drawCentered(pageLabel, kScreenWidth / 2, 774, 4);
  drawStatusBar();
}

void drawLanguageSelection() {
  epaper.fillSprite(TFT_WHITE);
  drawStickyArcadeBrand(24, 4);
  drawCentered(tr(TextId::SelectLanguage), kScreenWidth / 2, 72, 4);
  for (size_t index = 0; index < game_localization::kLanguageCount; ++index) {
    drawButton(kLanguageButtons[index],
               game_localization::languageName(static_cast<Language>(index)));
  }
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
  drawArcadeLogo(kScreenWidth / 2, 390, 280);
  drawStickyArcadeBrand(525, 6);
  drawRepoQr();
}

void drawChargeSplash(int batteryPercent) {
  epaper.fillSprite(TFT_WHITE);
  drawCentered(tr(TextId::BatteryLow), kScreenWidth / 2, 130, 4);
  drawCentered(String(batteryPercent) + "% " + tr(TextId::Remaining),
               kScreenWidth / 2, 180, 4);
  drawArcadeLogo(kScreenWidth / 2, 390, 280);
  drawStickyArcadeBrand(500, 4);
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
    drawCentered(tr(TextId::Solved), kScreenWidth / 2, 590, 4);
    drawCentered(String(lightsOut.moves()) + " " + tr(TextId::Moves) + " - " +
                     tr(TextId::TapNew),
                 kScreenWidth / 2, 630, 4);
  } else {
    drawCentered(String(lightsOut.moves()) + " " + tr(TextId::Moves),
                 kScreenWidth / 2, 590, 4);
  }
}

void drawLightsOut() {
  epaper.fillSprite(TFT_WHITE);
  drawLightsOutBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::LightsOut));
}

void draw2048Board() {
  epaper.fillRect(k2048BoardRegion.x, k2048BoardRegion.y,
                  k2048BoardRegion.width, k2048BoardRegion.height, TFT_WHITE);
  drawCentered(String(tr(TextId::Score)) + " " + String(game2048.score()),
               135, 128, 4);
  drawCentered(String(tr(TextId::Best)) + " " + String(game2048.bestScore()),
               350, 128, 4);

  for (int row = 0; row < Game2048::kSize; ++row) {
    for (int column = 0; column < Game2048::kSize; ++column) {
      draw2048Tile(k2048GridLeft + column * k2048CellSize,
                   k2048GridTop + row * k2048CellSize, k2048CellSize,
                   game2048.at(row, column));
    }
  }

  if (game2048.gameOver()) {
    drawCentered(String(tr(TextId::NoMoves)) + " - " + tr(TextId::TapNew),
                 kScreenWidth / 2, 610, 4);
  } else if (game2048.won()) {
    drawCentered(String("2048! ") + tr(TextId::KeepGoing), kScreenWidth / 2,
                 610, 4);
  }
}

void draw2048() {
  epaper.fillSprite(TFT_WHITE);
  draw2048Board();
  drawButton(kCenteredNewButton, tr(TextId::NewGame));
  drawGameStatusBar(tr(TextId::Game2048));
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
    drawCentered(String(tr(TextId::ConnectedIn)) + " " +
                     String(pipeConnect.moves()) + " " + tr(TextId::Taps),
                 kScreenWidth / 2, 610, 4);
  } else {
    drawCentered(tr(TextId::ConnectEveryPipe), kScreenWidth / 2, 610, 4);
  }
}

void drawPipeConnect() {
  epaper.fillSprite(TFT_WHITE);
  drawPipeConnectBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::PipeConnect));
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
    drawCentered(tr(TextId::FieldCleared), kScreenWidth / 2, 610, 4);
  } else if (minesweeper.lost()) {
    drawCentered(String(tr(TextId::MineHit)) + " - " + tr(TextId::TapNew),
                 kScreenWidth / 2, 610, 4);
  } else {
    drawCentered(String("6 ") + tr(TextId::Mines), kScreenWidth / 2, 610, 4);
  }
}

void drawMinesweeper() {
  epaper.fillSprite(TFT_WHITE);
  drawMinesweeperBoard();
  drawButton(kCenteredNewButton, tr(TextId::NewGame));
  drawGameStatusBar(tr(TextId::Minesweeper));
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
    drawCentered(tr(TextId::PuzzleSolved), kScreenWidth / 2, 590, 4);
  }
}

void drawNonogram() {
  epaper.fillSprite(TFT_WHITE);
  drawNonogramBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::Nonogram));
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
  drawCentered(String(tr(TextId::Black)) + " " +
                   String(reversi.score(ReversiGame::Disc::Black)) + "    " +
                   tr(TextId::White) + " " +
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
            ? tr(TextId::BlackWins)
            : winner == ReversiGame::Disc::White ? tr(TextId::WhiteWins)
                                                  : tr(TextId::Draw);
    drawCentered(result, kScreenWidth / 2, 610, 4);
  } else if (status != nullptr) {
    drawCentered(status, kScreenWidth / 2, 610, 4);
  } else {
    drawCentered(reversi.currentPlayer() == ReversiGame::Disc::Black
                     ? tr(TextId::BlackToMove)
                     : tr(TextId::WhiteToMove),
                 kScreenWidth / 2, 610, 4);
  }
}

const char* reversiModeLabel() {
  return reversiMode == ReversiMode::SinglePlayer ? tr(TextId::OnePlayer)
                                                   : tr(TextId::TwoPlayers);
}

void drawReversi() {
  epaper.fillSprite(TFT_WHITE);
  drawReversiBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, reversiModeLabel());
  drawGameStatusBar(tr(TextId::Reversi));
}

void drawDotsAndBoxesBoard() {
  epaper.fillRect(kDotsBoardRegion.x, kDotsBoardRegion.y,
                  kDotsBoardRegion.width, kDotsBoardRegion.height, TFT_WHITE);
  drawCentered(String(tr(TextId::Player)) + " 1 " +
                   String(dotsAndBoxes.player1Score()) + "    " +
                   tr(TextId::Player) + " 2 " +
                   String(dotsAndBoxes.player2Score()),
               kScreenWidth / 2, 112, 4);

  for (int row = 0; row < DotsAndBoxesGame::kBoxSize; ++row) {
    for (int column = 0; column < DotsAndBoxesGame::kBoxSize; ++column) {
      const int owner = static_cast<int>(dotsAndBoxes.owner(row, column));
      if (owner == 0) continue;
      const int x = kDotsGridLeft + column * kDotsSpacing + 8;
      const int y = kDotsGridTop + row * kDotsSpacing + 8;
      const int size = kDotsSpacing - 16;
      if (owner == 1) {
        epaper.fillRect(x, y, size, size, TFT_BLACK);
        drawCenteredNumber(1, x + size / 2, y + size / 2, 4, TFT_WHITE,
                           TFT_BLACK);
      } else {
        epaper.drawRect(x, y, size, size, TFT_BLACK);
        epaper.drawRect(x + 3, y + 3, size - 6, size - 6, TFT_BLACK);
        drawCenteredNumber(2, x + size / 2, y + size / 2, 4, TFT_BLACK,
                           TFT_WHITE);
      }
    }
  }

  for (int row = 0; row <= DotsAndBoxesGame::kBoxSize; ++row) {
    for (int column = 0; column < DotsAndBoxesGame::kBoxSize; ++column) {
      if (!dotsAndBoxes.horizontalEdge(row, column)) continue;
      epaper.fillRect(kDotsGridLeft + column * kDotsSpacing,
                      kDotsGridTop + row * kDotsSpacing - 3,
                      kDotsSpacing + 1, 7, TFT_BLACK);
    }
  }
  for (int row = 0; row < DotsAndBoxesGame::kBoxSize; ++row) {
    for (int column = 0; column <= DotsAndBoxesGame::kBoxSize; ++column) {
      if (!dotsAndBoxes.verticalEdge(row, column)) continue;
      epaper.fillRect(kDotsGridLeft + column * kDotsSpacing - 3,
                      kDotsGridTop + row * kDotsSpacing, 7,
                      kDotsSpacing + 1, TFT_BLACK);
    }
  }
  for (int row = 0; row <= DotsAndBoxesGame::kBoxSize; ++row) {
    for (int column = 0; column <= DotsAndBoxesGame::kBoxSize; ++column) {
      epaper.fillCircle(kDotsGridLeft + column * kDotsSpacing,
                        kDotsGridTop + row * kDotsSpacing, 7, TFT_BLACK);
    }
  }

  if (dotsAndBoxes.gameOver()) {
    const int winner = static_cast<int>(dotsAndBoxes.winner());
    drawCentered(winner == 0
                     ? String(tr(TextId::Draw))
                     : String(tr(TextId::Player)) + " " + String(winner) +
                           " " + tr(TextId::Wins),
                 kScreenWidth / 2, 590, 4);
  } else {
    drawCentered(String(tr(TextId::Player)) + " " +
                     String(static_cast<int>(dotsAndBoxes.currentPlayer())) +
                     " " + tr(TextId::ToMove),
                 kScreenWidth / 2, 590, 4);
  }
}

void drawDotsAndBoxes() {
  epaper.fillSprite(TFT_WHITE);
  drawDotsAndBoxesBoard();
  drawButton(kCenteredNewButton, tr(TextId::NewGame));
  drawGameStatusBar(tr(TextId::DotsAndBoxes));
}

int sokobanCellSize() {
  const int horizontal = kSokobanMaxBoardPixels / sokoban.width();
  const int vertical = kSokobanMaxBoardPixels / sokoban.height();
  return std::min(56, std::min(horizontal, vertical));
}

int sokobanGridLeft() {
  return (kScreenWidth - sokoban.width() * sokobanCellSize()) / 2;
}

void drawSokobanCell(int row, int column) {
  const SokobanGame::Cell cell = sokoban.cellAt(row, column);
  if (cell == SokobanGame::Cell::Outside) return;

  const int cellSize = sokobanCellSize();
  const int x = sokobanGridLeft() + column * cellSize;
  const int y = kSokobanGridTop + row * cellSize;
  if (cell == SokobanGame::Cell::Wall) {
    epaper.fillRect(x, y, cellSize, cellSize, TFT_BLACK);
    if (cellSize >= 28) {
      epaper.drawRect(x + 3, y + 3, cellSize - 6, cellSize - 6, TFT_WHITE);
    }
    return;
  }

  epaper.fillRect(x, y, cellSize, cellSize, TFT_WHITE);
  if (cellSize >= 28) epaper.drawRect(x, y, cellSize, cellSize, TFT_BLACK);
  const int centerX = x + cellSize / 2;
  const int centerY = y + cellSize / 2;
  if (sokoban.isTarget(row, column)) {
    const int targetRadius = std::max(2, cellSize / 5);
    epaper.drawCircle(centerX, centerY, targetRadius, TFT_BLACK);
    epaper.fillCircle(centerX, centerY, std::max(1, targetRadius / 3),
                      TFT_BLACK);
  }
  if (sokoban.hasBox(row, column)) {
    const int inset = std::max(2, cellSize / 8);
    const int far = cellSize - inset - 1;
    epaper.fillRect(x + inset, y + inset, cellSize - inset * 2,
                    cellSize - inset * 2, TFT_BLACK);
    if (cell == SokobanGame::Cell::BoxOnTarget) {
      epaper.drawCircle(centerX, centerY, std::max(2, cellSize / 5),
                        TFT_WHITE);
    } else {
      epaper.drawLine(x + inset + 2, y + inset + 2, x + far - 2,
                      y + far - 2, TFT_WHITE);
      epaper.drawLine(x + far - 2, y + inset + 2, x + inset + 2,
                      y + far - 2, TFT_WHITE);
    }
  }
  if (sokoban.playerRow() == row && sokoban.playerColumn() == column) {
    const int radius = std::max(3, cellSize / 3);
    epaper.fillCircle(centerX, centerY, radius, TFT_BLACK);
    epaper.fillCircle(centerX, centerY - std::max(1, radius / 4),
                      std::max(1, radius / 3), TFT_WHITE);
  }
}

void drawSokobanBoard() {
  epaper.fillRect(kSokobanBoardRegion.x, kSokobanBoardRegion.y,
                  kSokobanBoardRegion.width, kSokobanBoardRegion.height,
                  TFT_WHITE);
  if (sokobanCompletedLevelCount == SokobanGame::kLevelCount) {
    drawCentered(String("155 / 155 ") + tr(TextId::Levels),
                 kScreenWidth / 2, 250, 4);
    drawCentered(tr(TextId::AllComplete), kScreenWidth / 2, 330, 6);
    drawCentered(tr(TextId::ProgressSaved), kScreenWidth / 2, 410, 4);
    return;
  }

  drawCentered(String(tr(TextId::Level)) + " " +
                   String(sokoban.levelIndex() + 1) + "/" +
                   String(SokobanGame::kLevelCount),
               kScreenWidth / 2, 112, 4);
  for (int row = 0; row < sokoban.height(); ++row) {
    for (int column = 0; column < sokoban.width(); ++column) {
      drawSokobanCell(row, column);
    }
  }
  if (sokoban.solved()) {
    drawCentered(sokobanProgressSaveFailed ? tr(TextId::SaveFailedTapNext)
                                           : tr(TextId::LevelComplete),
                 kScreenWidth / 2, 590, 4);
  } else {
    drawCentered(String(sokoban.moveCount()) + " " + tr(TextId::Moves) +
                     "  " + String(sokoban.pushCount()) + " " +
                     tr(TextId::Pushes),
                 kScreenWidth / 2, 590, 4);
  }
}

void drawSokobanControls() {
  if (sokobanCompletedLevelCount < SokobanGame::kLevelCount) {
    drawButton(kCenteredNewButton,
               sokoban.solved() ? tr(TextId::Next) : tr(TextId::Restart));
  }
}

void drawSokoban() {
  epaper.fillSprite(TFT_WHITE);
  drawSokobanBoard();
  drawSokobanControls();
  drawGameStatusBar(tr(TextId::Sokoban));
}

void drawPegSolitaireBoard() {
  epaper.fillRect(kPegBoardRegion.x, kPegBoardRegion.y, kPegBoardRegion.width,
                  kPegBoardRegion.height, TFT_WHITE);
  drawCentered(String(pegSolitaire.pegCount()) + " " + tr(TextId::Pegs),
               kScreenWidth / 2, 112, 4);
  for (int row = 0; row < PegSolitaireGame::kSize; ++row) {
    for (int column = 0; column < PegSolitaireGame::kSize; ++column) {
      if (!pegSolitaire.validCell(row, column)) continue;
      const int centerX =
          kPegGridLeft + column * kPegCellSize + kPegCellSize / 2;
      const int centerY =
          kPegGridTop + row * kPegCellSize + kPegCellSize / 2;
      epaper.drawCircle(centerX, centerY, 18, TFT_BLACK);
      epaper.drawCircle(centerX, centerY, 17, TFT_BLACK);
      if (!pegSolitaire.hasPeg(row, column)) continue;
      epaper.fillCircle(centerX, centerY, 15, TFT_BLACK);
      if (pegSolitaire.selectedRow() == row &&
          pegSolitaire.selectedColumn() == column) {
        epaper.drawCircle(centerX, centerY, 9, TFT_WHITE);
        epaper.drawCircle(centerX, centerY, 8, TFT_WHITE);
      }
    }
  }
  if (pegSolitaire.solved()) {
    drawCentered(tr(TextId::PerfectCenterFinish), kScreenWidth / 2, 590, 4);
  } else if (!pegSolitaire.hasAvailableMove()) {
    drawCentered(String(tr(TextId::NoMoves)) + " - " +
                     String(pegSolitaire.pegCount()) + " " + tr(TextId::Pegs),
                 kScreenWidth / 2, 590, 4);
  } else {
    drawCentered(String(pegSolitaire.moves()) + " " + tr(TextId::Moves),
                 kScreenWidth / 2, 590, 4);
  }
}

void drawPegSolitaire() {
  epaper.fillSprite(TFT_WHITE);
  drawPegSolitaireBoard();
  drawButton(kCenteredNewButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::PegSolitaire));
}

void drawSlitherlinkBoard() {
  epaper.fillRect(kSlitherlinkBoardRegion.x, kSlitherlinkBoardRegion.y,
                  kSlitherlinkBoardRegion.width,
                  kSlitherlinkBoardRegion.height, TFT_WHITE);
  drawCentered(String(tr(TextId::Puzzle)) + " " +
                   String(slitherlink.puzzleIndex() + 1) + "/" +
                   String(SlitherlinkGame::kPuzzleCount),
               kScreenWidth / 2, 112, 4);
  for (int row = 0; row < SlitherlinkGame::kSize; ++row) {
    for (int column = 0; column < SlitherlinkGame::kSize; ++column) {
      const int clue = slitherlink.clue(row, column);
      if (clue >= 0) {
        drawCenteredNumber(
            static_cast<uint32_t>(clue),
            kSlitherlinkGridLeft + column * kSlitherlinkCellSize +
                kSlitherlinkCellSize / 2,
            kSlitherlinkGridTop + row * kSlitherlinkCellSize +
                kSlitherlinkCellSize / 2,
            4, TFT_BLACK, TFT_WHITE);
      }
    }
  }
  for (int row = 0; row <= SlitherlinkGame::kSize; ++row) {
    for (int column = 0; column < SlitherlinkGame::kSize; ++column) {
      const int x = kSlitherlinkGridLeft + column * kSlitherlinkCellSize;
      const int y = kSlitherlinkGridTop + row * kSlitherlinkCellSize;
      const SlitherlinkGame::EdgeState edge =
          slitherlink.horizontalEdge(row, column);
      if (edge == SlitherlinkGame::EdgeState::Line) {
        epaper.fillRect(x, y - 3, kSlitherlinkCellSize + 1, 7, TFT_BLACK);
      } else if (edge == SlitherlinkGame::EdgeState::Cross) {
        const int centerX = x + kSlitherlinkCellSize / 2;
        epaper.drawLine(centerX - 8, y - 8, centerX + 8, y + 8, TFT_BLACK);
        epaper.drawLine(centerX - 8, y + 8, centerX + 8, y - 8, TFT_BLACK);
      }
    }
  }
  for (int row = 0; row < SlitherlinkGame::kSize; ++row) {
    for (int column = 0; column <= SlitherlinkGame::kSize; ++column) {
      const int x = kSlitherlinkGridLeft + column * kSlitherlinkCellSize;
      const int y = kSlitherlinkGridTop + row * kSlitherlinkCellSize;
      const SlitherlinkGame::EdgeState edge =
          slitherlink.verticalEdge(row, column);
      if (edge == SlitherlinkGame::EdgeState::Line) {
        epaper.fillRect(x - 3, y, 7, kSlitherlinkCellSize + 1, TFT_BLACK);
      } else if (edge == SlitherlinkGame::EdgeState::Cross) {
        const int centerY = y + kSlitherlinkCellSize / 2;
        epaper.drawLine(x - 8, centerY - 8, x + 8, centerY + 8, TFT_BLACK);
        epaper.drawLine(x - 8, centerY + 8, x + 8, centerY - 8, TFT_BLACK);
      }
    }
  }
  for (int row = 0; row <= SlitherlinkGame::kSize; ++row) {
    for (int column = 0; column <= SlitherlinkGame::kSize; ++column) {
      epaper.fillCircle(kSlitherlinkGridLeft + column * kSlitherlinkCellSize,
                        kSlitherlinkGridTop + row * kSlitherlinkCellSize, 6,
                        TFT_BLACK);
    }
  }
  drawCentered(slitherlink.solved() ? tr(TextId::LoopComplete)
                                    : tr(TextId::MakeOneLoop),
               kScreenWidth / 2, 590, 4);
}

void drawSlitherlink() {
  epaper.fillSprite(TFT_WHITE);
  drawSlitherlinkBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::Slitherlink));
}

Rect sudokuKeyRect(int key) {
  constexpr int kKeyWidth = 42;
  constexpr int kKeyGap = 4;
  return {12 + key * (kKeyWidth + kKeyGap), 584, kKeyWidth, 50};
}

void drawSmallButton(const Rect& rect, const char* label, int font = 2) {
  const bool smoothFont = languageSelectionVisible ||
                          currentLanguage != Language::English;
  const int verticalOffset =
      smoothFont ? (font >= 4 ? -2 : -1) : (font == 4 ? 3 : 1);
  epaper.fillRoundRect(rect.x, rect.y, rect.width, rect.height, 6, TFT_BLACK);
  drawCenteredText(label, rect.x + rect.width / 2,
                   rect.y + rect.height / 2 + verticalOffset, font, TFT_WHITE,
                   TFT_BLACK, rect.width - 4, smoothFont);
}

void drawSudokuBoard() {
  epaper.fillRect(kSudokuBoardRegion.x, kSudokuBoardRegion.y,
                  kSudokuBoardRegion.width, kSudokuBoardRegion.height,
                  TFT_WHITE);
  drawCentered(String(tr(TextId::Puzzle)) + " " +
                   String(sudoku.puzzleIndex() + 1) + "/" +
                   String(SudokuGame::kPuzzleCount),
               kScreenWidth / 2, 78, 2);
  for (int row = 0; row < SudokuGame::kSize; ++row) {
    for (int column = 0; column < SudokuGame::kSize; ++column) {
      const int x = kSudokuGridLeft + column * kSudokuCellSize;
      const int y = kSudokuGridTop + row * kSudokuCellSize;
      const bool selected = sudoku.selected(row, column);
      epaper.fillRect(x, y, kSudokuCellSize, kSudokuCellSize,
                      selected ? TFT_BLACK : TFT_WHITE);
      epaper.drawRect(x, y, kSudokuCellSize, kSudokuCellSize, TFT_BLACK);
      const uint8_t value = sudoku.at(row, column);
      if (value != 0) {
        drawCenteredNumber(value, x + kSudokuCellSize / 2,
                           y + kSudokuCellSize / 2, 4,
                           selected ? TFT_WHITE : TFT_BLACK,
                           selected ? TFT_BLACK : TFT_WHITE);
        if (!sudoku.given(row, column) && !selected) {
          epaper.drawCircle(x + kSudokuCellSize / 2,
                            y + kSudokuCellSize / 2, 16, TFT_BLACK);
        }
      }
    }
  }
  for (int section = 0; section <= 3; ++section) {
    const int offset = section * kSudokuCellSize * 3;
    epaper.fillRect(kSudokuGridLeft + offset - 1, kSudokuGridTop, 3,
                    kSudokuGridSize, TFT_BLACK);
    epaper.fillRect(kSudokuGridLeft, kSudokuGridTop + offset - 1,
                    kSudokuGridSize, 3, TFT_BLACK);
  }
  drawCentered(sudoku.solved() ? tr(TextId::PuzzleComplete)
                               : sudoku.selectedIndex() == SudokuGame::kNoSelection
                                     ? tr(TextId::TapEmptyCell)
                                     : tr(TextId::ChooseOneToNineOrX),
               kScreenWidth / 2, 548, 4);
  for (int key = 0; key < 10; ++key) {
    if (key < 9) {
      const String label(key + 1);
      drawSmallButton(sudokuKeyRect(key), label.c_str(), 4);
    } else {
      drawSmallButton(sudokuKeyRect(key), "X", 4);
    }
  }
}

void drawSudoku() {
  epaper.fillSprite(TFT_WHITE);
  drawSudokuBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::Sudoku));
}

int crosswordCellSize() {
  return std::min(72, kCrosswordMaxGridPixels /
                          std::max(crossword.width(), crossword.height()));
}

int crosswordGridLeft() {
  return (kScreenWidth - crossword.width() * crosswordCellSize()) / 2;
}

Rect crosswordKeyboardKeyRect(int row, int key) {
  const int y =
      kKeyboardFirstRowY + row * (kKeyboardKeyHeight + kKeyboardRowGap);
  if (row == 0) return {31 + key * 42, y, 39, kKeyboardKeyHeight};
  if (row == 1) return {52 + key * 42, y, 39, kKeyboardKeyHeight};
  if (key < 7) return {37 + key * 42, y, 39, kKeyboardKeyHeight};
  return {37 + 7 * 42 + (key - 7) * 57, y, 54, kKeyboardKeyHeight};
}

const char* crosswordKeyboardLabel(int row, int key) {
  static constexpr const char* kRows[] = {
      "QWERTYUIOP",
      "ASDFGHJKL",
      "ZXCVBNM",
  };
  if (row < 2 || key < 7) {
    static char label[2] = {};
    label[0] = kRows[row][key];
    return label;
  }
  return key == 7 ? tr(TextId::DeleteKey) : tr(TextId::OkKey);
}

void drawCrosswordKeyboard() {
  const int keyCounts[3] = {10, 9, 9};
  for (int row = 0; row < 3; ++row) {
    for (int key = 0; key < keyCounts[row]; ++key) {
      drawSmallButton(crosswordKeyboardKeyRect(row, key),
                      crosswordKeyboardLabel(row, key));
    }
  }
}

void drawCrosswordBoard() {
  epaper.fillRect(kCrosswordBoardRegion.x, kCrosswordBoardRegion.y,
                  kCrosswordBoardRegion.width, kCrosswordBoardRegion.height,
                  TFT_WHITE);
  drawCentered(String(tr(TextId::Puzzle)) + " " +
                   String(crossword.puzzleIndex() + 1) + "/" +
                   String(CrosswordGame::kPuzzleCount),
               kScreenWidth / 2, 70, 4);
  const int cellSize = crosswordCellSize();
  const int left = crosswordGridLeft();
  for (int row = 0; row < crossword.height(); ++row) {
    for (int column = 0; column < crossword.width(); ++column) {
      const int x = left + column * cellSize;
      const int y = kCrosswordGridTop + row * cellSize;
      if (crossword.blocked(row, column)) {
        epaper.fillRect(x, y, cellSize, cellSize, TFT_BLACK);
        continue;
      }
      const bool selected = crossword.selected(row, column);
      epaper.fillRect(x, y, cellSize, cellSize,
                      selected ? TFT_BLACK : TFT_WHITE);
      epaper.drawRect(x, y, cellSize, cellSize, TFT_BLACK);
      const int number = crossword.cellNumber(row, column);
      if (number > 0) {
        epaper.setTextDatum(TL_DATUM);
        epaper.setTextColor(selected ? TFT_WHITE : TFT_BLACK,
                            selected ? TFT_BLACK : TFT_WHITE, true);
        epaper.drawNumber(number, x + 3, y + 2, 2);
      }
      const char entry = crossword.entryAt(row, column);
      if (entry != '\0') {
        epaper.setTextDatum(MC_DATUM);
        epaper.setTextColor(selected ? TFT_WHITE : TFT_BLACK,
                            selected ? TFT_BLACK : TFT_WHITE, true);
        epaper.drawString(String(entry), x + cellSize / 2,
                          y + cellSize / 2 + 3, cellSize >= 48 ? 4 : 2);
      }
    }
  }

  const int gridBottom = kCrosswordGridTop + crossword.height() * cellSize;
  if (crossword.selectedIndex() != CrosswordGame::kNoSelection) {
    const char* direction =
        crossword.direction() == CrosswordGame::Direction::Across
            ? tr(TextId::AcrossAbbreviation)
            : tr(TextId::DownAbbreviation);
    drawCentered(String(crossword.currentClueNumber()) + direction + "  " +
                     crossword.currentClueText(),
                 kScreenWidth / 2, gridBottom + 28, 4);
  } else {
    drawCentered(tr(TextId::TapWhiteSquare), kScreenWidth / 2,
                 gridBottom + 28, 4);
  }
  if (crossword.solved()) {
    drawCentered(tr(TextId::PuzzleComplete), kScreenWidth / 2, 550, 4);
  }
}

void drawCrosswordControls() {
  if (crosswordKeyboardVisible && !crossword.solved()) {
    drawCrosswordKeyboard();
  } else {
    drawButton(kNewButton, tr(TextId::NewGame));
    drawButton(kResetButton, tr(TextId::Reset));
  }
}

void drawCrossword() {
  epaper.fillSprite(TFT_WHITE);
  drawCrosswordBoard();
  drawCrosswordControls();
  drawGameStatusBar(tr(TextId::Crossword));
}

Rect klondikeFoundationSlot(int suit) {
  return {208 + suit * kKlondikeColumnStep, 72, kKlondikeCardWidth,
          kKlondikeCardHeight};
}

int klondikeTableauCardY(int column, int cardIndex) {
  const int count = klondike.tableauCount(column);
  if (count <= 1) return kKlondikeTableauTop;
  const int hidden = klondike.faceUpStart(column);
  constexpr int kHiddenStep = 16;
  constexpr int kFaceUpStep = 28;
  constexpr int kLastCardBottom = 666;
  const int available =
      kLastCardBottom - kKlondikeTableauTop - kKlondikeCardHeight;
  const int desired =
      hidden * kHiddenStep + std::max(0, count - hidden - 1) * kFaceUpStep;
  if (desired <= available) {
    return kKlondikeTableauTop +
           std::min(cardIndex, hidden) * kHiddenStep +
           std::max(0, cardIndex - hidden) * kFaceUpStep;
  }
  return kKlondikeTableauTop +
         cardIndex * std::max(7, available / (count - 1));
}

void drawKlondikeBoard() {
  epaper.fillRect(kKlondikeBoardRegion.x, kKlondikeBoardRegion.y,
                   kKlondikeBoardRegion.width, kKlondikeBoardRegion.height,
                   TFT_WHITE);
  if (klondike.stockCount() > 0) {
    drawKlondikeCard(kKlondikeStockSlot.x, kKlondikeStockSlot.y,
                     kKlondikeCardWidth, kKlondikeCardHeight, 0, false);
  } else {
    epaper.drawRoundRect(kKlondikeStockSlot.x, kKlondikeStockSlot.y,
                         kKlondikeCardWidth, kKlondikeCardHeight, 5,
                         TFT_BLACK);
  }
  if (klondike.wasteCount() > 0) {
    drawKlondikeCard(kKlondikeWasteSlot.x, kKlondikeWasteSlot.y,
                     kKlondikeCardWidth, kKlondikeCardHeight,
                     klondike.wasteTop(), true, klondike.wasteSelected());
  } else {
    epaper.drawRoundRect(kKlondikeWasteSlot.x, kKlondikeWasteSlot.y,
                         kKlondikeCardWidth, kKlondikeCardHeight, 5,
                         TFT_BLACK);
  }
  for (int suit = 0; suit < KlondikeGame::kSuitCount; ++suit) {
    const Rect slot = klondikeFoundationSlot(suit);
    const uint8_t card = klondike.foundationTop(suit);
    if (card == KlondikeGame::kNoCard) {
      epaper.drawRoundRect(slot.x, slot.y, slot.width, slot.height, 5,
                           TFT_BLACK);
      drawKlondikeSuit(suit, slot.x + slot.width / 2,
                        slot.y + slot.height / 2, 22, TFT_BLACK);
    } else {
      drawKlondikeCard(slot.x, slot.y, slot.width, slot.height, card, true,
                       klondike.foundationSelected(suit));
    }
  }

  for (int column = 0; column < KlondikeGame::kTableauCount; ++column) {
    const int count = klondike.tableauCount(column);
    const int x = kKlondikeGridLeft + column * kKlondikeColumnStep;
    if (count == 0) {
      epaper.drawRoundRect(x, kKlondikeTableauTop, kKlondikeCardWidth,
                           kKlondikeCardHeight, 5, TFT_BLACK);
      drawCentered("K", x + kKlondikeCardWidth / 2,
                   kKlondikeTableauTop + kKlondikeCardHeight / 2, 4);
      continue;
    }
    for (int index = 0; index < count; ++index) {
      drawKlondikeCard(
          x, klondikeTableauCardY(column, index), kKlondikeCardWidth,
          kKlondikeCardHeight, klondike.tableauCard(column, index),
          klondike.tableauCardFaceUp(column, index),
          klondike.isTableauCardSelected(column, index));
    }
  }

  if (klondike.solved()) {
    drawCentered(tr(TextId::Solved), kScreenWidth / 2, 654, 4);
  } else {
    drawCentered(String(klondike.moves()) + " " + tr(TextId::Moves),
                 kScreenWidth / 2, 654, 4);
  }
}

void drawKlondike() {
  epaper.fillSprite(TFT_WHITE);
  drawKlondikeBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::Klondike));
}

Rect mahjongTileRect(int index) {
  const MahjongSolitaireGame::Position position =
      MahjongSolitaireGame::position(index);
  return {kMahjongGridLeft + position.column * kMahjongColumnStep +
              position.layer * kMahjongLayerOffset,
          kMahjongGridTop + position.row * kMahjongRowStep -
              position.layer * kMahjongLayerOffset,
          kMahjongTileWidth, kMahjongTileHeight};
}

void drawMahjongBoard() {
  epaper.fillRect(kMahjongBoardRegion.x, kMahjongBoardRegion.y,
                   kMahjongBoardRegion.width, kMahjongBoardRegion.height,
                   TFT_WHITE);
  for (int layer = 0; layer < 4; ++layer) {
    for (int index = 0; index < MahjongSolitaireGame::kTileCount; ++index) {
      if (!mahjong.occupied(index) ||
          MahjongSolitaireGame::position(index).layer != layer) {
        continue;
      }
      const Rect tile = mahjongTileRect(index);
      if (layer > 0) {
        epaper.fillRoundRect(tile.x - 3, tile.y + 3, tile.width, tile.height, 3,
                            TFT_BLACK);
      }
      drawMahjongTile(tile.x, tile.y, tile.width, tile.height,
                      mahjong.tileSuit(index), mahjong.tileRank(index),
                      mahjong.selected(index));
    }
  }

  if (mahjong.solved()) {
    drawCentered(tr(TextId::Solved), kScreenWidth / 2, 654, 4);
  } else if (!mahjong.hasMoves()) {
    drawCentered(tr(TextId::NoMoves), kScreenWidth / 2, 654, 4);
  } else {
    drawCentered(String(mahjong.remaining()) + " " + tr(TextId::Tiles) +
                     "   " + String(mahjong.moves()) + " " +
                     tr(TextId::Moves),
                 kScreenWidth / 2, 654, 4);
  }
}

void drawMahjong() {
  epaper.fillSprite(TFT_WHITE);
  drawMahjongBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::MahjongSolitaire));
}

void drawFallingBlocksBoard() {
  epaper.fillRect(kFallingBlocksBoardRegion.x, kFallingBlocksBoardRegion.y,
                  kFallingBlocksBoardRegion.width,
                  kFallingBlocksBoardRegion.height, TFT_WHITE);
  for (int row = 0; row < FallingBlocksGame::kHeight; ++row) {
    for (int column = 0; column < FallingBlocksGame::kWidth; ++column) {
      drawFallingBlock(kFallingGridLeft + column * kFallingCellSize,
                       kFallingGridTop + row * kFallingCellSize,
                       kFallingCellSize, fallingBlocks.at(row, column));
    }
  }
  epaper.drawRect(kFallingGridLeft - 2, kFallingGridTop - 2,
                  kFallingGridWidth + 4, kFallingGridHeight + 4, TFT_BLACK);

  constexpr int kPanelCenterX = 416;
  drawCenteredText(tr(TextId::Score), kPanelCenterX, 120, 2, TFT_BLACK,
                   TFT_WHITE, 104, currentLanguage != Language::English);
  drawCentered(String(fallingBlocks.score()), kPanelCenterX, 148, 4);
  drawCenteredText(tr(TextId::Lines), kPanelCenterX, 198, 2, TFT_BLACK,
                   TFT_WHITE, 104, currentLanguage != Language::English);
  drawCentered(String(fallingBlocks.lines()), kPanelCenterX, 226, 4);
  drawCenteredText(tr(TextId::Level), kPanelCenterX, 276, 2, TFT_BLACK,
                   TFT_WHITE, 104, currentLanguage != Language::English);
  drawCentered(String(fallingBlocks.level()), kPanelCenterX, 304, 4);
  drawCenteredText(tr(TextId::Next), kPanelCenterX, 360, 2, TFT_BLACK,
                   TFT_WHITE, 104, currentLanguage != Language::English);
  constexpr int kPreviewCell = 20;
  constexpr int kPreviewLeft = 376;
  constexpr int kPreviewTop = 386;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      if (FallingBlocksGame::pieceCell(fallingBlocks.nextPiece(), 0, row,
                                       column)) {
        drawFallingBlock(kPreviewLeft + column * kPreviewCell,
                         kPreviewTop + row * kPreviewCell, kPreviewCell,
                         fallingBlocks.nextPiece() + 1);
      }
    }
  }
  if (fallingBlocks.gameOver()) {
    drawCenteredText(
        String(tr(TextId::GameOver)) + " - " + tr(TextId::TapNew),
        kScreenWidth / 2, 668, 2, TFT_BLACK, TFT_WHITE, 440,
        currentLanguage != Language::English);
  }
}

void drawFallingBlocks() {
  epaper.fillSprite(TFT_WHITE);
  drawFallingBlocksBoard();
  drawButton(kCenteredNewButton, tr(TextId::NewGame));
  drawGameStatusBar(tr(TextId::FallingBlocks));
}

const char* connectFourModeLabel() {
  return connectFourMode == ReversiMode::SinglePlayer
             ? tr(TextId::OnePlayer)
             : tr(TextId::TwoPlayers);
}

void drawConnectFourBoard() {
  epaper.fillRect(kConnectFourBoardRegion.x, kConnectFourBoardRegion.y,
                  kConnectFourBoardRegion.width,
                  kConnectFourBoardRegion.height, TFT_WHITE);
  for (int row = 0; row < ConnectFourGame::kRows; ++row) {
    for (int column = 0; column < ConnectFourGame::kColumns; ++column) {
      const int x = kConnectFourGridLeft + column * kConnectFourCellSize;
      const int y = kConnectFourGridTop + row * kConnectFourCellSize;
      const int centerX = x + kConnectFourCellSize / 2;
      const int centerY = y + kConnectFourCellSize / 2;
      epaper.drawRect(x, y, kConnectFourCellSize, kConnectFourCellSize,
                      TFT_BLACK);
      const ConnectFourGame::Disc disc = connectFour.at(row, column);
      if (disc == ConnectFourGame::Disc::Red) {
        epaper.fillCircle(centerX, centerY, 20, TFT_BLACK);
      } else if (disc == ConnectFourGame::Disc::Yellow) {
        epaper.drawCircle(centerX, centerY, 20, TFT_BLACK);
        epaper.drawCircle(centerX, centerY, 19, TFT_BLACK);
        epaper.drawCircle(centerX, centerY, 13, TFT_BLACK);
      }
    }
  }

  const char* status = nullptr;
  if (connectFour.gameOver()) {
    const ConnectFourGame::Disc winner = connectFour.winner();
    status = winner == ConnectFourGame::Disc::Red
                 ? tr(TextId::RedWins)
                 : winner == ConnectFourGame::Disc::Yellow
                       ? tr(TextId::YellowWins)
                       : tr(TextId::Draw);
  } else {
    status = connectFour.currentPlayer() == ConnectFourGame::Disc::Red
                 ? tr(TextId::RedToMove)
                 : tr(TextId::YellowToMove);
  }
  drawCentered(status, kScreenWidth / 2, 555, 4);
}

void drawConnectFour() {
  epaper.fillSprite(TFT_WHITE);
  drawConnectFourBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, connectFourModeLabel());
  drawGameStatusBar(tr(TextId::ConnectFour));
}

void drawWordSearchBoard() {
  epaper.fillRect(kWordSearchBoardRegion.x, kWordSearchBoardRegion.y,
                  kWordSearchBoardRegion.width,
                  kWordSearchBoardRegion.height, TFT_WHITE);
  for (int row = 0; row < WordSearchGame::kSize; ++row) {
    for (int column = 0; column < WordSearchGame::kSize; ++column) {
      const int x = kWordSearchGridLeft + column * kWordSearchCellSize;
      const int y = kWordSearchGridTop + row * kWordSearchCellSize;
      epaper.drawRect(x, y, kWordSearchCellSize, kWordSearchCellSize,
                      TFT_BLACK);
      const char label[] = {wordSearch.at(row, column), '\0'};
      drawCentered(label, x + kWordSearchCellSize / 2,
                   y + kWordSearchCellSize / 2 + 2, 2);
    }
  }

  for (int index = 0; index < WordSearchGame::kWordCount; ++index) {
    const int column = index % 2;
    const int row = index / 2;
    const Rect wordRect = {20 + column * 230, 526 + row * 48, 210, 38};
    if (wordSearch.found(index)) {
      epaper.fillRoundRect(wordRect.x, wordRect.y, wordRect.width,
                           wordRect.height, 5, TFT_BLACK);
      drawCenteredText(wordSearch.word(index),
                       wordRect.x + wordRect.width / 2,
                       wordRect.y + wordRect.height / 2, 2, TFT_WHITE,
                       TFT_BLACK, wordRect.width - 8);
    } else {
      epaper.drawRoundRect(wordRect.x, wordRect.y, wordRect.width,
                           wordRect.height, 5, TFT_BLACK);
      drawCenteredText(wordSearch.word(index),
                       wordRect.x + wordRect.width / 2,
                       wordRect.y + wordRect.height / 2, 2, TFT_BLACK,
                       TFT_WHITE, wordRect.width - 8);
    }
  }
  if (wordSearch.solved()) {
    drawCentered(tr(TextId::PuzzleComplete), kScreenWidth / 2, 674, 2);
  }
}

void drawWordSearch() {
  epaper.fillSprite(TFT_WHITE);
  drawWordSearchBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, tr(TextId::Reset));
  drawGameStatusBar(tr(TextId::WordSearch));
}

String fitReaderText(String text, int maximumWidth, int builtInFont = 0) {
  const auto textWidth = [builtInFont](const String& value) {
    return builtInFont > 0 ? epaper.textWidth(value, builtInFont)
                           : epaper.textWidth(value);
  };
  if (textWidth(text) <= maximumWidth) return text;
  constexpr char kEllipsis[] = "...";
  while (!text.isEmpty() && textWidth(text + kEllipsis) > maximumWidth) {
    size_t offset = text.length() - 1;
    while (offset > 0 &&
           (static_cast<uint8_t>(text[offset]) & 0xC0) == 0x80) {
      --offset;
    }
    text.remove(offset);
  }
  return text + kEllipsis;
}

void drawReaderString(const String& text, int x, int y, int builtInFont = 0) {
  if (builtInFont > 0) {
    epaper.drawString(text, x, y, builtInFont);
  } else {
    epaper.drawString(text, x, y);
  }
}

const uint8_t* embeddedReaderFont(epub_text::TextStyle style) {
  switch (style) {
    case epub_text::TextStyle::Bold:
      return epub_latin_fonts::kBold;
    case epub_text::TextStyle::Italic:
      return epub_latin_fonts::kItalic;
    case epub_text::TextStyle::BoldItalic:
      return epub_latin_fonts::kBoldItalic;
    case epub_text::TextStyle::Regular:
      return epub_latin_fonts::kRegular;
  }
  return epub_latin_fonts::kRegular;
}

size_t readerGlyphWidth(uint32_t codepoint, epub_text::TextStyle style,
                        bool embeddedLatin) {
  if (codepoint >= 1 && codepoint <= 4) return 0;
  if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||
      (codepoint >= 0xFE00 && codepoint <= 0xFE0F)) {
    return 0;
  }
  if (embeddedLatin && epub_latin_fonts::supports(codepoint)) {
    return epub_latin_fonts::advance(static_cast<uint8_t>(style), codepoint);
  }

  uint8_t width = 0;
  const size_t fallbackWidth =
      epub_latin_fonts::maximumFallbackAdvance(codepoint, width) ? width : 24;
  return fallbackWidth +
         ((!embeddedLatin &&
           (style == epub_text::TextStyle::Bold ||
            style == epub_text::TextStyle::BoldItalic))
              ? 1
              : 0);
}

size_t readerLineWidth(const std::string& line,
                       epub_text::TextStyle initialStyle,
                       bool embeddedLatin) {
  size_t width = 0;
  epub_text::TextStyle style = initialStyle;
  for (size_t offset = 0; offset < line.length();) {
    if (epub_text::isStyleMarker(line[offset])) {
      style = epub_text::styleFromMarker(line[offset++]);
      continue;
    }
    const uint32_t codepoint =
        epub_text::utf8Codepoint(line.data(), line.length(), offset);
    width += readerGlyphWidth(codepoint, style, embeddedLatin);
    offset += epub_text::utf8CharacterBytes(line.data(), line.length(), offset);
  }
  return width;
}

void drawStyledReaderLine(const std::string& line, int x, int y,
                          epub_text::TextStyle& style, bool embeddedLatin,
                          epub_text::TextStyle& loadedStyle,
                          bool& embeddedFontLoaded, bool justify) {
  const size_t spaceCount =
      static_cast<size_t>(std::count(line.begin(), line.end(), ' '));
  const size_t lineWidth = readerLineWidth(line, style, embeddedLatin);
  const size_t extraWidth =
      justify && spaceCount > 0 && lineWidth < kReaderTextWidth
          ? kReaderTextWidth - lineWidth
          : 0;
  const size_t extraPerSpace =
      spaceCount > 0 ? extraWidth / spaceCount : 0;
  const size_t extraRemainder =
      spaceCount > 0 ? extraWidth % spaceCount : 0;
  size_t spaceIndex = 0;
  size_t runStart = 0;
  for (size_t offset = 0; offset <= line.length(); ++offset) {
    const bool atEnd = offset == line.length();
    const bool atStyle =
        !atEnd && epub_text::isStyleMarker(line[offset]);
    const bool atSpace = !atEnd && line[offset] == ' ';
    if (!atEnd && !atStyle && !atSpace) continue;

    if (offset > runStart) {
      const String run(line.substr(runStart, offset - runStart).c_str());
      if (embeddedLatin &&
          (!embeddedFontLoaded || loadedStyle != style)) {
        epaper.loadFont(embeddedReaderFont(style));
        loadedStyle = style;
        embeddedFontLoaded = true;
      }
      epaper.drawString(run, x, y);
      const int width = epaper.textWidth(run);
      if (!embeddedLatin &&
          (style == epub_text::TextStyle::Bold ||
           style == epub_text::TextStyle::BoldItalic)) {
        epaper.setTextColor(TFT_BLACK, TFT_BLACK, false);
        epaper.drawString(run, x + 1, y);
        epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
      }
      if (!embeddedLatin &&
          (style == epub_text::TextStyle::Italic ||
           style == epub_text::TextStyle::BoldItalic)) {
        epaper.drawFastHLine(x, y + 27, width, TFT_BLACK);
      }
      x += width;
    }
    if (atStyle) {
      style = epub_text::styleFromMarker(line[offset]);
    } else if (atSpace) {
      x += static_cast<int>(readerGlyphWidth(' ', style, embeddedLatin) +
                            extraPerSpace +
                            (spaceIndex < extraRemainder ? 1 : 0));
      ++spaceIndex;
    }
    runStart = offset + 1;
  }
}

bool usesEmbeddedReaderFont(const char* text, size_t length) {
  for (size_t offset = 0; offset < length;) {
    if (epub_text::isStyleMarker(text[offset])) {
      ++offset;
      continue;
    }
    const uint32_t codepoint =
        epub_text::utf8Codepoint(text, length, offset);
    if (!epub_latin_fonts::supports(codepoint)) return false;
    offset += epub_text::utf8CharacterBytes(text, length, offset);
  }
  return true;
}

bool pageUsesEmbeddedReaderFont(const epub_text::TextPage& page) {
  for (const std::string& line : page.lines) {
    if (!usesEmbeddedReaderFont(line.data(), line.length())) return false;
  }
  return true;
}

bool pageRequiresCjk(const epub_text::TextPage& page) {
  for (const std::string& line : page.lines) {
    if (epub_text::containsCjk(line.data(), line.length())) return true;
  }
  return false;
}

size_t readerCharacterWidth(uint32_t codepoint,
                            epub_text::TextStyle style) {
  return readerGlyphWidth(codepoint, style, true);
}

bool sdCardInserted() {
  pinMode(board::PIN_SD_DETECT, INPUT_PULLUP);
  delayMicroseconds(50);
  return digitalRead(board::PIN_SD_DETECT) == LOW;
}

bool readReaderCardIdentity(sd_card_identity::Identity& identity) {
  identity = {};
  if (!sdCardInserted()) return false;
  const size_t sectors = SD.numSectors();
  if (sectors == 0 || sectors > UINT32_MAX) return false;

  uint8_t sectorZero[512] = {};
  if (!SD.readRAW(sectorZero, 0)) return false;
  const uint32_t partitionStart =
      sd_card_identity::partitionStartSector(sectorZero, sizeof(sectorZero));
  uint8_t volumeBoot[512] = {};
  const uint8_t* volumeBootData = sectorZero;
  if (partitionStart > 0 && partitionStart < sectors) {
    if (!SD.readRAW(volumeBoot, partitionStart)) return false;
    volumeBootData = volumeBoot;
  }
  identity = sd_card_identity::identify(
      static_cast<uint32_t>(sectors), sectorZero, sizeof(sectorZero),
      volumeBootData);
  return identity.valid();
}

void detectReaderFonts() {
  if (!sdCardReady) {
    readerCjkFont16Available = false;
    readerCjkFont24Available = false;
    readerLatinFont16Available = false;
    readerLatinFont24Available = false;
    return;
  }
  readerCjkFont16Available = sd_card::fileExists(kReaderCjkFont16Path);
  readerCjkFont24Available = sd_card::fileExists(kReaderCjkFont24Path);
  readerLatinFont16Available = sd_card::fileExists(kReaderLatinFont16Path);
  readerLatinFont24Available = sd_card::fileExists(kReaderLatinFont24Path);
  LOG.printf(
      "[games] reader fonts: CJK 16=%s 24=%s, Latin 16=%s 24=%s\n",
      readerCjkFont16Available ? "yes" : "no",
      readerCjkFont24Available ? "yes" : "no",
      readerLatinFont16Available ? "yes" : "no",
      readerLatinFont24Available ? "yes" : "no");
}

enum class ReaderStorageStatus {
  Ready,
  Changed,
  Missing,
};

void clearReaderStorageState() {
  epubArchive.close();
  readerChapterText.clear();
  readerChapterRequiresNonAscii = false;
  readerChapterRequiresCjk = false;
  readerBookPath = "";
  readerFolderCoverPath = "";
  readerPageStart = 0;
  readerChapterIndex = 0;
  readerCoverVisible = false;
  readerBrowserPath = "/";
  browserPageStart = 0;
  browserMessage = "";
  readerCjkFont16Available = false;
  readerCjkFont24Available = false;
  readerLatinFont16Available = false;
  readerLatinFont24Available = false;
}

ReaderStorageStatus refreshReaderStorage() {
  if (!sdCardInserted()) {
    clearReaderStorageState();
    if (sdCardReady) SD.end();
    sdCardReady = false;
    return ReaderStorageStatus::Missing;
  }

  bool remounted = false;
  if (!sdCardReady) {
    epubArchive.close();
    sdCardReady =
        sd_card::mount(epaper.getSPIinstance(), "/sticky-arcade");
    if (!sdCardReady) {
      clearReaderStorageState();
      return ReaderStorageStatus::Missing;
    }
    remounted = true;
  }

  sd_card_identity::Identity currentIdentity;
  if (!readReaderCardIdentity(currentIdentity)) {
    clearReaderStorageState();
    SD.end();
    sdCardReady = false;
    return ReaderStorageStatus::Missing;
  }

  const bool changed =
      readerCardIdentity.valid() &&
      !sd_card_identity::same(readerCardIdentity, currentIdentity);
  if (changed) {
    LOG.println("[games] SD card changed; resetting EPUB reader state");
    clearReaderStorageState();
  }
  readerCardIdentity = currentIdentity;
  if (changed || remounted) detectReaderFonts();
  return changed ? ReaderStorageStatus::Changed
                 : ReaderStorageStatus::Ready;
}

bool loadReaderFont(int pixelSize, bool cjkRequired) {
  if (cjkRequired) {
    if (pixelSize >= 24 && readerCjkFont24Available) {
      epaper.loadFont(kReaderCjkFont24Name, SD);
      return true;
    }
    if (readerCjkFont16Available) {
      epaper.loadFont(kReaderCjkFont16Name, SD);
      return true;
    }
    epaper.loadFont(smoothFontFor(pixelSize));
    return false;
  }

  if (pixelSize >= 24 && readerLatinFont24Available) {
    epaper.loadFont(kReaderLatinFont24Name, SD);
  } else if (readerLatinFont16Available) {
    epaper.loadFont(kReaderLatinFont16Name, SD);
  } else {
    epaper.loadFont(smoothFontFor(pixelSize));
  }
  return true;
}

bool browserPageRequiresCjk() {
  if (epub_text::containsCjk(readerBrowserPath.c_str(),
                             readerBrowserPath.length())) {
    return true;
  }
  for (int row = 0; row < kBrowserRowsPerPage; ++row) {
    const int itemIndex = browserPageStart + row;
    if (epub_browser_logic::isParentFolderItem(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str()))) {
      const char* name = tr(TextId::UpFolder);
      if (epub_text::containsCjk(name, strlen(name))) return true;
      continue;
    }
    const SdReadonlyBrowser::Entry* entry =
        sdBrowser.entry(epub_browser_logic::storedEntryIndex(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str())));
    if (entry != nullptr &&
        epub_text::containsCjk(entry->name.c_str(), entry->name.length())) {
      return true;
    }
  }
  return false;
}

bool browserPageRequiresNonAscii() {
  if (containsNonAscii(readerBrowserPath)) return true;
  for (int row = 0; row < kBrowserRowsPerPage; ++row) {
    const int itemIndex = browserPageStart + row;
    if (epub_browser_logic::isParentFolderItem(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str()))) {
      const char* name = tr(TextId::UpFolder);
      if (containsNonAscii(name, strlen(name))) return true;
      continue;
    }
    const SdReadonlyBrowser::Entry* entry =
        sdBrowser.entry(epub_browser_logic::storedEntryIndex(
            itemIndex,
            epub_browser_logic::hasParentFolder(readerBrowserPath.c_str())));
    if (entry != nullptr && containsNonAscii(entry->name)) return true;
  }
  return false;
}

int browserItemCount() {
  return epub_browser_logic::itemCount(
      sdBrowser.count(),
      epub_browser_logic::hasParentFolder(readerBrowserPath.c_str()));
}

bool hasPreviousBrowserPage() { return browserPageStart > 0; }

bool hasNextBrowserPage() {
  return browserPageStart + kBrowserRowsPerPage < browserItemCount();
}

void drawBrowserFolderIcon(int x, int y) {
  epaper.fillRect(x + 2, y + 8, 44, 31, TFT_BLACK);
  epaper.fillRect(x + 7, y + 2, 19, 10, TFT_BLACK);
  epaper.fillRect(x + 6, y + 13, 36, 22, TFT_WHITE);
}

void drawBrowserEntryIcon(const SdReadonlyBrowser::Entry& entry, int x,
                          int y) {
  if (entry.directory) {
    drawBrowserFolderIcon(x, y);
    return;
  }
  epaper.drawRect(x + 7, y, 34, 44, TFT_BLACK);
  epaper.fillTriangle(x + 30, y, x + 41, y, x + 41, y + 11, TFT_BLACK);
  if (entry.epub) {
    epaper.fillRect(x + 12, y + 18, 24, 4, TFT_BLACK);
    epaper.fillRect(x + 12, y + 28, 24, 4, TFT_BLACK);
  } else {
    epaper.drawLine(x + 13, y + 17, x + 35, y + 37, TFT_BLACK);
    epaper.drawLine(x + 35, y + 17, x + 13, y + 37, TFT_BLACK);
  }
}

void drawBrowserParentFolderIcon(int x, int y) {
  drawBrowserFolderIcon(x, y);
  epaper.fillTriangle(x + 24, y + 15, x + 15, y + 26, x + 33, y + 26,
                      TFT_BLACK);
  epaper.fillRect(x + 21, y + 25, 7, 9, TFT_BLACK);
}

void drawEpubBrowser() {
  epaper.fillSprite(TFT_WHITE);
  drawGameStatusBar(tr(TextId::EpubReader));
  if (!sdCardReady) {
    drawCentered(tr(TextId::SdCardRequired), kScreenWidth / 2, 320, 4);
    drawCentered(tr(TextId::InsertSdCard), kScreenWidth / 2, 380, 4);
    return;
  }

  const bool smoothBrowserFont = browserPageRequiresNonAscii();
  const bool browserFontReady =
      !smoothBrowserFont || loadReaderFont(24, browserPageRequiresCjk());
  const int builtInFont = smoothBrowserFont ? 0 : 4;
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  drawReaderString(
      fitReaderText(readerBrowserPath, kScreenWidth - 36, builtInFont),
      kScreenWidth / 2, 72, builtInFont);
  const bool hasParentFolder =
      epub_browser_logic::hasParentFolder(readerBrowserPath.c_str());
  const int itemCount = browserItemCount();
  if (itemCount == 0) {
    if (smoothBrowserFont) epaper.unloadFont();
    drawCentered(tr(TextId::EmptyFolder), kScreenWidth / 2, 350, 4);
  } else {
    browserPageStart =
        std::max(0, std::min(browserPageStart,
                             std::max(0, itemCount - 1)));
    epaper.setTextDatum(ML_DATUM);
    for (int row = 0; row < kBrowserRowsPerPage; ++row) {
      const int index = browserPageStart + row;
      if (index >= itemCount) break;
      const int top = kBrowserRowTop + row * kBrowserRowHeight;
      epaper.drawFastHLine(18, top + kBrowserRowHeight - 1,
                          kScreenWidth - 36, TFT_BLACK);
      String name;
      if (epub_browser_logic::isParentFolderItem(index, hasParentFolder)) {
        drawBrowserParentFolderIcon(22, top + 16);
        name = tr(TextId::UpFolder);
      } else {
        const SdReadonlyBrowser::Entry* entry =
            sdBrowser.entry(epub_browser_logic::storedEntryIndex(
                index, hasParentFolder));
        if (entry == nullptr) break;
        drawBrowserEntryIcon(*entry, 22, top + 16);
        name = entry->name;
      }
      name = fitReaderText(name, 365, builtInFont);
      drawReaderString(name, 82, top + kBrowserRowHeight / 2, builtInFont);
    }
    if (smoothBrowserFont) epaper.unloadFont();
  }

  String footer = browserMessage;
  if (footer.isEmpty() && !browserFontReady) {
    footer = tr(TextId::CjkFontRequired);
  }
  if (sdBrowser.truncated()) {
    if (!footer.isEmpty()) footer += " - ";
    footer += "96+";
  }
  if (!footer.isEmpty()) drawCentered(footer, kScreenWidth / 2, 776, 2);
  if (hasPreviousBrowserPage()) {
    drawArrowButton(kPreviousPageButton, false);
  }
  if (hasNextBrowserPage()) {
    drawArrowButton(kNextPageButton, true);
  }
}

epub_text::TextPage currentReaderPage() {
  return epub_text::paginate(
      readerChapterText.c_str(), readerChapterText.length(), readerPageStart,
      kReaderTextWidth, kReaderLinesPerPage, epub_text::TextStyle::Regular,
      false, readerCharacterWidth);
}

uint32_t readerPageNumber() {
  uint32_t pageNumber = 1;
  size_t offset = 0;
  epub_text::TextStyle style = epub_text::TextStyle::Regular;
  while (offset < readerPageStart) {
    const epub_text::TextPage page = epub_text::paginate(
        readerChapterText.c_str(), readerChapterText.length(), offset,
        kReaderTextWidth, kReaderLinesPerPage, style, true,
        readerCharacterWidth);
    if (page.end <= offset || page.end > readerPageStart) break;
    offset = page.end;
    style = page.finalStyle;
    ++pageNumber;
  }
  return pageNumber;
}

bool readerHasCover() {
  return epubArchive.hasCover() || !readerFolderCoverPath.isEmpty();
}

String findReaderFolderCoverPath(const String& bookPath) {
  constexpr const char* kCoverNames[] = {"cover.png", "cover.jpg"};
  for (const char* coverName : kCoverNames) {
    const String candidate =
        epub_cover::folderCoverPath(bookPath.c_str(), coverName).c_str();
    if (!sd_card::fileExists(candidate)) continue;

    File cover = sd_card::openForRead(candidate);
    if (!cover) {
      LOG.printf("[games] could not open folder cover %s\n",
                 candidate.c_str());
      continue;
    }
    const size_t length = cover.size();
    cover.close();
    if (length == 0 || length > EpubArchive::kMaximumCoverBytes) {
      LOG.printf("[games] ignoring folder cover %s (%lu bytes)\n",
                 candidate.c_str(), static_cast<unsigned long>(length));
      continue;
    }
    return candidate;
  }
  return "";
}

bool loadReaderFolderCoverImage(RgbImage& image) {
  if (readerFolderCoverPath.isEmpty() || !sdCardInserted()) return false;

  // Avoid a recovery remount while the EPUB archive has an open File handle.
  File cover = SD.open(readerFolderCoverPath, FILE_READ);
  if (!cover) {
    LOG.printf("[games] could not reopen folder cover %s\n",
               readerFolderCoverPath.c_str());
    return false;
  }
  const size_t length = cover.size();
  if (length == 0 || length > EpubArchive::kMaximumCoverBytes) {
    cover.close();
    LOG.printf("[games] refusing changed folder cover %s (%lu bytes)\n",
               readerFolderCoverPath.c_str(),
               static_cast<unsigned long>(length));
    return false;
  }

  uint8_t* data = static_cast<uint8_t*>(ps_malloc(length));
  if (data == nullptr) data = static_cast<uint8_t*>(malloc(length));
  if (data == nullptr) {
    cover.close();
    LOG.println("[games] folder cover allocation failed");
    return false;
  }
  size_t bytesRead = 0;
  while (bytesRead < length) {
    const size_t chunk = cover.read(data + bytesRead, length - bytesRead);
    if (chunk == 0) break;
    bytesRead += chunk;
  }
  cover.close();
  if (bytesRead != length) {
    free(data);
    LOG.println("[games] folder cover read failed");
    return false;
  }

  const bool decoded =
      load_image_from_memory(data, length, readerFolderCoverPath.c_str(), 0, 0,
                             &image);
  free(data);
  return decoded;
}

bool renderEpubCoverImage() {
  RgbImage image;
  bool decoded = false;
  if (epubArchive.hasCover()) {
    EpubCoverData cover;
    if (!epubArchive.loadCover(cover)) {
      LOG.printf("[games] could not extract EPUB cover: %s\n",
                 epubArchive.error().c_str());
      return false;
    }
    LOG.printf("[games] decoding EPUB cover %s (%lu bytes)\n",
               cover.nameHint().c_str(),
               static_cast<unsigned long>(cover.length()));
    decoded = load_image_from_memory(cover.data(), cover.length(),
                                     cover.nameHint().c_str(), 0, 0, &image);
    cover.clear();
  } else if (!readerFolderCoverPath.isEmpty()) {
    LOG.printf("[games] decoding folder cover %s\n",
               readerFolderCoverPath.c_str());
    decoded = loadReaderFolderCoverImage(image);
  } else {
    return false;
  }

  if (!decoded || image.width <= 0 || image.height <= 0) {
    image_free(&image);
    LOG.println("[games] EPUB cover decode failed");
    return false;
  }

  const int sourceWidth = image.width;
  const int sourceHeight = image.height;
  const float scale =
      std::min(static_cast<float>(kReaderCoverMaximumWidth) / sourceWidth,
               static_cast<float>(kReaderCoverMaximumHeight) / sourceHeight);
  const int targetWidth =
      std::max(1, std::min(kReaderCoverMaximumWidth,
                           static_cast<int>(sourceWidth * scale)));
  const int targetHeight =
      std::max(1, std::min(kReaderCoverMaximumHeight,
                           static_cast<int>(sourceHeight * scale)));
  const size_t pixelCount =
      static_cast<size_t>(targetWidth) * targetHeight;
  uint8_t* indices = static_cast<uint8_t*>(ps_malloc(pixelCount));
  if (indices == nullptr) indices = static_cast<uint8_t*>(malloc(pixelCount));
  if (indices == nullptr) {
    image_free(&image);
    LOG.println("[games] EPUB cover index allocation failed");
    return false;
  }

  const bool dithered = dither_resized_image(
      image.pixels, sourceWidth, sourceHeight, targetWidth, targetHeight,
      PAL_BW, 1.0f, false, indices);
  image_free(&image);
  if (!dithered) {
    free(indices);
    LOG.println("[games] EPUB cover dithering failed");
    return false;
  }

  const size_t packedBytes =
      static_cast<size_t>((targetWidth + 7) / 8) * targetHeight;
  uint8_t* packed = static_cast<uint8_t*>(ps_malloc(packedBytes));
  if (packed == nullptr) packed = static_cast<uint8_t*>(malloc(packedBytes));
  if (packed == nullptr) {
    free(indices);
    LOG.println("[games] EPUB cover bitmap allocation failed");
    return false;
  }
  pack_1bpp_msb(indices, packed, targetWidth, targetHeight, true);
  free(indices);

  const int targetX = (kScreenWidth - targetWidth) / 2;
  const int targetY =
      kReaderCoverTop + (kReaderCoverMaximumHeight - targetHeight) / 2;
  epaper.drawBitmap(targetX, targetY, packed, targetWidth, targetHeight,
                    TFT_BLACK, TFT_WHITE);
  free(packed);
  LOG.printf("[games] EPUB cover rendered %dx%d -> %dx%d\n", sourceWidth,
             sourceHeight, targetWidth, targetHeight);
  return true;
}

void drawEpubCover() {
  epaper.fillSprite(TFT_WHITE);
  if (!renderEpubCoverImage()) {
    constexpr int kFallbackLeft = 100;
    constexpr int kFallbackTop = 180;
    constexpr int kFallbackWidth = 280;
    constexpr int kFallbackHeight = 400;
    epaper.drawRoundRect(kFallbackLeft, kFallbackTop, kFallbackWidth,
                        kFallbackHeight, 10, TFT_BLACK);
    epaper.drawRoundRect(kFallbackLeft + 2, kFallbackTop + 2,
                        kFallbackWidth - 4, kFallbackHeight - 4, 8, TFT_BLACK);
    drawCentered("EPUB", kScreenWidth / 2,
                 kFallbackTop + kFallbackHeight / 2, 4);
  }
  drawGameStatusBar(tr(TextId::EpubReader));
}

void drawEpubReading() {
  if (readerCoverVisible) {
    drawEpubCover();
    return;
  }
  epaper.fillSprite(TFT_WHITE);
  const epub_text::TextPage page = currentReaderPage();
  const bool bodyUsesEmbeddedLatin = pageUsesEmbeddedReaderFont(page);
  const bool bodyRequiresCjk = pageRequiresCjk(page);
  const bool titleUsesEmbeddedLatin = usesEmbeddedReaderFont(
      epubArchive.title().c_str(), epubArchive.title().length());
  const bool titleRequiresCjk =
      epub_text::containsCjk(epubArchive.title().c_str(),
                             epubArchive.title().length());
  epaper.setTextColor(TFT_BLACK, TFT_WHITE, true);
  epaper.setTextDatum(MC_DATUM);

  bool bodyFontReady = true;
  bool titleFontReady = true;
  bool embeddedFontLoaded = false;
  epub_text::TextStyle loadedStyle = epub_text::TextStyle::Regular;
  if (!bodyUsesEmbeddedLatin) {
    bodyFontReady = loadReaderFont(24, bodyRequiresCjk || titleRequiresCjk);
    titleFontReady = !titleRequiresCjk || bodyFontReady;
  } else if (titleUsesEmbeddedLatin) {
    epaper.loadFont(epub_latin_fonts::kRegular);
    embeddedFontLoaded = true;
  } else {
    titleFontReady = loadReaderFont(24, titleRequiresCjk);
  }
  if (titleFontReady) {
    drawReaderString(
        fitReaderText(epubArchive.title(), kScreenWidth - 36),
        kScreenWidth / 2, 76);
  }

  epaper.setTextDatum(TL_DATUM);
  epub_text::TextStyle style = page.initialStyle;
  if (bodyFontReady) {
    if (bodyUsesEmbeddedLatin && !titleUsesEmbeddedLatin) {
      epaper.unloadFont();
      embeddedFontLoaded = false;
    }
    for (size_t index = 0; index < page.lines.size(); ++index) {
      drawStyledReaderLine(
          page.lines[index], 18,
          104 + static_cast<int>(index) * kReaderLineHeight, style,
          bodyUsesEmbeddedLatin, loadedStyle, embeddedFontLoaded,
          index < page.justifyLines.size() && page.justifyLines[index]);
    }
  }
  epaper.unloadFont();
  if ((bodyRequiresCjk && !bodyFontReady) ||
      (titleRequiresCjk && !titleFontReady)) {
    drawCentered(tr(TextId::CjkFontRequired), kScreenWidth / 2, 360, 4);
  }

  const String location =
      String(tr(TextId::Chapter)) + " " + String(readerChapterIndex + 1) +
      " / " + String(epubArchive.chapterCount()) + "   " +
      tr(TextId::Page) + " " + String(readerPageNumber());
  drawCentered(location, kScreenWidth / 2, 776, 2);
  drawGameStatusBar(tr(TextId::EpubReader));
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

bool save2048HighScore() {
  const uint32_t candidate = game2048.bestScore();
  if (candidate <= stored2048HighScore) return true;

  const game_progress::HighScoreSaveResult result =
      game_progress::saveHighScore(k2048HighScoreKey, candidate);
  if (result.status != game_progress::Status::Ok) {
    LOG.printf("[games] could not save 2048 high score: %s\n",
               game_progress::statusMessage(result.status));
    return false;
  }

  stored2048HighScore = result.score;
  LOG.printf("[games] saved 2048 high score: %lu\n",
             static_cast<unsigned long>(stored2048HighScore));
  return true;
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

void startNewDotsAndBoxes() {
  dotsAndBoxes.start();
  dotsAndBoxesSaved = true;
}

void startNewSokoban() {
  const uint16_t level =
      sokobanCompletedLevelCount < SokobanGame::kLevelCount
          ? sokobanCompletedLevelCount
          : SokobanGame::kLevelCount - 1;
  sokoban.start(level);
  sokobanSaved = true;
  sokobanProgressSaveFailed = false;
}

bool saveSokobanCompletion() {
  const uint16_t completedLevelCount = sokoban.levelIndex() + 1;
  if (completedLevelCount <= sokobanCompletedLevelCount) return true;

  const game_progress::SaveResult result =
      game_progress::saveHighestCheckpoint(
          kSokobanProgressKey, completedLevelCount,
          SokobanGame::kLevelCount + 1);
  if (result.status != game_progress::Status::Ok) {
    sokobanProgressSaveFailed = true;
    LOG.printf("[games] could not save Sokoban completion: %s\n",
               game_progress::statusMessage(result.status));
    return false;
  }

  sokobanCompletedLevelCount = result.checkpoint;
  sokobanProgressSaveFailed = false;
  LOG.printf("[games] saved Sokoban completion: %u/%u levels\n",
             static_cast<unsigned>(sokobanCompletedLevelCount),
             static_cast<unsigned>(SokobanGame::kLevelCount));
  return true;
}

void openNextUnfinishedSokobanLevel() {
  if (sokobanCompletedLevelCount >= SokobanGame::kLevelCount) return;
  if (sokoban.levelIndex() != sokobanCompletedLevelCount) {
    sokoban.start(sokobanCompletedLevelCount);
    sokobanProgressSaveFailed = false;
  }
}

void startNewPegSolitaire() {
  pegSolitaire.start();
  pegSolitaireSaved = true;
}

void startNewSlitherlink() {
  slitherlink.start(0);
  slitherlinkSaved = true;
}

void startNewSudoku() {
  uint8_t puzzleIndex = esp_random() % SudokuGame::kPuzzleCount;
  if (sudokuSaved && puzzleIndex == sudoku.puzzleIndex()) {
    puzzleIndex = (puzzleIndex + 1U) % SudokuGame::kPuzzleCount;
  }
  sudoku.start(puzzleIndex);
  sudokuSaved = true;
}

void startNewCrossword() {
  uint8_t puzzleIndex = esp_random() % CrosswordGame::kPuzzleCount;
  if (crosswordSaved && puzzleIndex == crossword.puzzleIndex()) {
    puzzleIndex = (puzzleIndex + 1U) % CrosswordGame::kPuzzleCount;
  }
  crossword.start(puzzleIndex);
  crosswordSaved = true;
  crosswordKeyboardVisible = false;
}

void startNewKlondike() {
  klondike.start(esp_random());
  klondikeDoubleTap.clear();
  klondikeSaved = true;
}

void startNewMahjong() {
  mahjong.start(esp_random());
  mahjongSaved = true;
}

void startNewFallingBlocks() {
  fallingBlocks.start(esp_random());
  fallingBlocksSaved = true;
}

void startNewConnectFour() {
  connectFour.start();
  connectFourSaved = true;
}

void startNewWordSearch() {
  wordSearch.start(esp_random());
  wordSearchSaved = true;
}

bool listReaderBrowserPath(const String& path, bool resetPage) {
  const int savedPageStart = resetPage ? 0 : browserPageStart;
  readerBrowserPath = path.isEmpty() ? "/" : path;
  if (!sdCardReady) return false;
  const uint32_t startedAtMs = millis();
  if (sdBrowser.open(readerBrowserPath)) {
    browserPageStart = savedPageStart;
    LOG.printf("[games] listed %s: %d entries in %lu ms%s\n",
               readerBrowserPath.c_str(), sdBrowser.count(),
               static_cast<unsigned long>(millis() - startedAtMs),
               sdBrowser.truncated() ? " (truncated)" : "");
    return true;
  }
  LOG.printf("[games] could not open SD directory: %s\n",
             readerBrowserPath.c_str());
  readerBrowserPath = "/";
  browserPageStart = 0;
  browserMessage = tr(TextId::OpenFailed);
  if (sdBrowser.open(readerBrowserPath)) return true;

  clearReaderStorageState();
  SD.end();
  sdCardReady = false;
  return false;
}

bool openReaderBrowserPath(const String& path) {
  epubArchive.close();
  readerChapterText.clear();
  readerChapterRequiresNonAscii = false;
  readerChapterRequiresCjk = false;
  readerBookPath = "";
  readerFolderCoverPath = "";
  readerPageStart = 0;
  readerChapterIndex = 0;
  readerCoverVisible = false;
  browserMessage = "";
  return listReaderBrowserPath(path, true);
}

bool loadReaderChapter(int chapter, size_t pageStart = 0) {
  EpubChapterText text;
  if (!epubArchive.loadChapter(chapter, text)) return false;
  readerChapterText = std::move(text);
  readerChapterRequiresNonAscii =
      containsNonAscii(readerChapterText.c_str(),
                       readerChapterText.length());
  readerChapterRequiresCjk =
      epub_text::containsCjk(readerChapterText.c_str(),
                             readerChapterText.length());
  readerChapterIndex = chapter;
  readerPageStart =
      pageStart < readerChapterText.length() ? pageStart : 0;
  readerCoverVisible = false;
  return true;
}

bool openReaderBook(const String& path, int chapter = 0,
                    size_t pageStart = 0, bool skipUnreadable = true,
                    bool showCover = true) {
  readerChapterText.clear();
  readerChapterRequiresNonAscii = false;
  readerChapterRequiresCjk = false;
  readerCoverVisible = false;
  readerFolderCoverPath = "";
  LOG.printf(
      "[games] opening EPUB %s (heap=%lu KiB, PSRAM=%lu KiB)\n",
      path.c_str(), static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
      static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  if (!sdCardReady) {
    LOG.println("[games] cannot open EPUB without an SD card");
    return false;
  }
  detectReaderFonts();
  const String folderCoverPath = findReaderFolderCoverPath(path);
  if (!epubArchive.open(path)) {
    LOG.printf("[games] could not open EPUB %s: %s\n", path.c_str(),
               epubArchive.error().c_str());
    return false;
  }
  LOG.printf("[games] EPUB archive ready: %d spine items\n",
             epubArchive.chapterCount());
  const int firstChapter =
      std::max(0, std::min(chapter, epubArchive.chapterCount() - 1));
  const int lastChapter =
      skipUnreadable ? epubArchive.chapterCount() : firstChapter + 1;
  for (int index = firstChapter; index < lastChapter; ++index) {
    if (!loadReaderChapter(index, index == firstChapter ? pageStart : 0)) {
      LOG.printf("[games] skipping EPUB chapter %d: %s\n", index + 1,
                 epubArchive.error().c_str());
      continue;
    }
    readerBookPath = path;
    if (!epubArchive.hasCover()) {
      readerFolderCoverPath = folderCoverPath;
    }
    readerCoverVisible = showCover && readerHasCover();
    LOG.printf(
        "[games] EPUB ready: chapter=%d bytes=%lu cover=%s heap=%lu KiB "
        "PSRAM=%lu KiB\n",
        readerChapterIndex + 1,
        static_cast<unsigned long>(readerChapterText.length()),
        readerCoverVisible ? "yes" : "no",
        static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
        static_cast<unsigned long>(ESP.getFreePsram() / 1024));
    return true;
  }
  const String savedError = epubArchive.error();
  epubArchive.close();
  LOG.printf("[games] EPUB has no readable chapter: %s\n",
             savedError.c_str());
  return false;
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

void playConnectFourComputerTurn() {
  if (connectFourMode != ReversiMode::SinglePlayer ||
      connectFour.currentPlayer() != ConnectFourGame::Disc::Yellow ||
      connectFour.gameOver()) {
    return;
  }
  int column = -1;
  if (!connectFour.chooseBestColumn(kConnectFourAiDepth, column)) {
    LOG.println("[games] Connect Four AI could not find a legal move");
    return;
  }
  LOG.printf("[games] Connect Four AI plays column=%d\n", column);
  if (!connectFour.drop(column)) {
    LOG.println("[games] Connect Four AI selected an illegal move");
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

void drawCurrentScreen();

void toggleHelpPane() {
  helpPaneVisible = !helpPaneVisible;
  if (helpPaneVisible) {
    drawHelpPane();
    refreshScreen("help pane opened");
  } else {
    drawCurrentScreen();
    refreshScreen("help pane closed");
  }
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case Screen::Menu:
      drawMenu();
      return;
    case Screen::LightsOut:
      drawLightsOut();
      return;
    case Screen::Game2048:
      draw2048();
      return;
    case Screen::PipeConnect:
      drawPipeConnect();
      return;
    case Screen::Minesweeper:
      drawMinesweeper();
      return;
    case Screen::Nonogram:
      drawNonogram();
      return;
    case Screen::Reversi:
      drawReversi();
      return;
    case Screen::DotsAndBoxes:
      drawDotsAndBoxes();
      return;
    case Screen::Sokoban:
      drawSokoban();
      return;
    case Screen::PegSolitaire:
      drawPegSolitaire();
      return;
    case Screen::Slitherlink:
      drawSlitherlink();
      return;
    case Screen::Sudoku:
      drawSudoku();
      return;
    case Screen::Crossword:
      drawCrossword();
      return;
    case Screen::Klondike:
      drawKlondike();
      return;
    case Screen::MahjongSolitaire:
      drawMahjong();
      return;
    case Screen::FallingBlocks:
      drawFallingBlocks();
      return;
    case Screen::ConnectFour:
      drawConnectFour();
      return;
    case Screen::WordSearch:
      drawWordSearch();
      return;
    case Screen::EpubBrowser:
      drawEpubBrowser();
      return;
    case Screen::EpubReading:
      drawEpubReading();
      return;
  }
}

void showLanguageSelection() {
  saveResumeState();
  helpPaneVisible = false;
  languageSelectionVisible = true;
  drawLanguageSelection();
  refreshScreen("language selection");
}

void selectLanguage(Language language) {
  const game_language_store::Status status =
      game_language_store::save(language);
  if (status != game_language_store::Status::Ok) {
    LOG.printf("[games] could not save language: %s\n",
               game_language_store::statusMessage(status));
    return;
  }

  currentLanguage = language;
  languageSelected = true;
  languageSelectionVisible = false;
  LOG.printf("[games] language selected: %s\n",
             game_localization::languageName(language));
  drawCurrentScreen();
  refreshScreen("language changed");
}

void showMenu() {
  const GameId returningGame = gameForScreen(currentScreen);
  helpPaneVisible = false;
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
  } else if (currentScreen == Screen::DotsAndBoxes && dotsAndBoxesSaved) {
    LOG.println("[games] auto-saving Dots and Boxes");
  } else if (currentScreen == Screen::Sokoban && sokobanSaved) {
    LOG.println("[games] auto-saving Sokoban");
  } else if (currentScreen == Screen::PegSolitaire && pegSolitaireSaved) {
    LOG.println("[games] auto-saving Peg Solitaire");
  } else if (currentScreen == Screen::Slitherlink && slitherlinkSaved) {
    LOG.println("[games] auto-saving Slitherlink");
  } else if (currentScreen == Screen::Sudoku && sudokuSaved) {
    LOG.println("[games] auto-saving Sudoku");
  } else if (currentScreen == Screen::Crossword && crosswordSaved) {
    LOG.println("[games] auto-saving Crossword");
  } else if (currentScreen == Screen::Klondike && klondikeSaved) {
    LOG.println("[games] auto-saving Klondike");
  } else if (currentScreen == Screen::MahjongSolitaire && mahjongSaved) {
    LOG.println("[games] auto-saving Mahjong Solitaire");
  } else if (currentScreen == Screen::FallingBlocks && fallingBlocksSaved) {
    LOG.println("[games] auto-saving Falling Blocks");
  } else if (currentScreen == Screen::ConnectFour && connectFourSaved) {
    LOG.println("[games] auto-saving Connect Four");
  } else if (currentScreen == Screen::WordSearch && wordSearchSaved) {
    LOG.println("[games] auto-saving Word Search");
  } else if (currentScreen == Screen::EpubReading) {
    LOG.println("[games] saving EPUB reading position");
  }
  if (currentScreen == Screen::EpubBrowser ||
      currentScreen == Screen::EpubReading) {
    epubArchive.close();
    readerChapterText.clear();
    readerChapterRequiresNonAscii = false;
    readerChapterRequiresCjk = false;
  }
  if (returningGame != GameId::Count) {
    currentMenuPage = menuPageForGame(returningGame);
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

void showDotsAndBoxes() {
  if (!dotsAndBoxesSaved) startNewDotsAndBoxes();
  currentScreen = Screen::DotsAndBoxes;
  drawDotsAndBoxes();
  refreshScreen("Dots and Boxes screen");
}

void showSokoban() {
  if (!sokobanSaved) startNewSokoban();
  openNextUnfinishedSokobanLevel();
  currentScreen = Screen::Sokoban;
  drawSokoban();
  refreshScreen("Sokoban screen");
}

void showPegSolitaire() {
  if (!pegSolitaireSaved) startNewPegSolitaire();
  currentScreen = Screen::PegSolitaire;
  drawPegSolitaire();
  refreshScreen("Peg Solitaire screen");
}

void showSlitherlink() {
  if (!slitherlinkSaved) startNewSlitherlink();
  currentScreen = Screen::Slitherlink;
  drawSlitherlink();
  refreshScreen("Slitherlink screen");
}

void showSudoku() {
  if (!sudokuSaved) startNewSudoku();
  currentScreen = Screen::Sudoku;
  drawSudoku();
  refreshScreen("Sudoku screen");
}

void showCrossword() {
  if (!crosswordSaved) startNewCrossword();
  crosswordKeyboardVisible = false;
  currentScreen = Screen::Crossword;
  drawCrossword();
  refreshScreen("Crossword screen");
}

void showKlondike() {
  if (!klondikeSaved) startNewKlondike();
  currentScreen = Screen::Klondike;
  drawKlondike();
  refreshScreen("Klondike screen");
}

void showMahjong() {
  if (!mahjongSaved) startNewMahjong();
  currentScreen = Screen::MahjongSolitaire;
  drawMahjong();
  refreshScreen("Mahjong Solitaire screen");
}

void showFallingBlocks() {
  if (!fallingBlocksSaved) startNewFallingBlocks();
  currentScreen = Screen::FallingBlocks;
  drawFallingBlocks();
  refreshScreen("Falling Blocks screen");
}

void showConnectFour() {
  if (!connectFourSaved) startNewConnectFour();
  playConnectFourComputerTurn();
  currentScreen = Screen::ConnectFour;
  drawConnectFour();
  refreshScreen("Connect Four screen");
}

void showWordSearch() {
  if (!wordSearchSaved) startNewWordSearch();
  currentScreen = Screen::WordSearch;
  drawWordSearch();
  refreshScreen("Word Search screen");
}

void showEpubBrowser(bool fullRefresh = true) {
  currentScreen = Screen::EpubBrowser;
  const ReaderStorageStatus storage = refreshReaderStorage();
  if (storage != ReaderStorageStatus::Missing) {
    openReaderBrowserPath(readerBrowserPath);
  }
  saveResumeState();
  drawEpubBrowser();
  if (fullRefresh) {
    refreshScreen("EPUB browser");
  } else {
    refreshRegion(kReaderRegion, "EPUB browser");
  }
}

void showEpubReading(bool fullRefresh = false) {
  currentScreen = Screen::EpubReading;
  saveResumeState();
  drawEpubReading();
  if (readerCoverVisible || fullRefresh) {
    refreshScreen(readerCoverVisible ? "EPUB cover" : "EPUB first text page");
  } else {
    refreshRegion(kReaderRegion, "EPUB page");
  }
}

bool prepareEpubBrowserInteraction() {
  const ReaderStorageStatus storage = refreshReaderStorage();
  if (storage == ReaderStorageStatus::Ready &&
      listReaderBrowserPath(readerBrowserPath, false)) {
    return true;
  }
  if (storage == ReaderStorageStatus::Changed) {
    openReaderBrowserPath("/");
  }
  currentScreen = Screen::EpubBrowser;
  saveResumeState();
  drawEpubBrowser();
  refreshScreen(storage == ReaderStorageStatus::Changed
                    ? "EPUB SD card changed"
                    : "EPUB SD card unavailable");
  return false;
}

bool prepareEpubReadingInteraction() {
  const ReaderStorageStatus storage = refreshReaderStorage();
  if (storage == ReaderStorageStatus::Ready && epubArchive.isOpen()) {
    return true;
  }
  showEpubBrowser();
  return false;
}

void showPreviousBrowserPage(bool validateStorage = true) {
  if (validateStorage && !prepareEpubBrowserInteraction()) return;
  if (!hasPreviousBrowserPage()) {
    LOG.println("[games] already on first EPUB browser page");
    return;
  }
  browserPageStart = std::max(0, browserPageStart - kBrowserRowsPerPage);
  browserMessage = "";
  drawEpubBrowser();
  refreshRegion(kReaderRegion, "EPUB previous files");
}

void showNextBrowserPage(bool validateStorage = true) {
  if (validateStorage && !prepareEpubBrowserInteraction()) return;
  if (!hasNextBrowserPage()) {
    LOG.println("[games] already on last EPUB browser page");
    return;
  }
  browserPageStart += kBrowserRowsPerPage;
  browserMessage = "";
  drawEpubBrowser();
  refreshRegion(kReaderRegion, "EPUB next files");
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
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, reversiModeLabel());
  refreshRegion(kReversiModeRegion, action);
}

void updateDotsAndBoxes(const char* action) {
  drawDotsAndBoxesBoard();
  refreshRegion(kDotsBoardRegion, action);
}

void updateSokoban(const char* action) {
  drawSokobanBoard();
  drawSokobanControls();
  refreshRegion(kSokobanBoardRegion, action);
}

void updatePegSolitaire(const char* action) {
  drawPegSolitaireBoard();
  refreshRegion(kPegBoardRegion, action);
}

void updateSlitherlink(const char* action) {
  drawSlitherlinkBoard();
  refreshRegion(kSlitherlinkBoardRegion, action);
}

void updateSudoku(const char* action) {
  drawSudokuBoard();
  refreshRegion(kSudokuBoardRegion, action);
}

void updateCrossword(const char* action) {
  drawCrosswordBoard();
  drawCrosswordControls();
  refreshRegion(kCrosswordBoardRegion, action);
}

void updateKlondike(const char* action) {
  drawKlondikeBoard();
  refreshRegion(kKlondikeBoardRegion, action);
}

void updateMahjong(const char* action) {
  drawMahjongBoard();
  refreshRegion(kMahjongBoardRegion, action);
}

void updateFallingBlocks(const char* action) {
  drawFallingBlocksBoard();
  refreshRegion(kFallingBlocksBoardRegion, action);
}

void updateConnectFour(const char* action) {
  drawConnectFourBoard();
  refreshRegion(kConnectFourBoardRegion, action);
}

void updateConnectFourMode(const char* action) {
  drawConnectFourBoard();
  drawButton(kNewButton, tr(TextId::NewGame));
  drawButton(kResetButton, connectFourModeLabel());
  refreshRegion(kConnectFourModeRegion, action);
}

void updateWordSearch(const char* action) {
  drawWordSearchBoard();
  refreshRegion(kWordSearchBoardRegion, action);
}

void launchGame(GameId game) {
  hardware::beep();
  switch (game) {
    case GameId::LightsOut:
      showLightsOut();
      break;
    case GameId::Game2048:
      show2048();
      break;
    case GameId::PipeConnect:
      showPipeConnect();
      break;
    case GameId::Minesweeper:
      showMinesweeper();
      break;
    case GameId::Nonogram:
      showNonogram();
      break;
    case GameId::Reversi:
      showReversi();
      break;
    case GameId::DotsAndBoxes:
      showDotsAndBoxes();
      break;
    case GameId::Sokoban:
      showSokoban();
      break;
    case GameId::PegSolitaire:
      showPegSolitaire();
      break;
    case GameId::Slitherlink:
      showSlitherlink();
      break;
    case GameId::Sudoku:
      showSudoku();
      break;
    case GameId::Crossword:
      showCrossword();
      break;
    case GameId::Klondike:
      showKlondike();
      break;
    case GameId::MahjongSolitaire:
      showMahjong();
      break;
    case GameId::FallingBlocks:
      showFallingBlocks();
      break;
    case GameId::EpubReader:
      showEpubBrowser();
      break;
    case GameId::ConnectFour:
      showConnectFour();
      break;
    case GameId::WordSearch:
      showWordSearch();
      break;
    case GameId::Count:
      return;
  }
  saveResumeState();
}

void showMenuPage(MenuPage page, bool beep = true) {
  if (beep) hardware::beep();
  currentMenuPage = page;
  saveResumeState();
  drawMenu();
  refreshScreen("menu page");
}

void handleLanguageTouch(const Gt911Touch::Point& point) {
  for (size_t index = 0; index < game_localization::kLanguageCount; ++index) {
    if (!kLanguageButtons[index].contains(point.x, point.y)) continue;
    hardware::beep();
    selectLanguage(static_cast<Language>(index));
    return;
  }
}

void handleMenuTouch(const Gt911Touch::Point& point) {
  if (kSettingsButton.contains(point.x, point.y)) {
    hardware::beep();
    showLanguageSelection();
    return;
  }
  const size_t pageIndex = static_cast<size_t>(currentMenuPage);
  if (pageIndex + 1 < kMenuPageCount &&
      kNextPageButton.contains(point.x, point.y)) {
    showMenuPage(static_cast<MenuPage>(pageIndex + 1));
    return;
  }
  if (pageIndex > 0 &&
      kPreviousPageButton.contains(point.x, point.y)) {
    showMenuPage(static_cast<MenuPage>(pageIndex - 1));
    return;
  }
  const size_t firstPosition = pageIndex * kGamesPerMenuPage;
  const size_t visibleGames =
      std::min(kGamesPerMenuPage, kGameCount - firstPosition);
  for (size_t slot = 0; slot < visibleGames; ++slot) {
    if (kMenuCardSlots[slot].contains(point.x, point.y)) {
      launchGame(orderedGameAt(firstPosition + slot));
      return;
    }
  }
}

bool handleMenuEdgeSwipe(const Gt911Touch::Point& start,
                         const Gt911Touch::Point& end) {
  const menu_edge_swipe::Direction direction = menu_edge_swipe::detect(
      start.x, start.y, end.x, end.y, kScreenWidth, kMenuSwipeEdgeWidth,
      kSwipeThreshold);
  const size_t pageIndex = static_cast<size_t>(currentMenuPage);
  if (direction == menu_edge_swipe::Direction::Previous) {
    if (pageIndex > 0) {
      showMenuPage(static_cast<MenuPage>(pageIndex - 1));
    } else {
      LOG.println("[games] already on first menu page");
    }
    return true;
  }
  if (direction == menu_edge_swipe::Direction::Next) {
    if (pageIndex + 1 < kMenuPageCount) {
      showMenuPage(static_cast<MenuPage>(pageIndex + 1));
    } else {
      LOG.println("[games] already on last menu page");
    }
    return true;
  }
  return false;
}

void handleLightsOutTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewPuzzle();
    updateLightsOut("new puzzle");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
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
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewPipeConnect();
    updatePipeConnect("new Pipe Connect game");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
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
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewNonogram();
    updateNonogram("new Nonogram puzzle");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
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
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewReversi();
    updateReversi("new Reversi game");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
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
                  ? tr(TextId::WhitePassesBlackAgain)
                  : tr(TextId::BlackPassesWhiteAgain)
            : nullptr;
    updateReversi(result == ReversiGame::MoveResult::GameOver
                       ? "Reversi game over"
                       : humanOpponentPassed ? "Reversi pass"
                                             : "Reversi move",
                   passStatus);
  }
}

int absoluteDistance(int first, int second) {
  const int difference = first - second;
  return difference < 0 ? -difference : difference;
}

void handleDotsAndBoxesTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewDotsAndBoxes();
    updateDotsAndBoxes("new Dots and Boxes game");
    return;
  }

  constexpr int kEdgeTolerance = 20;
  const int localX = point.x - kDotsGridLeft;
  const int localY = point.y - kDotsGridTop;
  if (localX < -kEdgeTolerance ||
      localX > kDotsGridSize + kEdgeTolerance ||
      localY < -kEdgeTolerance ||
      localY > kDotsGridSize + kEdgeTolerance) {
    return;
  }

  const int horizontalRow = (localY + kDotsSpacing / 2) / kDotsSpacing;
  const int horizontalColumn =
      localX >= 0 ? localX / kDotsSpacing : -1;
  const int verticalColumn = (localX + kDotsSpacing / 2) / kDotsSpacing;
  const int verticalRow = localY >= 0 ? localY / kDotsSpacing : -1;
  const int horizontalDistance =
      absoluteDistance(localY, horizontalRow * kDotsSpacing);
  const int verticalDistance =
      absoluteDistance(localX, verticalColumn * kDotsSpacing);
  const bool horizontalCandidate =
      horizontalRow >= 0 &&
      horizontalRow <= DotsAndBoxesGame::kBoxSize &&
      horizontalColumn >= 0 &&
      horizontalColumn < DotsAndBoxesGame::kBoxSize &&
      localX < kDotsGridSize && horizontalDistance <= kEdgeTolerance;
  const bool verticalCandidate =
      verticalRow >= 0 && verticalRow < DotsAndBoxesGame::kBoxSize &&
      verticalColumn >= 0 &&
      verticalColumn <= DotsAndBoxesGame::kBoxSize &&
      localY < kDotsGridSize && verticalDistance <= kEdgeTolerance;

  bool moved = false;
  if (horizontalCandidate &&
      (!verticalCandidate || horizontalDistance <= verticalDistance)) {
    moved = dotsAndBoxes.placeHorizontal(horizontalRow, horizontalColumn);
  } else if (verticalCandidate) {
    moved = dotsAndBoxes.placeVertical(verticalRow, verticalColumn);
  }
  if (moved) updateDotsAndBoxes("Dots and Boxes edge");
}

void handleSokobanTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (sokobanCompletedLevelCount == SokobanGame::kLevelCount) return;
  if (kCenteredNewButton.contains(point.x, point.y)) {
    hardware::beep();
    if (sokoban.solved()) {
      if (!saveSokobanCompletion()) {
        updateSokoban("retry Sokoban progress save");
        return;
      }
      if (sokobanCompletedLevelCount < SokobanGame::kLevelCount) {
        sokoban.start(sokobanCompletedLevelCount);
      }
      updateSokoban("next Sokoban level");
    } else {
      sokoban.reset();
      sokobanProgressSaveFailed = false;
      updateSokoban("restart Sokoban level");
    }
    return;
  }

  const int cellSize = sokobanCellSize();
  const int gridLeft = sokobanGridLeft();
  const int gridWidth = sokoban.width() * cellSize;
  const int gridHeight = sokoban.height() * cellSize;
  if (point.x < gridLeft || point.x >= gridLeft + gridWidth ||
      point.y < kSokobanGridTop ||
      point.y >= kSokobanGridTop + gridHeight) {
    return;
  }
  const int playerCenterX =
      gridLeft + sokoban.playerColumn() * cellSize + cellSize / 2;
  const int playerCenterY =
      kSokobanGridTop + sokoban.playerRow() * cellSize + cellSize / 2;
  const int horizontalDistance = point.x - playerCenterX;
  const int verticalDistance = point.y - playerCenterY;
  const int absoluteHorizontal =
      horizontalDistance < 0 ? -horizontalDistance : horizontalDistance;
  const int absoluteVertical =
      verticalDistance < 0 ? -verticalDistance : verticalDistance;
  if (absoluteHorizontal < cellSize / 2 &&
      absoluteVertical < cellSize / 2) {
    return;
  }

  SokobanGame::Direction direction;
  if (absoluteHorizontal > absoluteVertical) {
    direction = horizontalDistance < 0 ? SokobanGame::Direction::Left
                                       : SokobanGame::Direction::Right;
  } else {
    direction = verticalDistance < 0 ? SokobanGame::Direction::Up
                                     : SokobanGame::Direction::Down;
  }
  if (sokoban.move(direction)) {
    if (sokoban.solved()) saveSokobanCompletion();
    updateSokoban("Sokoban move");
  }
}

void handlePegSolitaireTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    hardware::beep();
    pegSolitaire.reset();
    updatePegSolitaire("reset Peg Solitaire");
    return;
  }
  if (point.x < kPegGridLeft ||
      point.x >= kPegGridLeft + kPegGridSize ||
      point.y < kPegGridTop ||
      point.y >= kPegGridTop + kPegGridSize) {
    return;
  }
  const int column = (point.x - kPegGridLeft) / kPegCellSize;
  const int row = (point.y - kPegGridTop) / kPegCellSize;
  const PegSolitaireGame::TapResult result = pegSolitaire.tap(row, column);
  if (result != PegSolitaireGame::TapResult::InvalidCell &&
      result != PegSolitaireGame::TapResult::NoSelection) {
    updatePegSolitaire(result == PegSolitaireGame::TapResult::Moved
                           ? "Peg Solitaire move"
                           : "Peg Solitaire selection");
  }
}

void handleSlitherlinkTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    slitherlink.nextPuzzle();
    slitherlinkSaved = true;
    updateSlitherlink("next Slitherlink puzzle");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
    slitherlink.reset();
    updateSlitherlink("reset Slitherlink puzzle");
    return;
  }

  constexpr int kEdgeTolerance = 18;
  const int localX = point.x - kSlitherlinkGridLeft;
  const int localY = point.y - kSlitherlinkGridTop;
  if (localX < -kEdgeTolerance ||
      localX > kSlitherlinkGridSize + kEdgeTolerance ||
      localY < -kEdgeTolerance ||
      localY > kSlitherlinkGridSize + kEdgeTolerance) {
    return;
  }

  const int horizontalRow =
      (localY + kSlitherlinkCellSize / 2) / kSlitherlinkCellSize;
  const int horizontalColumn =
      localX >= 0 ? localX / kSlitherlinkCellSize : -1;
  const int verticalColumn =
      (localX + kSlitherlinkCellSize / 2) / kSlitherlinkCellSize;
  const int verticalRow =
      localY >= 0 ? localY / kSlitherlinkCellSize : -1;
  const int horizontalDistance =
      absoluteDistance(localY, horizontalRow * kSlitherlinkCellSize);
  const int verticalDistance =
      absoluteDistance(localX, verticalColumn * kSlitherlinkCellSize);
  const bool horizontalCandidate =
      horizontalRow >= 0 && horizontalRow <= SlitherlinkGame::kSize &&
      horizontalColumn >= 0 &&
      horizontalColumn < SlitherlinkGame::kSize &&
      localX < kSlitherlinkGridSize &&
      horizontalDistance <= kEdgeTolerance;
  const bool verticalCandidate =
      verticalRow >= 0 && verticalRow < SlitherlinkGame::kSize &&
      verticalColumn >= 0 &&
      verticalColumn <= SlitherlinkGame::kSize &&
      localY < kSlitherlinkGridSize &&
      verticalDistance <= kEdgeTolerance;

  bool changed = false;
  if (horizontalCandidate &&
      (!verticalCandidate || horizontalDistance <= verticalDistance)) {
    changed = slitherlink.cycleHorizontal(horizontalRow, horizontalColumn);
  } else if (verticalCandidate) {
    changed = slitherlink.cycleVertical(verticalRow, verticalColumn);
  }
  if (changed) updateSlitherlink("Slitherlink edge");
}

void handleSudokuTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewSudoku();
    updateSudoku("new Sudoku puzzle");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
    sudoku.reset();
    updateSudoku("reset Sudoku puzzle");
    return;
  }
  if (point.x >= kSudokuGridLeft &&
      point.x < kSudokuGridLeft + kSudokuGridSize &&
      point.y >= kSudokuGridTop &&
      point.y < kSudokuGridTop + kSudokuGridSize) {
    const int column = (point.x - kSudokuGridLeft) / kSudokuCellSize;
    const int row = (point.y - kSudokuGridTop) / kSudokuCellSize;
    if (sudoku.select(row, column)) updateSudoku("Sudoku cell selection");
    return;
  }
  for (int key = 0; key < 10; ++key) {
    if (!sudokuKeyRect(key).contains(point.x, point.y)) continue;
    hardware::beep();
    if (sudoku.setDigit(key < 9 ? static_cast<uint8_t>(key + 1) : 0)) {
      updateSudoku(key < 9 ? "Sudoku digit" : "clear Sudoku cell");
    }
    return;
  }
}

void handleCrosswordTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (crosswordKeyboardVisible) {
    const int keyCounts[3] = {10, 9, 9};
    for (int row = 0; row < 3; ++row) {
      for (int key = 0; key < keyCounts[row]; ++key) {
        if (!crosswordKeyboardKeyRect(row, key).contains(point.x, point.y)) {
          continue;
        }
        hardware::beep();
        if (row == 2 && key == 8) {
          crosswordKeyboardVisible = false;
          updateCrossword("close Crossword keyboard");
        } else if (row == 2 && key == 7) {
          if (crossword.erase()) updateCrossword("erase Crossword letter");
        } else {
          const char letter = crosswordKeyboardLabel(row, key)[0];
          if (crossword.setLetter(letter)) {
            if (crossword.solved()) crosswordKeyboardVisible = false;
            updateCrossword("Crossword letter");
          }
        }
        return;
      }
    }
  } else {
    if (kNewButton.contains(point.x, point.y)) {
      hardware::beep();
      startNewCrossword();
      updateCrossword("new Crossword puzzle");
      return;
    }
    if (kResetButton.contains(point.x, point.y)) {
      hardware::beep();
      crossword.reset();
      updateCrossword("reset Crossword puzzle");
      return;
    }
  }

  const int cellSize = crosswordCellSize();
  const int left = crosswordGridLeft();
  const int gridWidth = crossword.width() * cellSize;
  const int gridHeight = crossword.height() * cellSize;
  if (point.x < left || point.x >= left + gridWidth ||
      point.y < kCrosswordGridTop ||
      point.y >= kCrosswordGridTop + gridHeight) {
    return;
  }
  const int column = (point.x - left) / cellSize;
  const int row = (point.y - kCrosswordGridTop) / cellSize;
  if (crossword.select(row, column)) {
    crosswordKeyboardVisible = true;
    updateCrossword("Crossword cell selection");
  }
}

void handleKlondikeTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    klondikeDoubleTap.clear();
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewKlondike();
    updateKlondike("new Klondike deal");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
    klondike.reset();
    klondikeDoubleTap.clear();
    updateKlondike("reset Klondike deal");
    return;
  }
  if (kKlondikeStockSlot.contains(point.x, point.y)) {
    klondikeDoubleTap.clear();
    const KlondikeGame::StockResult result = klondike.drawStock();
    if (result != KlondikeGame::StockResult::NoChange) {
      updateKlondike(result == KlondikeGame::StockResult::Drawn
                         ? "draw Klondike stock"
                         : "recycle Klondike stock");
    }
    return;
  }
  if (kKlondikeWasteSlot.contains(point.x, point.y)) {
    if (klondikeDoubleTap.registerTap(kKlondikeWasteTapTarget, millis(),
                                     kKlondikeDoubleTapMs)) {
      if (klondike.moveSelectionToMatchingFoundation()) {
        updateKlondike("double-tap Klondike foundation move");
      }
      return;
    }
    if (klondike.selectWaste()) updateKlondike("select Klondike waste");
    return;
  }
  for (int suit = 0; suit < KlondikeGame::kSuitCount; ++suit) {
    if (!klondikeFoundationSlot(suit).contains(point.x, point.y)) continue;
    klondikeDoubleTap.clear();
    if (klondike.hasSelection()) {
      if (klondike.moveSelectionToFoundation(suit)) {
        updateKlondike("Klondike foundation move");
      }
    } else if (klondike.selectFoundation(suit)) {
      updateKlondike("select Klondike foundation");
    }
    return;
  }

  if (point.x < kKlondikeGridLeft ||
      point.x >= kKlondikeGridLeft +
                     KlondikeGame::kTableauCount * kKlondikeColumnStep ||
      point.y < kKlondikeTableauTop || point.y >= 676) {
    return;
  }
  const int column = (point.x - kKlondikeGridLeft) / kKlondikeColumnStep;
  const int columnLeft = kKlondikeGridLeft + column * kKlondikeColumnStep;
  if (point.x >= columnLeft + kKlondikeCardWidth) return;
  const int count = klondike.tableauCount(column);
  int cardIndex = -1;
  for (int index = count - 1; index >= 0; --index) {
    const Rect card = {columnLeft, klondikeTableauCardY(column, index),
                       kKlondikeCardWidth, kKlondikeCardHeight};
    if (card.contains(point.x, point.y)) {
      cardIndex = index;
      break;
    }
  }
  if (klondike.hasSelection() &&
      klondike.moveSelectionToTableau(column)) {
    klondikeDoubleTap.clear();
    updateKlondike("Klondike tableau move");
    return;
  }
  if (cardIndex >= 0 && cardIndex == count - 1 &&
      klondikeDoubleTap.registerTap(
          static_cast<uint16_t>(kKlondikeTableauTapTarget |
                                (column << 6) | cardIndex),
          millis(), kKlondikeDoubleTapMs)) {
    if (klondike.moveSelectionToMatchingFoundation()) {
      updateKlondike("double-tap Klondike foundation move");
    }
    return;
  }
  if (cardIndex < 0 || cardIndex != count - 1) klondikeDoubleTap.clear();
  if (cardIndex >= 0 && klondike.selectTableau(column, cardIndex)) {
    updateKlondike("select Klondike tableau");
  }
}

void handleMahjongTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewMahjong();
    updateMahjong("new Mahjong Solitaire layout");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
    mahjong.reset();
    updateMahjong("reset Mahjong Solitaire");
    return;
  }

  for (int layer = 3; layer >= 0; --layer) {
    for (int index = MahjongSolitaireGame::kTileCount - 1; index >= 0;
         --index) {
      if (!mahjong.occupied(index) ||
          MahjongSolitaireGame::position(index).layer != layer ||
          !mahjongTileRect(index).contains(point.x, point.y)) {
        continue;
      }
      const MahjongSolitaireGame::TapResult result = mahjong.tap(index);
      if (result != MahjongSolitaireGame::TapResult::NoChange &&
          result != MahjongSolitaireGame::TapResult::Blocked) {
        updateMahjong(result == MahjongSolitaireGame::TapResult::Removed
                          ? "Mahjong Solitaire pair"
                          : result == MahjongSolitaireGame::TapResult::Won
                                ? "Mahjong Solitaire solved"
                                : "Mahjong Solitaire selection");
      }
      return;
    }
  }
}

bool handleFallingBlocksTouchStart(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return true;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewFallingBlocks();
    updateFallingBlocks("new Falling Blocks game");
    return true;
  }
  return point.x < kFallingGridLeft ||
         point.x >= kFallingGridLeft + kFallingGridWidth ||
         point.y < kFallingGridTop ||
         point.y >= kFallingGridTop + kFallingGridHeight;
}

void handleFallingBlocksGesture(const Gt911Touch::Point& start,
                                const Gt911Touch::Point& end) {
  const int dx = static_cast<int>(end.x) - static_cast<int>(start.x);
  const int dy = static_cast<int>(end.y) - static_cast<int>(start.y);
  const int absX = dx < 0 ? -dx : dx;
  const int absY = dy < 0 ? -dy : dy;
  FallingBlocksGame::Action action =
      FallingBlocksGame::Action::RotateClockwise;
  const char* description = "rotate Falling Blocks piece";
  if (std::max(absX, absY) >= kSwipeThreshold) {
    if (absX > absY) {
      action = dx < 0 ? FallingBlocksGame::Action::MoveLeft
                      : FallingBlocksGame::Action::MoveRight;
      description =
          dx < 0 ? "move Falling Blocks left" : "move Falling Blocks right";
    } else if (dy < 0) {
      action = FallingBlocksGame::Action::HardDrop;
      description = "hard-drop Falling Blocks piece";
    } else {
      action = FallingBlocksGame::Action::SoftDrop;
      description = "soft-drop Falling Blocks piece";
    }
  }
  if (fallingBlocks.turn(action)) {
    updateFallingBlocks(description);
  }
}

size_t lastReaderPageStart() {
  size_t offset = 0;
  size_t last = 0;
  epub_text::TextStyle style = epub_text::TextStyle::Regular;
  while (offset < readerChapterText.length()) {
    const epub_text::TextPage page = epub_text::paginate(
        readerChapterText.c_str(), readerChapterText.length(), offset,
        kReaderTextWidth, kReaderLinesPerPage, style, true,
        readerCharacterWidth);
    if (page.end <= offset || page.end >= readerChapterText.length()) {
      return last;
    }
    last = page.end;
    offset = page.end;
    style = page.finalStyle;
  }
  return last;
}

void showPreviousReaderPage() {
  if (!prepareEpubReadingInteraction()) return;
  if (readerCoverVisible) {
    LOG.println("[games] already on EPUB cover");
    return;
  }
  if (readerPageStart > 0) {
    readerPageStart = epub_text::previousPageStart(
        readerChapterText.c_str(), readerChapterText.length(), readerPageStart,
        kReaderTextWidth, kReaderLinesPerPage, readerCharacterWidth);
    showEpubReading();
    return;
  }
  for (int chapter = readerChapterIndex - 1; chapter >= 0; --chapter) {
    if (!loadReaderChapter(chapter)) continue;
    readerPageStart = lastReaderPageStart();
    showEpubReading();
    return;
  }
  if (readerHasCover()) {
    readerCoverVisible = true;
    showEpubReading();
  }
}

void showNextReaderPage() {
  if (!prepareEpubReadingInteraction()) return;
  if (readerCoverVisible) {
    readerCoverVisible = false;
    showEpubReading(true);
    return;
  }
  const epub_text::TextPage page = currentReaderPage();
  if (page.end < readerChapterText.length()) {
    readerPageStart = page.end;
    showEpubReading();
    return;
  }
  for (int chapter = readerChapterIndex + 1;
       chapter < epubArchive.chapterCount(); ++chapter) {
    if (!loadReaderChapter(chapter)) continue;
    showEpubReading();
    return;
  }
}

void handleEpubBrowserTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (!prepareEpubBrowserInteraction()) return;
  if (kPreviousPageButton.contains(point.x, point.y) &&
      hasPreviousBrowserPage()) {
    hardware::beep();
    showPreviousBrowserPage(false);
    return;
  }
  if (kNextPageButton.contains(point.x, point.y) && hasNextBrowserPage()) {
    hardware::beep();
    showNextBrowserPage(false);
    return;
  }
  if (point.y < kBrowserRowTop ||
      point.y >= kBrowserRowTop +
                     kBrowserRowsPerPage * kBrowserRowHeight) {
    return;
  }
  const int row = (point.y - kBrowserRowTop) / kBrowserRowHeight;
  const int itemIndex = browserPageStart + row;
  const bool hasParentFolder =
      epub_browser_logic::hasParentFolder(readerBrowserPath.c_str());
  if (epub_browser_logic::isParentFolderItem(itemIndex, hasParentFolder)) {
    hardware::beep();
    openReaderBrowserPath(SdReadonlyBrowser::parentPath(readerBrowserPath));
    drawEpubBrowser();
    refreshRegion(kReaderRegion, "EPUB parent folder");
    return;
  }
  const SdReadonlyBrowser::Entry* entry =
      sdBrowser.entry(
          epub_browser_logic::storedEntryIndex(itemIndex, hasParentFolder));
  if (entry == nullptr) return;
  if (entry->directory) {
    hardware::beep();
    openReaderBrowserPath(entry->path);
    drawEpubBrowser();
    refreshRegion(kReaderRegion, "EPUB folder");
    return;
  }
  if (!entry->epub) return;

  hardware::beep();
  browserMessage = "";
  if (openReaderBook(entry->path)) {
    showEpubReading();
  } else {
    browserMessage = tr(TextId::OpenFailed);
    drawEpubBrowser();
    refreshRegion(kReaderRegion, "EPUB open failed");
  }
}

void handleEpubReadingTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showEpubBrowser(false);
  }
}

bool handleMinesweeperTouchStart(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return true;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    hardware::beep();
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
    hardware::beep();
    showMenu();
    return true;
  }
  if (kCenteredNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNew2048();
    update2048("new 2048 game");
    return true;
  }
  return point.x < k2048GridLeft ||
         point.x >= k2048GridLeft + k2048GridSize ||
         point.y < k2048GridTop ||
         point.y >= k2048GridTop + k2048GridSize;
}

void handleConnectFourTouch(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewConnectFour();
    updateConnectFour("new Connect Four game");
    return;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
    connectFourMode = connectFourMode == ReversiMode::SinglePlayer
                          ? ReversiMode::TwoPlayer
                          : ReversiMode::SinglePlayer;
    startNewConnectFour();
    updateConnectFourMode("Connect Four mode");
    return;
  }
  if (point.x < kConnectFourGridLeft ||
      point.x >= kConnectFourGridLeft + kConnectFourGridWidth ||
      point.y < kConnectFourGridTop ||
      point.y >= kConnectFourGridTop + kConnectFourGridHeight ||
      (connectFourMode == ReversiMode::SinglePlayer &&
       connectFour.currentPlayer() != ConnectFourGame::Disc::Red)) {
    return;
  }
  const int column = (point.x - kConnectFourGridLeft) / kConnectFourCellSize;
  if (connectFour.drop(column)) {
    hardware::beep();
    playConnectFourComputerTurn();
    updateConnectFour(connectFour.gameOver() ? "Connect Four game over"
                                             : "Connect Four move");
  }
}

bool handleWordSearchTouchStart(const Gt911Touch::Point& point) {
  if (kBackButton.contains(point.x, point.y)) {
    hardware::beep();
    showMenu();
    return true;
  }
  if (kNewButton.contains(point.x, point.y)) {
    hardware::beep();
    startNewWordSearch();
    updateWordSearch("new Word Search puzzle");
    return true;
  }
  if (kResetButton.contains(point.x, point.y)) {
    hardware::beep();
    wordSearch.reset();
    updateWordSearch("reset Word Search puzzle");
    return true;
  }
  return point.x < kWordSearchGridLeft ||
         point.x >= kWordSearchGridLeft + kWordSearchGridSize ||
         point.y < kWordSearchGridTop ||
         point.y >= kWordSearchGridTop + kWordSearchGridSize;
}

void handleWordSearchSelection(const Gt911Touch::Point& start,
                               const Gt911Touch::Point& end) {
  if (end.x < kWordSearchGridLeft ||
      end.x >= kWordSearchGridLeft + kWordSearchGridSize ||
      end.y < kWordSearchGridTop ||
      end.y >= kWordSearchGridTop + kWordSearchGridSize) {
    return;
  }
  const int startColumn =
      (start.x - kWordSearchGridLeft) / kWordSearchCellSize;
  const int startRow = (start.y - kWordSearchGridTop) / kWordSearchCellSize;
  const int endColumn = (end.x - kWordSearchGridLeft) / kWordSearchCellSize;
  const int endRow = (end.y - kWordSearchGridTop) / kWordSearchCellSize;
  if (wordSearch.submit(startRow, startColumn, endRow, endColumn)) {
    hardware::beep();
    updateWordSearch(wordSearch.solved() ? "Word Search solved"
                                         : "Word Search word found");
  }
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
    save2048HighScore();
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
    if (touchActive && currentScreen == Screen::Menu &&
        !touchActionHandled) {
      if (!handleMenuEdgeSwipe(touchStart, touchLast)) {
        handleMenuTouch(touchStart);
      }
    } else if (touchActive && currentScreen == Screen::Game2048 &&
        !touchActionHandled) {
      handle2048Swipe(touchStart, touchLast);
    } else if (touchActive && currentScreen == Screen::FallingBlocks &&
               !touchActionHandled) {
      handleFallingBlocksGesture(touchStart, touchLast);
    } else if (touchActive && currentScreen == Screen::Minesweeper &&
               !touchActionHandled) {
      handleMinesweeperCellTouch(touchStart, touchLast,
                                 millis() - touchStartedAtMs);
    } else if (touchActive && currentScreen == Screen::WordSearch &&
               !touchActionHandled) {
      handleWordSearchSelection(touchStart, touchLast);
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
  if (languageSelectionVisible) {
    handleLanguageTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Menu) {
    if (!menu_edge_swipe::startsAtEdge(point.x, kScreenWidth,
                                       kMenuSwipeEdgeWidth)) {
      handleMenuTouch(point);
      touchActionHandled = true;
    }
  } else if (kHelpButton.contains(point.x, point.y)) {
    hardware::beep();
    toggleHelpPane();
    touchActionHandled = true;
  } else if (helpPaneVisible) {
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
  } else if (currentScreen == Screen::DotsAndBoxes) {
    handleDotsAndBoxesTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Sokoban) {
    handleSokobanTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::PegSolitaire) {
    handlePegSolitaireTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Slitherlink) {
    handleSlitherlinkTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Sudoku) {
    handleSudokuTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Crossword) {
    handleCrosswordTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::Klondike) {
    handleKlondikeTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::MahjongSolitaire) {
    handleMahjongTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::FallingBlocks) {
    touchActionHandled = handleFallingBlocksTouchStart(point);
  } else if (currentScreen == Screen::ConnectFour) {
    handleConnectFourTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::WordSearch) {
    touchActionHandled = handleWordSearchTouchStart(point);
  } else if (currentScreen == Screen::EpubBrowser) {
    handleEpubBrowserTouch(point);
    touchActionHandled = true;
  } else if (currentScreen == Screen::EpubReading) {
    handleEpubReadingTouch(point);
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

void handleShortOkPress() {
  if (helpPaneVisible) {
    hardware::beep();
    toggleHelpPane();
    return;
  }
  if (languageSelectionVisible) {
    LOG.println("[games] no back action on language selection");
    return;
  }
  if (currentScreen == Screen::Menu) {
    LOG.println("[games] no back action on game picker");
    return;
  }

  hardware::beep();
  if (currentScreen == Screen::EpubReading) {
    showEpubBrowser(false);
  } else {
    showMenu();
  }
}

void handleButton(const ButtonEvent& event) {
  ButtonState& button = *event.button;
  recordActivity();
  LOG.printf("[games] %s released after %lu ms\n", button.name,
             static_cast<unsigned long>(event.heldMs));

  if (button.pin == board::PIN_BUTTON_0) {
    switch (ok_button::actionForHold(event.heldMs)) {
      case ok_button::Action::ShortPress:
        handleShortOkPress();
        return;
      case ok_button::Action::DeepSleep:
        hardware::beep();
        powerDownAndSleep();
        return;
    }
  }

  hardware::beep();
  if (languageSelectionVisible) {
    LOG.println("[games] ignoring navigation button on language selection");
    return;
  }
  if (helpPaneVisible) {
    LOG.println("[games] ignoring navigation button while help is open");
    return;
  }
  if (button.pin == board::PIN_BUTTON_1) {
    if (currentScreen == Screen::FallingBlocks) {
      if (fallingBlocks.turn(
              FallingBlocksGame::Action::RotateCounterclockwise)) {
        updateFallingBlocks("rotate Falling Blocks counterclockwise");
      }
    } else if (currentScreen == Screen::EpubReading) {
      showPreviousReaderPage();
    } else if (currentScreen == Screen::EpubBrowser) {
      showPreviousBrowserPage();
    } else if (currentScreen != Screen::Menu) {
      showMenu();
    } else if (currentMenuPage != MenuPage::First) {
      showMenuPage(
          static_cast<MenuPage>(static_cast<uint8_t>(currentMenuPage) - 1),
          false);
    } else {
      LOG.println("[games] already on first menu page");
    }
    return;
  }
  if (button.pin == board::PIN_BUTTON_2) {
    if (currentScreen == Screen::FallingBlocks) {
      if (fallingBlocks.turn(FallingBlocksGame::Action::RotateClockwise)) {
        updateFallingBlocks("rotate Falling Blocks clockwise");
      }
    } else if (currentScreen == Screen::EpubReading) {
      showNextReaderPage();
    } else if (currentScreen == Screen::EpubBrowser) {
      showNextBrowserPage();
    } else if (currentScreen == Screen::Menu &&
        static_cast<size_t>(currentMenuPage) + 1 < kMenuPageCount) {
      showMenuPage(
          static_cast<MenuPage>(static_cast<uint8_t>(currentMenuPage) + 1),
          false);
    } else if (currentScreen == Screen::Menu) {
      LOG.println("[games] already on last menu page");
    } else {
      LOG.println("[games] ignoring DOWN while playing");
    }
    return;
  }
  if (button.pin != board::PIN_BUTTON_0) {
    LOG.printf("[games] ignoring %s button\n", button.name);
    return;
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

void handleReaderCardRemoval() {
  if ((currentScreen != Screen::EpubBrowser &&
       currentScreen != Screen::EpubReading) ||
      !sdCardReady || sdCardInserted()) {
    return;
  }

  LOG.println("[games] SD card removed; closing EPUB reader");
  clearReaderStorageState();
  SD.end();
  sdCardReady = false;
  helpPaneVisible = false;
  currentScreen = Screen::EpubBrowser;
  saveResumeState();
  drawEpubBrowser();
  refreshScreen("EPUB SD card removed");
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
  usbScreenCapture.begin(Serial1);
  delay(50);
  LOG.println();
  LOG.printf("[games] reTerminal E1005 %s\n", kAppName);
  hardware::beep();
  const game_language_store::LoadResult languageResult =
      game_language_store::load();
  if (languageResult.status == game_language_store::Status::Ok) {
    currentLanguage = languageResult.language;
    languageSelected = true;
    LOG.printf("[games] language: %s\n",
               game_localization::languageName(currentLanguage));
  } else {
    LOG.printf("[games] language selection required: %s\n",
               game_language_store::statusMessage(languageResult.status));
  }
  const game_progress::LoadResult sokobanProgress =
      game_progress::loadHighestCheckpoint(kSokobanProgressKey,
                                           SokobanGame::kLevelCount + 1);
  if (sokobanProgress.status == game_progress::Status::Ok) {
    sokobanCompletedLevelCount = sokobanProgress.checkpoint;
    LOG.printf("[games] Sokoban completed levels: %u/%u\n",
               static_cast<unsigned>(sokobanCompletedLevelCount),
               static_cast<unsigned>(SokobanGame::kLevelCount));
  } else {
    LOG.printf("[games] could not load Sokoban progress: %s\n",
               game_progress::statusMessage(sokobanProgress.status));
  }
  const game_progress::HighScoreLoadResult highScore =
      game_progress::loadHighScore(k2048HighScoreKey);
  if (highScore.status == game_progress::Status::Ok) {
    stored2048HighScore = highScore.score;
    LOG.printf("[games] 2048 high score: %lu\n",
               static_cast<unsigned long>(stored2048HighScore));
  } else {
    LOG.printf("[games] could not load 2048 high score: %s\n",
               game_progress::statusMessage(highScore.status));
  }
  const bool resumed = restoreResumeState();
  game2048.retainBestScore(stored2048HighScore);
  save2048HighScore();
  if (resumed && sokobanSaved) {
    openNextUnfinishedSokobanLevel();
    if (sokoban.solved()) saveSokobanCompletion();
  }
  LOG.printf("[games] boot mode: %s\n", resumed ? "resume" : "cold");

  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);

  epaper_setup::begin(epaper);
  checkBatteryAndSleepIfNeeded();
  sdCardReady = sd_card::mount(epaper.getSPIinstance(), "/sticky-arcade");
  if (sdCardReady && sd_ota::hasUpdate()) {
    drawStatus(tr(TextId::UpdatingFirmware), tr(TextId::DoNotPowerOff));
    epaper.update();
    const sd_ota::Result updateResult = sd_ota::apply();
    if (updateResult == sd_ota::Result::Applied) {
      delay(1000);
      ESP.restart();
    }
    drawStatus(tr(TextId::UpdateFailed), tr(TextId::CurrentFirmwareSafe));
    epaper.update();
    delay(2500);
  }
  const ReaderStorageStatus bootStorage = refreshReaderStorage();
  if (bootStorage != ReaderStorageStatus::Missing) detectReaderFonts();

  if (!resumed) currentScreen = Screen::Menu;
  if (resumed && currentScreen == Screen::Reversi) {
    playReversiComputerTurns();
  } else if (resumed && currentScreen == Screen::ConnectFour) {
    playConnectFourComputerTurn();
  }
  if (resumed && currentScreen == Screen::EpubReading &&
      bootStorage == ReaderStorageStatus::Ready) {
    const String savedBookPath = readerBookPath;
    const int savedChapter = readerChapterIndex;
    const size_t savedPageStart = readerPageStart;
    const bool savedCoverVisible = readerCoverVisible;
    if (!openReaderBook(savedBookPath, savedChapter, savedPageStart, false,
                        savedCoverVisible)) {
      currentScreen = Screen::EpubBrowser;
      openReaderBrowserPath(readerBrowserPath);
      browserMessage = tr(TextId::OpenFailed);
    }
  } else if (resumed && currentScreen == Screen::EpubBrowser) {
    if (bootStorage == ReaderStorageStatus::Missing) {
      clearReaderStorageState();
    } else {
      openReaderBrowserPath(readerBrowserPath);
    }
  } else if (resumed && currentScreen == Screen::EpubReading) {
    currentScreen = Screen::EpubBrowser;
    if (bootStorage == ReaderStorageStatus::Changed) {
      openReaderBrowserPath("/");
    } else {
      clearReaderStorageState();
    }
  }
  if (languageSelected) {
    drawCurrentScreen();
  } else {
    languageSelectionVisible = true;
    drawLanguageSelection();
  }
  LOG.printf("[games] refreshing %s\n",
             languageSelectionVisible ? "language selection"
                                      : screenName(currentScreen));
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
  usbScreenCapture.poll(epaper, kScreenWidth, kScreenHeight);
  handleReaderCardRemoval();
  checkBatteryAndSleepIfNeeded();
  pollTouch();
  ButtonEvent event = {};
  if (pollButtonEvent(event)) {
    handleButton(event);
  }
  sleepAfterInactivityIfNeeded();
  idleInLightSleep();
}
