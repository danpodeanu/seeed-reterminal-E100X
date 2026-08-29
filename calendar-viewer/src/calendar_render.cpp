#include "calendar_render.h"

#include <TFT_eSPI.h>
#include <math.h>

#include <algorithm>

#include "calendar_config_runtime.h"
#include "calendar_logic.h"
#include "config.h"
#include "driver.h"
#include "text_render.h"
#include "theme.h"
#include "weather_app_logic.h"

namespace calendar_render {
namespace {

using namespace theme;

enum class FontSize {
  Tiny,
  Small,
  Medium,
  Large,
};

void selectFont(EPaper& epaper, FontSize size, bool bold = true) {
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  if (size == FontSize::Large) {
    epaper.setFreeFont(bold ? &FreeSansBold24pt7b : &FreeSans24pt7b);
  } else if (size == FontSize::Medium) {
    epaper.setFreeFont(bold ? &FreeSansBold18pt7b : &FreeSans18pt7b);
  } else if (size == FontSize::Small) {
    epaper.setFreeFont(bold ? &FreeSansBold12pt7b : &FreeSans12pt7b);
  } else {
    epaper.setFreeFont(bold ? &FreeSansBold9pt7b : &FreeSans9pt7b);
  }
#elif RETERMINAL_MODEL == 1004
  if (size == FontSize::Large) {
    epaper.setFreeFont(bold ? &FreeSansBold24pt7b : &FreeSans24pt7b);
  } else if (size == FontSize::Medium) {
    epaper.setFreeFont(bold ? &FreeSansBold18pt7b : &FreeSans18pt7b);
  } else if (size == FontSize::Small) {
    epaper.setFreeFont(bold ? &FreeSansBold12pt7b : &FreeSans12pt7b);
  } else {
    epaper.setFreeFont(bold ? &FreeSansBold9pt7b : &FreeSans9pt7b);
  }
#else
  if (size == FontSize::Large) {
    epaper.setFreeFont(bold ? &FreeSansBold18pt7b : &FreeSans18pt7b);
  } else if (size == FontSize::Medium) {
    epaper.setFreeFont(bold ? &FreeSansBold12pt7b : &FreeSans12pt7b);
  } else {
    epaper.setFreeFont(bold ? &FreeSansBold9pt7b : &FreeSans9pt7b);
  }
#endif
}

uint32_t nearestCalendarInk(uint32_t rgb) {
#if RETERMINAL_MODEL == 1002 || RETERMINAL_MODEL == 1004
  struct Candidate {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t ink;
  };
  static const Candidate kCandidates[] = {
      {196, 40, 38, TFT_RED},    {245, 180, 0, TFT_YELLOW},
      {35, 95, 190, TFT_BLUE},   {35, 135, 65, TFT_GREEN},
      {30, 30, 30, TFT_BLACK},
  };
  const int r = calendar_logic::red(rgb);
  const int g = calendar_logic::green(rgb);
  const int b = calendar_logic::blue(rgb);
  uint32_t bestInk = TFT_BLACK;
  uint32_t bestDistance = UINT32_MAX;
  for (const Candidate& candidate : kCandidates) {
    const int dr = r - candidate.r;
    const int dg = g - candidate.g;
    const int db = b - candidate.b;
    const uint32_t distance =
        static_cast<uint32_t>(dr * dr + dg * dg + db * db);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestInk = candidate.ink;
    }
  }
  return bestInk;
#elif RETERMINAL_MODEL == 1003
  const uint8_t level =
      static_cast<uint8_t>(std::min(11, calendar_logic::luminance(rgb) / 17));
  return TFT_GRAY_0 + level;
#else
  const uint8_t level =
      static_cast<uint8_t>(std::min(2, calendar_logic::luminance(rgb) / 64));
  return TFT_GRAY_0 + level;
#endif
}

String ellipsize(EPaper& epaper, const std::string& value, int width) {
  return text_render::ellipsize(epaper, String(value.c_str()), width);
}

String formatTime(time_t value) {
  struct tm local = {};
  char buffer[16] = {};
  if (localtime_r(&value, &local) == nullptr) return "--:--";
  strftime(buffer, sizeof(buffer), "%H:%M", &local);
  return String(buffer);
}

String formatDate(time_t value, const char* pattern) {
  struct tm local = {};
  char buffer[64] = {};
  if (localtime_r(&value, &local) == nullptr) return "";
  strftime(buffer, sizeof(buffer), pattern, &local);
  return String(buffer);
}

String temperature(float celsius, bool includeUnit = true) {
  if (!isfinite(celsius)) return "--";
  float value = celsius;
  const char* unit = "C";
  if (calendar_config::runtime::temperatureUnit() ==
      config::TemperatureUnit::Fahrenheit) {
    value = value * 9.0f / 5.0f + 32.0f;
    unit = "F";
  }
  String result(value, 0);
  if (includeUnit) {
    result += "\xC2\xB0";
    result += unit;
  }
  return result;
}

String wind(float kmh) {
  if (!isfinite(kmh)) return "";
  float value = kmh;
  const char* unit = "km/h";
  if (calendar_config::runtime::windSpeedUnit() ==
      config::WindSpeedUnit::MilesPerHour) {
    value *= 0.621371f;
    unit = "mph";
  }
  return String(value, 0) + " " + unit;
}

void drawHeader(EPaper& epaper, config::CalendarView view, time_t now,
                const sensors::Readings& indoor) {
#if RETERMINAL_MODEL == 1003
  constexpr int height = 132;
  constexpr int margin = 38;
#elif RETERMINAL_MODEL == 1004
  constexpr int height = 112;
  constexpr int margin = 26;
#else
  constexpr int height = 64;
  constexpr int margin = 16;
#endif
  epaper.fillRect(0, 0, config::PANEL_WIDTH, height, PANEL_LIGHT);
  epaper.drawFastHLine(0, height - 1, config::PANEL_WIDTH, PANEL_BLACK);
  epaper.setTextDatum(ML_DATUM);
  epaper.setTextColor(PANEL_BLACK, PANEL_LIGHT, true);
  selectFont(epaper, FontSize::Medium);
  String heading = formatDate(now, "%A, %e %B");
  epaper.drawString(heading, margin, height / 2);

  selectFont(epaper, FontSize::Small);
  String viewLabel;
  if (view == config::CalendarView::Week) viewLabel = "WEEK";
  else if (view == config::CalendarView::Month) viewLabel = "MONTH";
  else viewLabel = "TODAY";
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString(viewLabel, config::PANEL_WIDTH / 2, height / 2);

  String sensorsLabel;
  if (indoor.climateValid) {
    sensorsLabel = "Indoor " + temperature(indoor.temperatureC) + "  " +
                   String(indoor.humidityPct, 0) + "%";
  }
  if (indoor.batteryValid) {
    if (!sensorsLabel.isEmpty()) sensorsLabel += "  ";
    sensorsLabel += String(indoor.batteryPct) + "%";
    if (indoor.externalPower) sensorsLabel += " USB";
  }
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(sensorsLabel, config::PANEL_WIDTH - margin, height / 2);
  epaper.setTextDatum(TL_DATUM);
}

struct BodyGeometry {
  int left;
  int top;
  int width;
  int height;
  int weatherLeft;
  int weatherTop;
  int weatherWidth;
  int weatherHeight;
};

BodyGeometry bodyGeometry() {
  BodyGeometry value{};
#if RETERMINAL_MODEL == 1003
  value = {32, 156, 1340, 1192, 1404, 156, 436, 1192};
#elif RETERMINAL_MODEL == 1004
  value = {24, 132, 1168, 1044, 1216, 132, 360, 1044};
#else
  value = {12, 76, 596, 390, 620, 76, 168, 390};
#endif
  return value;
}

void drawWeatherCard(EPaper& epaper, const BodyGeometry& body,
                     const WeatherData& weather) {
  epaper.drawRect(body.weatherLeft, body.weatherTop, body.weatherWidth,
                  body.weatherHeight, PANEL_BLACK);
  const int margin =
#if RETERMINAL_MODEL == 1003
      28;
#elif RETERMINAL_MODEL == 1004
      20;
#else
      12;
#endif
  int x = body.weatherLeft + margin;
  int y = body.weatherTop + margin;
  const int availableWidth = body.weatherWidth - margin * 2;
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(TL_DATUM);
  selectFont(epaper, FontSize::Small);
  epaper.drawString(
      text_render::ellipsize(
          epaper, String(calendar_config::runtime::locationName()),
          availableWidth),
      x, y);
  y +=
#if RETERMINAL_MODEL == 1003
      60;
#elif RETERMINAL_MODEL == 1004
      45;
#else
      34;
#endif

  if (!weather.valid) {
    selectFont(epaper, FontSize::Small, false);
    epaper.drawString("Weather unavailable", x, y);
    return;
  }

  selectFont(epaper, FontSize::Large);
  epaper.drawString(temperature(weather.temperatureC), x, y);
  y +=
#if RETERMINAL_MODEL == 1003
      95;
#elif RETERMINAL_MODEL == 1004
      58;
#else
      52;
#endif
  selectFont(epaper, FontSize::Small, false);
  epaper.drawString(
      text_render::ellipsize(
          epaper, String(app_logic::conditionName(weather.weatherCode)),
          availableWidth),
      x, y);
  y +=
#if RETERMINAL_MODEL == 1003
      56;
#elif RETERMINAL_MODEL == 1004
      40;
#else
      31;
#endif

  String details;
  if (isfinite(weather.days[0].minimumC) &&
      isfinite(weather.days[0].maximumC)) {
    details = "Low " + temperature(weather.days[0].minimumC, false) +
              "\xC2\xB0  High " +
              temperature(weather.days[0].maximumC, false) + "\xC2\xB0";
  }
  epaper.drawString(text_render::ellipsize(epaper, details, availableWidth),
                    x, y);
  y +=
#if RETERMINAL_MODEL == 1003
      52;
#else
      31;
#endif
  const String windLabel = wind(weather.windKmh);
  if (!windLabel.isEmpty()) epaper.drawString("Wind " + windLabel, x, y);
  y +=
#if RETERMINAL_MODEL == 1003
      52;
#else
      31;
#endif
  if (!weather.alertTitle.isEmpty() && y < body.weatherTop + body.weatherHeight - 40) {
    selectFont(epaper, FontSize::Tiny);
    epaper.setTextColor(PANEL_BLACK, PANEL_LIGHT, true);
    epaper.fillRect(x - 5, y - 4, availableWidth + 10,
#if RETERMINAL_MODEL == 1003
                   90,
#else
                   52,
#endif
                   PANEL_LIGHT);
    epaper.drawString(
        text_render::ellipsize(epaper, "! " + weather.alertTitle,
                               availableWidth),
        x, y);
  }
}

std::vector<const ::calendar::Event*> eventsForDay(
    const ::calendar::Data& data, time_t dayStart) {
  std::vector<const ::calendar::Event*> result;
  const time_t dayEnd = calendar_logic::addLocalDays(dayStart, 1);
  for (const auto& event : data.events) {
    if (calendar_logic::overlaps(event.start, event.end, dayStart, dayEnd)) {
      result.push_back(&event);
    }
  }
  return result;
}

void drawToday(EPaper& epaper, const ::calendar::Data& data,
               const BodyGeometry& body, time_t dayStart) {
  const auto events = eventsForDay(data, dayStart);
  epaper.setTextDatum(TL_DATUM);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  if (events.empty()) {
    selectFont(epaper, FontSize::Medium, false);
    epaper.drawString("No events today", body.left + 20, body.top + 30);
    return;
  }

  const int rowHeight =
#if RETERMINAL_MODEL == 1003
      132;
#elif RETERMINAL_MODEL == 1004
      105;
#else
      62;
#endif
  const int maxRows = std::max(1, body.height / rowHeight);
  const int timeWidth =
#if RETERMINAL_MODEL == 1003
      170;
#elif RETERMINAL_MODEL == 1004
      115;
#else
      70;
#endif
  int y = body.top;
  int shown = 0;
  for (const ::calendar::Event* event : events) {
    if (shown >= maxRows) break;
    const uint32_t ink = nearestCalendarInk(event->colorRgb);
    epaper.fillRect(body.left, y + 5,
#if RETERMINAL_MODEL == 1003
                   14,
#else
                   7,
#endif
                   rowHeight - 10, ink);
    selectFont(epaper, FontSize::Small);
    epaper.drawString(event->allDay ? "All day" : formatTime(event->start),
                      body.left + 22, y + 8);
    epaper.drawString(
        ellipsize(epaper, event->title, body.width - timeWidth - 34),
        body.left + timeWidth, y + 8);
    if (!event->location.empty()) {
      selectFont(epaper, FontSize::Tiny, false);
      epaper.drawString(
          ellipsize(epaper, event->location, body.width - timeWidth - 34),
          body.left + timeWidth,
          y +
#if RETERMINAL_MODEL == 1003
              62
#else
              34
#endif
      );
    }
    epaper.drawFastHLine(body.left, y + rowHeight - 1, body.width,
                        PANEL_MUTED);
    y += rowHeight;
    ++shown;
  }
  if (static_cast<size_t>(shown) < events.size()) {
    selectFont(epaper, FontSize::Tiny);
    epaper.setTextDatum(BR_DATUM);
    epaper.drawString("+" + String(events.size() - shown) + " more",
                      body.left + body.width - 4,
                      body.top + body.height - 4);
    epaper.setTextDatum(TL_DATUM);
  }
}

const char* weekdayLabel(int index, config::WeekStart weekStart) {
  static const char* kMonday[] = {"Mon", "Tue", "Wed", "Thu",
                                   "Fri", "Sat", "Sun"};
  static const char* kSunday[] = {"Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat"};
  return weekStart == config::WeekStart::Sunday ? kSunday[index]
                                                 : kMonday[index];
}

void drawGrid(EPaper& epaper, const ::calendar::Data& data,
              const BodyGeometry& body, time_t gridStart, int rows,
              config::WeekStart weekStart, time_t now, bool monthView) {
  const int headerHeight =
#if RETERMINAL_MODEL == 1003
      72;
#elif RETERMINAL_MODEL == 1004
      52;
#else
      34;
#endif
  const int cellWidth = body.width / 7;
  const int cellHeight = (body.height - headerHeight) / rows;
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectFont(epaper, FontSize::Tiny);
  for (int column = 0; column < 7; ++column) {
    epaper.drawString(weekdayLabel(column, weekStart),
                      body.left + column * cellWidth + cellWidth / 2,
                      body.top + headerHeight / 2);
  }
  epaper.drawFastHLine(body.left, body.top + headerHeight, body.width,
                      PANEL_BLACK);

  const time_t monthAnchor = calendar_logic::startOfMonth(now);
  struct tm anchorTm = {};
  localtime_r(&monthAnchor, &anchorTm);
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < 7; ++column) {
      const int cell = row * 7 + column;
      const time_t day = calendar_logic::addLocalDays(gridStart, cell);
      const int x = body.left + column * cellWidth;
      const int y = body.top + headerHeight + row * cellHeight;
      if (column > 0) epaper.drawFastVLine(x, y, cellHeight, PANEL_MUTED);
      if (row > 0) epaper.drawFastHLine(x, y, cellWidth, PANEL_MUTED);

      struct tm dayTm = {};
      localtime_r(&day, &dayTm);
      const bool today = calendar_logic::sameLocalDate(day, now);
      const bool outsideMonth =
          monthView && (dayTm.tm_year != anchorTm.tm_year ||
                        dayTm.tm_mon != anchorTm.tm_mon);
      if (today) {
        epaper.fillRect(x + 1, y + 1, cellWidth - 2,
#if RETERMINAL_MODEL == 1003
                       54,
#else
                       30,
#endif
                       PANEL_LIGHT);
      }
      epaper.setTextDatum(TL_DATUM);
      epaper.setTextColor(outsideMonth ? PANEL_MUTED : PANEL_BLACK,
                         today ? PANEL_LIGHT : PANEL_WHITE, true);
      selectFont(epaper, FontSize::Tiny, today);
      epaper.drawString(String(dayTm.tm_mday), x + 5, y + 3);

      const auto dayEvents = eventsForDay(data, day);
      int eventY = y +
#if RETERMINAL_MODEL == 1003
          62;
#else
          32;
#endif
      const int lineHeight =
#if RETERMINAL_MODEL == 1003
          46;
#elif RETERMINAL_MODEL == 1004
          31;
#else
          23;
#endif
      const int capacity = std::max(1, (cellHeight - (eventY - y) - 4) /
                                           lineHeight);
      int shown = 0;
      for (const ::calendar::Event* event : dayEvents) {
        if (shown >= capacity) break;
        const uint32_t ink = nearestCalendarInk(event->colorRgb);
        epaper.fillRect(x + 4, eventY + 4,
#if RETERMINAL_MODEL == 1003
                       10,
#else
                       5,
#endif
                       lineHeight - 8, ink);
        selectFont(epaper, FontSize::Tiny, false);
        epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
        const int textX =
#if RETERMINAL_MODEL == 1003
            x + 20;
#else
            x + 12;
#endif
        epaper.drawString(
            ellipsize(epaper, event->title,
                      cellWidth - (textX - x) - 4),
            textX, eventY);
        eventY += lineHeight;
        ++shown;
      }
      if (shown < static_cast<int>(dayEvents.size())) {
        selectFont(epaper, FontSize::Tiny);
        epaper.setTextDatum(BR_DATUM);
        epaper.drawString("+" + String(dayEvents.size() - shown),
                          x + cellWidth - 4, y + cellHeight - 3);
      }
    }
  }
  epaper.setTextDatum(TL_DATUM);
}

