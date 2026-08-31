#pragma once

namespace calendar_portrait_layout {

struct Rect {
  int left;
  int top;
  int width;
  int height;
};

inline constexpr int PANEL_WIDTH = 480;
inline constexpr int PANEL_HEIGHT = 800;
inline constexpr int HEADER_HEIGHT = 64;
inline constexpr int CONTENT_LEFT = 12;
inline constexpr int CONTENT_WIDTH = PANEL_WIDTH - CONTENT_LEFT * 2;
inline constexpr int CONTENT_TOP = 76;
inline constexpr int DIAGNOSTIC_TOP = 744;
inline constexpr int NAVIGATION_TOP = 764;
inline constexpr int NAVIGATION_HEIGHT = PANEL_HEIGHT - NAVIGATION_TOP;

inline constexpr Rect TODAY = {
    CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, 238};
inline constexpr Rect UPCOMING = {
    CONTENT_LEFT, 326, CONTENT_WIDTH, 238};
inline constexpr Rect WEATHER = {
    CONTENT_LEFT, 576, CONTENT_WIDTH, DIAGNOSTIC_TOP - 576};
inline constexpr Rect CALENDAR = {
    CONTENT_LEFT, CONTENT_TOP, CONTENT_WIDTH, DIAGNOSTIC_TOP - CONTENT_TOP};
inline constexpr int MONTH_HEADER_HEIGHT = 34;

constexpr Rect weekRow(int index) {
  const int top = CALENDAR.top + CALENDAR.height * index / 7;
  const int bottom = CALENDAR.top + CALENDAR.height * (index + 1) / 7;
  return {CALENDAR.left, top, CALENDAR.width, bottom - top};
}

constexpr Rect monthCell(int row, int column) {
  const int gridTop = CALENDAR.top + MONTH_HEADER_HEIGHT;
  const int gridHeight = CALENDAR.height - MONTH_HEADER_HEIGHT;
  const int left = CALENDAR.left + CALENDAR.width * column / 7;
  const int right = CALENDAR.left + CALENDAR.width * (column + 1) / 7;
  const int top = gridTop + gridHeight * row / 6;
  const int bottom = gridTop + gridHeight * (row + 1) / 6;
  return {left, top, right - left, bottom - top};
}

constexpr bool contains(const Rect& rect, int x, int y) {
  return x >= rect.left && x < rect.left + rect.width &&
         y >= rect.top && y < rect.top + rect.height;
}

constexpr int navigationIndexAt(int x, int y) {
  if (x < 0 || x >= PANEL_WIDTH || y < NAVIGATION_TOP ||
      y >= PANEL_HEIGHT) {
    return -1;
  }
  return x * 3 / PANEL_WIDTH;
}

constexpr int weekDayIndexAt(int x, int y) {
  if (!contains(CALENDAR, x, y)) return -1;
  return (y - CALENDAR.top) * 7 / CALENDAR.height;
}

constexpr int monthDayIndexAt(int x, int y) {
  const int gridTop = CALENDAR.top + MONTH_HEADER_HEIGHT;
  if (x < CALENDAR.left || x >= CALENDAR.left + CALENDAR.width ||
      y < gridTop || y >= CALENDAR.top + CALENDAR.height) {
    return -1;
  }
  const int column = (x - CALENDAR.left) * 7 / CALENDAR.width;
  const int row =
      (y - gridTop) * 6 / (CALENDAR.height - MONTH_HEADER_HEIGHT);
  return row * 7 + column;
}

constexpr bool insidePanel(const Rect& rect) {
  return rect.left >= 0 && rect.top >= 0 && rect.width > 0 &&
         rect.height > 0 && rect.left + rect.width <= PANEL_WIDTH &&
         rect.top + rect.height <= PANEL_HEIGHT;
}

constexpr bool fitsPanel(int width, int height) {
  return width == PANEL_WIDTH && height == PANEL_HEIGHT &&
         insidePanel(WEATHER) && insidePanel(TODAY) &&
         insidePanel(UPCOMING) && insidePanel(CALENDAR) &&
         TODAY.top + TODAY.height < UPCOMING.top &&
         UPCOMING.top + UPCOMING.height < WEATHER.top &&
         WEATHER.top + WEATHER.height <= DIAGNOSTIC_TOP &&
         weekRow(6).top + weekRow(6).height == DIAGNOSTIC_TOP &&
         monthCell(5, 6).top + monthCell(5, 6).height == DIAGNOSTIC_TOP &&
         NAVIGATION_TOP + NAVIGATION_HEIGHT == PANEL_HEIGHT;
}

}  // namespace calendar_portrait_layout
