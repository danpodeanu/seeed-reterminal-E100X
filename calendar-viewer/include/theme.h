#pragma once

#include <TFT_eSPI.h>

#include "board_pins.h"

namespace theme {

inline constexpr int PIN_BUTTON_GREEN = board::PIN_BUTTON_0;
inline constexpr int PIN_BUTTON_RIGHT = board::PIN_BUTTON_1;
inline constexpr int PIN_BUTTON_LEFT = board::PIN_BUTTON_2;

#if RETERMINAL_MODEL == 1001
inline constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
inline constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
inline constexpr uint32_t PANEL_LIGHT = TFT_GRAY_2;
inline constexpr uint32_t PANEL_MUTED = TFT_GRAY_1;
#elif RETERMINAL_MODEL == 1002
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
#elif RETERMINAL_MODEL == 1003
inline constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
inline constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
inline constexpr uint32_t PANEL_LIGHT = TFT_GRAY_12;
inline constexpr uint32_t PANEL_MUTED = TFT_GRAY_7;
#elif RETERMINAL_MODEL == 1004
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
#else
#error "Unsupported reTerminal model"
#endif

inline constexpr char PRIMARY_BUTTON_LABEL[] = "green";
inline constexpr char BUTTON_0_NAME[] = "GREEN";
inline constexpr char BUTTON_1_NAME[] = "RIGHT";
inline constexpr char BUTTON_2_NAME[] = "LEFT";
inline constexpr char PORTAL_EXIT_HINT[] =
    "Press green button to return to calendar";

}  // namespace theme