void drawFooter(EPaper& epaper, const String& footer,
                const ::calendar::Data& data) {
  if (footer.isEmpty() &&
      !calendar_config::runtime::debugShowStatusBadges()) {
    return;
  }
  selectFont(epaper, FontSize::Tiny, false);
  epaper.setTextColor(PANEL_MUTED, PANEL_WHITE, true);
  epaper.setTextDatum(BL_DATUM);
  String label = footer;
  if (calendar_config::runtime::debugShowStatusBadges()) {
    if (!label.isEmpty()) label += "  ";
    label += String(data.events.size()) + " events";
    if (data.truncated) label += " (limited)";
  }
  epaper.drawString(label, 8, config::PANEL_HEIGHT - 3);
  epaper.setTextDatum(TL_DATUM);
}

}  // namespace

void status(EPaper& epaper, const String& title, const String& detail,
            const String& footer) {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  selectFont(epaper, FontSize::Large);
  epaper.drawString(title, config::PANEL_WIDTH / 2,
                    config::PANEL_HEIGHT / 2 -
#if RETERMINAL_MODEL == 1003
                        60
#else
                        30
#endif
  );
  selectFont(epaper, FontSize::Small, false);
  epaper.drawString(
      text_render::ellipsize(epaper, detail, config::PANEL_WIDTH - 60),
      config::PANEL_WIDTH / 2, config::PANEL_HEIGHT / 2 + 30);
  if (!footer.isEmpty()) {
    selectFont(epaper, FontSize::Tiny, false);
    epaper.setTextDatum(BC_DATUM);
    epaper.drawString(
        text_render::ellipsize(epaper, footer, config::PANEL_WIDTH - 30),
        config::PANEL_WIDTH / 2, config::PANEL_HEIGHT - 14);
  }
  epaper.setTextDatum(TL_DATUM);
}

void calendar(EPaper& epaper, const ::calendar::Data& data,
              const ::calendar::Window& window, config::CalendarView view,
              config::WeekStart weekStart, time_t now,
              const sensors::Readings& indoor, const WeatherData& weather,
              const String& footer) {
  epaper.fillSprite(PANEL_WHITE);
  drawHeader(epaper, view, now, indoor);
  const BodyGeometry body = bodyGeometry();
  drawWeatherCard(epaper, body, weather);
  if (view == config::CalendarView::Today) {
    drawToday(epaper, data, body, window.start);
  } else if (view == config::CalendarView::Week) {
    drawGrid(epaper, data, body, window.start, 1, weekStart, now, false);
  } else {
    drawGrid(epaper, data, body, window.start, 6, weekStart, now, true);
  }
  drawFooter(epaper, footer, data);
}

}  // namespace calendar_render
