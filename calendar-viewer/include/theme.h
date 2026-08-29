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
inline constexpr uint32_t PANEL_HEADER_BACKGROUND = TFT_GRAY_2;
inline constexpr uint32_t PANEL_ACCENT = TFT_GRAY_0;
inline constexpr uint32_t PANEL_ACCENT_TEXT = TFT_GRAY_3;
inline constexpr uint32_t PANEL_SECTION_HEADER = TFT_GRAY_1;
inline constexpr uint32_t PANEL_SECTION_HEADER_TEXT = TFT_GRAY_3;
inline constexpr uint32_t PANEL_WEATHER_HEADER = TFT_GRAY_2;
inline constexpr uint32_t PANEL_WEATHER_BACKGROUND = TFT_GRAY_3;
inline constexpr uint32_t PANEL_TODAY_BACKGROUND = TFT_GRAY_2;
inline constexpr uint32_t COLOR_SUN = TFT_GRAY_1;
inline constexpr uint32_t COLOR_RAIN = TFT_GRAY_0;
inline constexpr uint32_t COLOR_TEMPERATURE = TFT_GRAY_0;
inline constexpr uint32_t COLOR_HUMIDITY = TFT_GRAY_1;
inline constexpr uint32_t COLOR_ALERT = TFT_GRAY_0;
inline constexpr uint32_t COLOR_ALERT_TEXT = TFT_GRAY_3;
#elif RETERMINAL_MODEL == 1002
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
inline constexpr uint32_t PANEL_HEADER_BACKGROUND = TFT_YELLOW;
inline constexpr uint32_t PANEL_ACCENT = TFT_BLUE;
inline constexpr uint32_t PANEL_ACCENT_TEXT = TFT_WHITE;
inline constexpr uint32_t PANEL_SECTION_HEADER = TFT_BLUE;
inline constexpr uint32_t PANEL_SECTION_HEADER_TEXT = TFT_WHITE;
inline constexpr uint32_t PANEL_WEATHER_HEADER = TFT_YELLOW;
inline constexpr uint32_t PANEL_WEATHER_BACKGROUND = TFT_WHITE;
inline constexpr uint32_t PANEL_TODAY_BACKGROUND = TFT_YELLOW;
inline constexpr uint32_t COLOR_SUN = TFT_YELLOW;
inline constexpr uint32_t COLOR_RAIN = TFT_BLUE;
inline constexpr uint32_t COLOR_TEMPERATURE = TFT_RED;
inline constexpr uint32_t COLOR_HUMIDITY = TFT_BLUE;
inline constexpr uint32_t COLOR_ALERT = TFT_RED;
inline constexpr uint32_t COLOR_ALERT_TEXT = TFT_WHITE;
#elif RETERMINAL_MODEL == 1003
inline constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
inline constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
inline constexpr uint32_t PANEL_LIGHT = TFT_GRAY_12;
inline constexpr uint32_t PANEL_MUTED = TFT_GRAY_7;
inline constexpr uint32_t PANEL_HEADER_BACKGROUND = TFT_GRAY_12;
inline constexpr uint32_t PANEL_ACCENT = TFT_GRAY_2;
inline constexpr uint32_t PANEL_ACCENT_TEXT = TFT_GRAY_15;
inline constexpr uint32_t PANEL_SECTION_HEADER = TFT_GRAY_4;
inline constexpr uint32_t PANEL_SECTION_HEADER_TEXT = TFT_GRAY_15;
inline constexpr uint32_t PANEL_WEATHER_HEADER = TFT_GRAY_12;
inline constexpr uint32_t PANEL_WEATHER_BACKGROUND = TFT_GRAY_15;
inline constexpr uint32_t PANEL_TODAY_BACKGROUND = TFT_GRAY_12;
inline constexpr uint32_t COLOR_SUN = TFT_GRAY_3;
inline constexpr uint32_t COLOR_RAIN = TFT_GRAY_4;
inline constexpr uint32_t COLOR_TEMPERATURE = TFT_GRAY_0;
inline constexpr uint32_t COLOR_HUMIDITY = TFT_GRAY_5;
inline constexpr uint32_t COLOR_ALERT = TFT_GRAY_0;
inline constexpr uint32_t COLOR_ALERT_TEXT = TFT_GRAY_15;
#elif RETERMINAL_MODEL == 1004
inline constexpr uint32_t PANEL_WHITE = TFT_WHITE;
inline constexpr uint32_t PANEL_BLACK = TFT_BLACK;
inline constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
inline constexpr uint32_t PANEL_MUTED = TFT_BLACK;
inline constexpr uint32_t PANEL_HEADER_BACKGROUND = TFT_YELLOW;
inline constexpr uint32_t PANEL_ACCENT = TFT_BLUE;
inline constexpr uint32_t PANEL_ACCENT_TEXT = TFT_WHITE;
inline constexpr uint32_t PANEL_SECTION_HEADER = TFT_BLUE;
inline constexpr uint32_t PANEL_SECTION_HEADER_TEXT = TFT_WHITE;
inline constexpr uint32_t PANEL_WEATHER_HEADER = TFT_YELLOW;
inline constexpr uint32_t PANEL_WEATHER_BACKGROUND = TFT_WHITE;
inline constexpr uint32_t PANEL_TODAY_BACKGROUND = TFT_YELLOW;
inline constexpr uint32_t COLOR_SUN = TFT_YELLOW;
inline constexpr uint32_t COLOR_RAIN = TFT_BLUE;
inline constexpr uint32_t COLOR_TEMPERATURE = TFT_RED;
inline constexpr uint32_t COLOR_HUMIDITY = TFT_BLUE;
inline constexpr uint32_t COLOR_ALERT = TFT_RED;
inline constexpr uint32_t COLOR_ALERT_TEXT = TFT_WHITE;
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
