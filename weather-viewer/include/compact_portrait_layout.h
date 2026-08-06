#pragma once

namespace compact_portrait_layout {

inline constexpr int PANEL_WIDTH = 480;
inline constexpr int PANEL_HEIGHT = 800;
inline constexpr int HEADER_HEIGHT = 62;
inline constexpr int ALERT_TOP = HEADER_HEIGHT;
inline constexpr int ALERT_HEIGHT = 30;
inline constexpr int HEADER_SIDE_BADGE_WIDTH = 120;
inline constexpr int HEADER_LOCATION_WIDTH =
    PANEL_WIDTH - 2 * HEADER_SIDE_BADGE_WIDTH;
inline constexpr int HERO_TOP = 74;
inline constexpr int HERO_TOP_WITH_ALERT = 100;
inline constexpr int HERO_ICON_X = 125;
inline constexpr int HERO_ICON_SIZE = 150;
inline constexpr int HERO_TEMPERATURE_X = 342;
inline constexpr int FORECAST_TOP = 370;
inline constexpr int FOOTER_TOP = 770;
inline constexpr int FORECAST_DAYS = 3;
inline constexpr int FORECAST_ROW_HEIGHT =
    (FOOTER_TOP - FORECAST_TOP) / FORECAST_DAYS;

constexpr int heroTop(bool hasAlert) {
  return hasAlert ? HERO_TOP_WITH_ALERT : HERO_TOP;
}

constexpr int forecastRowTop(int index) {
  return FORECAST_TOP + index * FORECAST_ROW_HEIGHT;
}

constexpr int forecastRowsBottom() {
  return forecastRowTop(FORECAST_DAYS);
}

constexpr bool fitsPanel(int width, int height) {
  return width == PANEL_WIDTH && height == PANEL_HEIGHT &&
         ALERT_TOP + ALERT_HEIGHT <= HERO_TOP_WITH_ALERT &&
         HEADER_LOCATION_WIDTH > 0 &&
         HEADER_LOCATION_WIDTH <=
             PANEL_WIDTH - 2 * HEADER_SIDE_BADGE_WIDTH &&
         HERO_ICON_X - HERO_ICON_SIZE / 2 >= 0 &&
         HERO_ICON_X + HERO_ICON_SIZE / 2 < PANEL_WIDTH &&
         forecastRowsBottom() <= FOOTER_TOP &&
         FOOTER_TOP < PANEL_HEIGHT;
}

}  // namespace compact_portrait_layout
