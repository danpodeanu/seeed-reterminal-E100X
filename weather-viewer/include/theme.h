#pragma once

// Per-model theme constants for the weather viewer. Every place that would
// otherwise write `#if RETERMINAL_MODEL == XXXX` inline in main.cpp routes
// through this header instead so hardware-specific palette and layout
// values live in one file, and adding a new board is a matter of editing
// one enum arm rather than sprinkling ifdefs through the app.
//
// main.cpp includes this header and does `using namespace theme;` inside
// its anonymous namespace, so callers reference PANEL_WHITE / COLOR_SUN
// unchanged.

#include <stdint.h>

// TFT_eSPI defines the TFT_GRAY_N, TFT_WHITE, etc. constants; theme.h is
// only ever included by main.cpp, which pulls TFT_eSPI in earlier.
#include <TFT_eSPI.h>

#include "board_pins.h"

namespace theme {

// Historical aliases retained for existing weather call sites. On E1005
// these refer to OK / UP / DOWN on GPIO4 / GPIO5 / GPIO6.
inline constexpr int PIN_BUTTON_GREEN = board::PIN_BUTTON_0;
inline constexpr int PIN_BUTTON_RIGHT = board::PIN_BUTTON_1;
inline constexpr int PIN_BUTTON_LEFT = board::PIN_BUTTON_2;

#if RETERMINAL_MODEL == 1001
// UC8179 / Gray4
inline constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
inline constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
inline constexpr uint32_t PANEL_LIGHT = TFT_GRAY_2;
inline constexpr uint32_t PANEL_MUTED = TFT_GRAY_1;
inline constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_GRAY_3;
inline constexpr bool     PANEL_STATUS_DITHERED = true;
inline constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_GRAY_2;
inline constexpr uint8_t  PANEL_STATUS_DITHER_THRESHOLD = 8;
inline constexpr uint32_t COLOR_SUN = TFT_GRAY_0;
inline constexpr uint32_t COLOR_RAIN = TFT_GRAY_1;
inline constexpr uint32_t COLOR_ALERT = TFT_GRAY_0;
#elif RETERMINAL_MODEL == 1002
// ED2208 / six-color
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
inline constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
inline constexpr bool     PANEL_STATUS_DITHERED = true;
inline constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
inline constexpr uint8_t  PANEL_STATUS_DITHER_THRESHOLD = 4;
inline constexpr uint32_t COLOR_SUN = TFT_YELLOW;
inline constexpr uint32_t COLOR_RAIN = TFT_BLUE;
inline constexpr uint32_t COLOR_ALERT = TFT_RED;
#elif RETERMINAL_MODEL == 1003
// ED103TC2 / Gray16
inline constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
inline constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
inline constexpr uint32_t PANEL_LIGHT = TFT_GRAY_12;
inline constexpr uint32_t PANEL_MUTED = TFT_GRAY_6;
inline constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_GRAY_13;
inline constexpr bool     PANEL_STATUS_DITHERED = false;
inline constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_GRAY_13;
inline constexpr uint8_t  PANEL_STATUS_DITHER_THRESHOLD = 0;
inline constexpr uint32_t COLOR_SUN = TFT_GRAY_2;
inline constexpr uint32_t COLOR_RAIN = TFT_GRAY_5;
inline constexpr uint32_t COLOR_ALERT = TFT_GRAY_0;
#elif RETERMINAL_MODEL == 1004
// T133A01 / six-color
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
inline constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
inline constexpr bool     PANEL_STATUS_DITHERED = true;
inline constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
inline constexpr uint8_t  PANEL_STATUS_DITHER_THRESHOLD = 4;
inline constexpr uint32_t COLOR_SUN = TFT_YELLOW;
inline constexpr uint32_t COLOR_RAIN = TFT_BLUE;
inline constexpr uint32_t COLOR_ALERT = TFT_RED;
#elif RETERMINAL_MODEL == 1005
// SSD1677 / monochrome
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
inline constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
inline constexpr bool     PANEL_STATUS_DITHERED = false;
inline constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
inline constexpr uint8_t  PANEL_STATUS_DITHER_THRESHOLD = 0;
inline constexpr uint32_t COLOR_SUN = TFT_BLACK;
inline constexpr uint32_t COLOR_RAIN = TFT_BLACK;
inline constexpr uint32_t COLOR_ALERT = TFT_BLACK;
#else
#error "RETERMINAL_MODEL must be 1001, 1002, 1003, 1004, or 1005"
#endif

#if RETERMINAL_MODEL == 1005
inline constexpr char PRIMARY_BUTTON_LABEL[] = "OK";
inline constexpr char BUTTON_0_NAME[] = "OK";
inline constexpr char BUTTON_1_NAME[] = "UP";
inline constexpr char BUTTON_2_NAME[] = "DOWN";
inline constexpr char PORTAL_EXIT_HINT[] = "Press OK to return to weather";
#else
inline constexpr char PRIMARY_BUTTON_LABEL[] = "green";
inline constexpr char BUTTON_0_NAME[] = "GREEN";
inline constexpr char BUTTON_1_NAME[] = "RIGHT";
inline constexpr char BUTTON_2_NAME[] = "LEFT";
inline constexpr char PORTAL_EXIT_HINT[] =
    "Press green button to return to viewer";
#endif

}  // namespace theme
