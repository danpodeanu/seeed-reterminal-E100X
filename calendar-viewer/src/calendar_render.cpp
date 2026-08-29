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

void drawCalendarIcon(EPaper& epaper, int x, int y, int size,
                      uint32_t ink) {
  const int width = size;
  const int height = std::max(8, size * 7 / 8);
  const int stroke = std::max(1, size / 10);
  for (int inset = 0; inset < stroke; ++inset) {
    epaper.drawRect(x + inset, y + inset, width - 2 * inset,
                    height - 2 * inset, ink);
  }
  epaper.fillRect(x, y + height / 4, width, stroke, ink);
  const int bindingWidth = std::max(2, stroke * 2);
  epaper.fillRect(x + width / 4 - bindingWidth / 2, y, bindingWidth,
                  height / 4, ink);
  epaper.fillRect(x + width * 3 / 4 - bindingWidth / 2, y, bindingWidth,
                  height / 4, ink);
  const int dot = std::max(1, stroke);
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      epaper.fillRect(x + width * (column + 1) / 3 - dot / 2,
                      y + height * (row + 2) / 4 - dot / 2,
                      dot, dot, ink);
    }
  }
}

void drawThermometerIcon(EPaper& epaper, int centerX, int centerY,
                         int height, uint32_t ink) {
  const int bulbRadius = std::max(2, height / 5);
  const int stemWidth = std::max(2, height / 5);
  const int stemTop = centerY - height / 2;
  const int stemBottom = centerY + height / 2 - bulbRadius;
  epaper.fillRect(centerX - stemWidth / 2, stemTop, stemWidth,
                  std::max(1, stemBottom - stemTop), ink);
  epaper.fillCircle(centerX, stemBottom, bulbRadius, ink);
}

void drawDropletIcon(EPaper& epaper, int centerX, int centerY, int height,
                     uint32_t ink) {
  const int radius = std::max(2, height / 4);
  const int circleY = centerY + height / 4;
  epaper.fillTriangle(centerX, centerY - height / 2,
                      centerX - radius, circleY,
                      centerX + radius, circleY, ink);
  epaper.fillCircle(centerX, circleY, radius, ink);
}

void drawLocationIcon(EPaper& epaper, int centerX, int centerY, int size,
                      uint32_t ink, uint32_t background) {
  const int radius = std::max(3, size / 3);
  const int circleY = centerY - size / 7;
  epaper.fillCircle(centerX, circleY, radius, ink);
  epaper.fillTriangle(centerX - radius + 1, circleY + radius / 2,
                      centerX + radius - 1, circleY + radius / 2,
                      centerX, centerY + size / 2, ink);
  epaper.fillCircle(centerX, circleY, std::max(1, radius / 3), background);
}

void drawAlertIcon(EPaper& epaper, int centerX, int centerY, int size,
                   uint32_t ink, uint32_t background) {
  const int half = std::max(4, size / 2);
  epaper.fillTriangle(centerX, centerY - half,
                      centerX - half, centerY + half,
                      centerX + half, centerY + half, ink);
  const int markWidth = std::max(1, size / 9);
  epaper.fillRect(centerX - markWidth / 2, centerY - size / 5,
                  markWidth, std::max(2, size / 3), background);
  epaper.fillCircle(centerX, centerY + size / 3,
                    std::max(1, markWidth / 2), background);
}

void drawSunIcon(EPaper& epaper, int centerX, int centerY, int size,
                 uint32_t ink) {
  const int radius = std::max(3, size / 5);
  const int rayStart = radius + std::max(2, size / 12);
  const int rayEnd = size / 2;
  const int thickness = std::max(1, size / 14);
  epaper.fillCircle(centerX, centerY, radius, ink);
  epaper.fillRect(centerX - thickness / 2, centerY - rayEnd, thickness,
                  rayEnd - rayStart + 1, ink);
  epaper.fillRect(centerX - thickness / 2, centerY + rayStart, thickness,
                  rayEnd - rayStart + 1, ink);
  epaper.fillRect(centerX - rayEnd, centerY - thickness / 2,
                  rayEnd - rayStart + 1, thickness, ink);
  epaper.fillRect(centerX + rayStart, centerY - thickness / 2,
                  rayEnd - rayStart + 1, thickness, ink);
  const int diagonal = std::max(1, size / 3);
  epaper.drawLine(centerX - diagonal, centerY - diagonal,
                  centerX - rayStart * 2 / 3,
                  centerY - rayStart * 2 / 3, ink);
  epaper.drawLine(centerX + rayStart * 2 / 3,
                  centerY - rayStart * 2 / 3,
                  centerX + diagonal, centerY - diagonal, ink);
  epaper.drawLine(centerX - diagonal, centerY + diagonal,
                  centerX - rayStart * 2 / 3,
                  centerY + rayStart * 2 / 3, ink);
  epaper.drawLine(centerX + rayStart * 2 / 3,
                  centerY + rayStart * 2 / 3,
                  centerX + diagonal, centerY + diagonal, ink);
}

void drawMoonIcon(EPaper& epaper, int centerX, int centerY, int size,
                  uint32_t ink, uint32_t background) {
  const int radius = std::max(4, size * 2 / 5);
  epaper.fillCircle(centerX, centerY, radius, ink);
  epaper.fillCircle(centerX + radius / 2, centerY - radius / 3,
                    std::max(2, radius * 4 / 5), background);
}

void drawCloudIcon(EPaper& epaper, int centerX, int centerY, int size,
                   uint32_t ink) {
  const int baseY = centerY + size / 7;
  const int leftRadius = std::max(3, size / 5);
  const int topRadius = std::max(4, size / 4);
  const int rightRadius = std::max(3, size / 6);
  epaper.fillCircle(centerX - size / 5, baseY, leftRadius, ink);
  epaper.fillCircle(centerX + size / 14, centerY - size / 12,
                    topRadius, ink);
  epaper.fillCircle(centerX + size / 4, baseY, rightRadius, ink);
  epaper.fillRect(centerX - size * 2 / 5, baseY,
                  size * 4 / 5, std::max(2, size / 5), ink);
}

void drawSnowflake(EPaper& epaper, int centerX, int centerY, int size,
                   uint32_t ink) {
  const int radius = std::max(2, size / 2);
  epaper.drawFastHLine(centerX - radius, centerY, radius * 2 + 1, ink);
  epaper.drawFastVLine(centerX, centerY - radius, radius * 2 + 1, ink);
  epaper.drawLine(centerX - radius, centerY - radius,
                  centerX + radius, centerY + radius, ink);
  epaper.drawLine(centerX - radius, centerY + radius,
                  centerX + radius, centerY - radius, ink);
}

void drawWeatherIcon(EPaper& epaper, int centerX, int centerY, int size,
                     int code, bool isDay, uint32_t background) {
  const bool thunder = code >= 95;
  const bool snow = (code >= 71 && code <= 77) ||
                    (code >= 85 && code <= 86);
  const bool rain = (code >= 51 && code <= 67) ||
                    (code >= 80 && code <= 82);
  const bool fog = code == 45 || code == 48;
  const bool clear = code == 0;
  const bool partlyCloudy = code == 1 || code == 2;

  if (clear) {
    if (isDay) {
      drawSunIcon(epaper, centerX, centerY, size, COLOR_SUN);
    } else {
      drawMoonIcon(epaper, centerX, centerY, size, PANEL_ACCENT,
                   background);
    }
    return;
  }

  if (partlyCloudy) {
    if (isDay) {
      drawSunIcon(epaper, centerX - size / 5, centerY - size / 5,
                  size * 2 / 3, COLOR_SUN);
    } else {
      drawMoonIcon(epaper, centerX - size / 5, centerY - size / 5,
                   size * 2 / 3, PANEL_ACCENT, background);
    }
  }
  drawCloudIcon(epaper, centerX, centerY - size / 12, size * 3 / 4,
                PANEL_BLACK);

  const int effectY = centerY + size / 3;
  if (thunder) {
    epaper.fillTriangle(centerX + size / 12, effectY - size / 10,
                        centerX - size / 8, effectY + size / 8,
                        centerX, effectY + size / 8, COLOR_ALERT);
    epaper.fillTriangle(centerX, effectY + size / 10,
                        centerX + size / 8, effectY + size / 10,
                        centerX - size / 12, effectY + size / 2,
                        COLOR_ALERT);
  } else if (snow) {
    drawSnowflake(epaper, centerX - size / 5, effectY, size / 7,
                  COLOR_RAIN);
    drawSnowflake(epaper, centerX + size / 5, effectY + size / 10,
                  size / 7, COLOR_RAIN);
  } else if (rain) {
    const int drop = std::max(3, size / 7);
    epaper.drawLine(centerX - size / 4, effectY,
                    centerX - size / 4 - drop / 2, effectY + drop,
                    COLOR_RAIN);
    epaper.drawLine(centerX, effectY, centerX - drop / 2,
                    effectY + drop, COLOR_RAIN);
    epaper.drawLine(centerX + size / 4, effectY,
                    centerX + size / 4 - drop / 2, effectY + drop,
                    COLOR_RAIN);
  } else if (fog) {
    epaper.drawFastHLine(centerX - size / 3, effectY,
                        size * 2 / 3, PANEL_MUTED);
    epaper.drawFastHLine(centerX - size / 4, effectY + size / 8,
                        size / 2, PANEL_MUTED);
  }
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
  epaper.fillRect(0, 0, config::PANEL_WIDTH, height,
                  PANEL_HEADER_BACKGROUND);
  epaper.drawFastHLine(0, height - 1, config::PANEL_WIDTH, PANEL_BLACK);
  epaper.setTextDatum(ML_DATUM);
  epaper.setTextColor(PANEL_BLACK, PANEL_HEADER_BACKGROUND, true);
  const int calendarIconSize = config::ui(18);
  drawCalendarIcon(epaper, margin,
                   height / 2 - calendarIconSize * 7 / 16,
                   calendarIconSize, PANEL_ACCENT);
  selectFont(epaper, FontSize::Medium);
  String heading = formatDate(now, "%A, %e %B");
  epaper.drawString(heading,
                    margin + calendarIconSize + config::ui(9), height / 2);

  selectFont(epaper, FontSize::Small);
  String viewLabel;
  if (view == config::CalendarView::Week) viewLabel = "WEEK";
  else if (view == config::CalendarView::Month) viewLabel = "MONTH";
  else viewLabel = "TODAY";
  const int viewWidth = config::ui(88);
  const int viewHeight = config::ui(28);
  const int viewLeft = (config::PANEL_WIDTH - viewWidth) / 2;
  const int viewTop = (height - viewHeight) / 2;
  epaper.fillRoundRect(viewLeft, viewTop, viewWidth, viewHeight,
                       std::max(3, config::ui(5)), PANEL_ACCENT);
  epaper.setTextColor(PANEL_ACCENT_TEXT, PANEL_ACCENT, true);
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString(viewLabel, config::PANEL_WIDTH / 2, height / 2);

  const int batteryWidth = config::ui(22);
  const int batteryHeight = config::ui(12);
  const int terminalWidth = std::max(3, config::ui(5));
  const int batteryX =
      config::PANEL_WIDTH - margin - terminalWidth - batteryWidth;
  const int batteryY = height / 2 + 2 - batteryHeight / 2;
  const int outline = std::max(1, config::ui(1));
  const int terminalHeight = std::max(3, config::ui(5));
  const int batteryPct = indoor.batteryValid ? indoor.batteryPct : -1;
  const String percent =
      indoor.batteryValid ? String(indoor.batteryPct) + "%" : "--%";

  epaper.setTextColor(PANEL_BLACK, PANEL_HEADER_BACKGROUND, true);
  epaper.setTextDatum(MR_DATUM);
  const int percentRight = batteryX - config::ui(9);
  epaper.drawString(percent, percentRight, height / 2);
  if (indoor.climateValid) {
    const int iconHeight = config::ui(12);
    const String humidity = String(indoor.humidityPct, 0) + "%";
    const int humidityRight =
        percentRight - epaper.textWidth(percent) - config::ui(14);
    epaper.drawString(humidity, humidityRight, height / 2);
    const int dropletX =
        humidityRight - epaper.textWidth(humidity) - config::ui(6);
    drawDropletIcon(epaper, dropletX, height / 2, iconHeight,
                    COLOR_HUMIDITY);

    const String indoorTemperature = temperature(indoor.temperatureC);
    const int temperatureRight =
        dropletX - iconHeight / 2 - config::ui(9);
    epaper.drawString(indoorTemperature, temperatureRight, height / 2);
    const int thermometerX =
        temperatureRight - epaper.textWidth(indoorTemperature) -
        config::ui(6);
    drawThermometerIcon(epaper, thermometerX, height / 2, iconHeight,
                        COLOR_TEMPERATURE);
  }
  text_render::drawBatteryGauge(
      epaper, batteryX, batteryY, batteryWidth, batteryHeight, batteryPct,
      outline, terminalWidth, terminalHeight, PANEL_BLACK,
      PANEL_HEADER_BACKGROUND,
      indoor.externalPowerValid && indoor.externalPower);
  epaper.setTextDatum(TL_DATUM);
}

struct BodyGeometry {
  int left;
  int top;
  int width;
  int height;
  int dayLeft;
  int dayTop;
  int dayWidth;
  int dayHeight;
  int weatherLeft;
  int weatherTop;
  int weatherWidth;
  int weatherHeight;
};

BodyGeometry bodyGeometry() {
  BodyGeometry value{};
#if RETERMINAL_MODEL == 1003
  value = {32, 156, 1340, 1192,
           1404, 156, 436, 800,
           1404, 988, 436, 360};
#elif RETERMINAL_MODEL == 1004
  value = {24, 132, 1168, 1044,
           1216, 132, 360, 720,
           1216, 876, 360, 300};
#else
  value = {12, 76, 596, 390,
           620, 76, 168, 228,
           620, 316, 168, 150};
#endif
  return value;
}

void drawWeatherCard(EPaper& epaper, const BodyGeometry& body,
                     const WeatherData& weather) {
  epaper.fillRect(body.weatherLeft, body.weatherTop, body.weatherWidth,
                  body.weatherHeight, PANEL_WEATHER_BACKGROUND);
  epaper.drawRect(body.weatherLeft, body.weatherTop, body.weatherWidth,
                  body.weatherHeight, PANEL_BLACK);
  const int margin =
#if RETERMINAL_MODEL == 1003
      24;
#elif RETERMINAL_MODEL == 1004
      18;
#else
      10;
#endif
  const int headerHeight =
#if RETERMINAL_MODEL == 1003
      58;
#elif RETERMINAL_MODEL == 1004
      48;
#else
      30;
#endif
  const int iconSize =
#if RETERMINAL_MODEL == 1003
      96;
#elif RETERMINAL_MODEL == 1004
      78;
#else
      42;
#endif
  const int alertHeight =
#if RETERMINAL_MODEL == 1003
      62;
#elif RETERMINAL_MODEL == 1004
      52;
#else
      40;
#endif
  const int detailStep =
#if RETERMINAL_MODEL == 1003
      44;
#elif RETERMINAL_MODEL == 1004
      38;
#else
      25;
#endif

  epaper.fillRect(body.weatherLeft + 1, body.weatherTop + 1,
                  body.weatherWidth - 2, headerHeight - 1,
                  PANEL_WEATHER_HEADER);
  epaper.drawFastHLine(body.weatherLeft, body.weatherTop + headerHeight,
                      body.weatherWidth, PANEL_BLACK);
  const int locationIconSize = std::max(10, config::ui(13));
  const int locationIconX =
      body.weatherLeft + margin + locationIconSize / 2;
  const int headerCenterY = body.weatherTop + headerHeight / 2;
  drawLocationIcon(epaper, locationIconX, headerCenterY,
                   locationIconSize, PANEL_ACCENT, PANEL_WEATHER_HEADER);
  epaper.setTextColor(PANEL_BLACK, PANEL_WEATHER_HEADER, true);
  epaper.setTextDatum(ML_DATUM);
  selectFont(epaper, FontSize::Small);
  epaper.drawString(
      text_render::ellipsize(
          epaper, String(calendar_config::runtime::locationName()),
          body.weatherWidth - margin * 2 - locationIconSize -
              config::ui(6)),
      locationIconX + locationIconSize / 2 + config::ui(6),
      headerCenterY);

  const int contentTop = body.weatherTop + headerHeight;
  const int cardBottom = body.weatherTop + body.weatherHeight;
  int contentBottom = cardBottom - margin;
  if (!weather.alertTitle.isEmpty()) {
    const int alertTop = cardBottom - alertHeight;
    epaper.fillRect(body.weatherLeft + 1, alertTop,
                    body.weatherWidth - 2, alertHeight - 1, COLOR_ALERT);
    const int alertIconSize = std::max(10, config::ui(13));
    const int alertIconX =
        body.weatherLeft + margin + alertIconSize / 2;
    const int alertCenterY = alertTop + alertHeight / 2;
    drawAlertIcon(epaper, alertIconX, alertCenterY, alertIconSize,
                  COLOR_ALERT_TEXT, COLOR_ALERT);
    selectFont(epaper, FontSize::Tiny);
    epaper.setTextColor(COLOR_ALERT_TEXT, COLOR_ALERT, true);
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(
        text_render::ellipsize(
            epaper, weather.alertTitle,
            body.weatherWidth - margin * 2 - alertIconSize -
                config::ui(6)),
        alertIconX + alertIconSize / 2 + config::ui(6),
        alertCenterY);
    contentBottom = alertTop - config::ui(5);
  }

  if (!weather.valid) {
    selectFont(epaper, FontSize::Small, false);
    epaper.setTextColor(PANEL_BLACK, PANEL_WEATHER_BACKGROUND, true);
    epaper.setTextDatum(MC_DATUM);
    const int messageY = (contentTop + contentBottom) / 2;
    if (messageY + epaper.fontHeight(1) / 2 <= contentBottom) {
      epaper.drawString(
          text_render::ellipsize(epaper, "Weather unavailable",
                                 body.weatherWidth - margin * 2),
          body.weatherLeft + body.weatherWidth / 2, messageY);
    }
    return;
  }

  const int iconCenterX =
      body.weatherLeft + margin + iconSize / 2;
  const int iconCenterY = contentTop + margin + iconSize / 2;
  drawWeatherIcon(epaper, iconCenterX, iconCenterY, iconSize,
                  weather.weatherCode, weather.isDay,
                  PANEL_WEATHER_BACKGROUND);

  selectFont(epaper, FontSize::Large);
  epaper.setTextColor(PANEL_BLACK, PANEL_WEATHER_BACKGROUND, true);
  epaper.setTextDatum(ML_DATUM);
  const int temperatureX =
      body.weatherLeft + margin + iconSize + config::ui(8);
  const int temperatureWidth =
      body.weatherLeft + body.weatherWidth - margin - temperatureX;
  epaper.drawString(
      text_render::ellipsize(epaper, temperature(weather.temperatureC),
                             temperatureWidth),
      temperatureX, iconCenterY);

#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
  selectFont(epaper, FontSize::Tiny, false);
#else
  selectFont(epaper, FontSize::Small, false);
#endif
  epaper.setTextDatum(TL_DATUM);
  int y = contentTop + margin + iconSize + config::ui(16);
  const int availableWidth = body.weatherWidth - margin * 2;
  auto drawDetail = [&](const String& value) {
    if (value.isEmpty() || y + epaper.fontHeight(1) > contentBottom) {
      return false;
    }
    epaper.drawString(text_render::ellipsize(epaper, value, availableWidth),
                      body.weatherLeft + margin, y);
    y += detailStep;
    return true;
  };
  drawDetail(String(app_logic::conditionName(weather.weatherCode)));

  String details;
  if (isfinite(weather.days[0].minimumC) &&
      isfinite(weather.days[0].maximumC)) {
    details = "Low " + temperature(weather.days[0].minimumC, false) +
              "\xC2\xB0  High " +
              temperature(weather.days[0].maximumC, false) + "\xC2\xB0";
  }
  drawDetail(details);

  const String windLabel = wind(weather.windKmh);
  drawDetail(windLabel.isEmpty() ? String() : "Wind " + windLabel);
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

void drawDayCard(EPaper& epaper, const ::calendar::Data& data,
                 const BodyGeometry& body, time_t dayStart) {
  epaper.fillRect(body.dayLeft, body.dayTop, body.dayWidth, body.dayHeight,
                  PANEL_WHITE);
  epaper.drawRect(body.dayLeft, body.dayTop, body.dayWidth, body.dayHeight,
                  PANEL_BLACK);
  const int margin =
#if RETERMINAL_MODEL == 1003
      24;
#elif RETERMINAL_MODEL == 1004
      18;
#else
      10;
#endif
  const int headerHeight =
#if RETERMINAL_MODEL == 1003
      68;
#elif RETERMINAL_MODEL == 1004
      54;
#else
      36;
#endif
  const int rowHeight =
#if RETERMINAL_MODEL == 1003
      82;
#elif RETERMINAL_MODEL == 1004
      64;
#else
      44;
#endif
  const int markerWidth =
#if RETERMINAL_MODEL == 1003
      10;
#elif RETERMINAL_MODEL == 1004
      7;
#else
      4;
#endif
  const int titleOffset =
#if RETERMINAL_MODEL == 1003
      34;
#elif RETERMINAL_MODEL == 1004
      27;
#else
      20;
#endif
  const int headerIconSize = std::max(10, config::ui(14));

  const int contentLeft = body.dayLeft + margin;
  const int contentWidth = body.dayWidth - 2 * margin;
  epaper.fillRect(body.dayLeft + 1, body.dayTop + 1,
                  body.dayWidth - 2, headerHeight - 1,
                  PANEL_SECTION_HEADER);
  epaper.setTextColor(PANEL_SECTION_HEADER_TEXT, PANEL_SECTION_HEADER, true);
#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
  selectFont(epaper, FontSize::Small);
#else
  selectFont(epaper, FontSize::Tiny);
#endif
  drawCalendarIcon(epaper, contentLeft,
                   body.dayTop + (headerHeight - headerIconSize * 7 / 8) / 2,
                   headerIconSize, PANEL_SECTION_HEADER_TEXT);
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString("TODAY",
                    contentLeft + headerIconSize + config::ui(6),
                    body.dayTop + headerHeight / 2);
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(formatDate(dayStart, "%e %b"),
                    body.dayLeft + body.dayWidth - margin,
                    body.dayTop + headerHeight / 2);
  epaper.drawFastHLine(body.dayLeft, body.dayTop + headerHeight,
                      body.dayWidth, PANEL_BLACK);

  const auto events = eventsForDay(data, dayStart);
  const int capacity =
      std::max(1, (body.dayHeight - headerHeight - margin) / rowHeight);
  if (events.empty()) {
    selectFont(epaper, FontSize::Tiny, false);
    epaper.setTextColor(PANEL_MUTED, PANEL_WHITE, true);
    epaper.setTextDatum(TL_DATUM);
    epaper.drawString("No events", contentLeft,
                      body.dayTop + headerHeight + margin);
    return;
  }

  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  int shown = std::min(static_cast<int>(events.size()), capacity);
  if (static_cast<int>(events.size()) > capacity && capacity > 1) {
    --shown;
  }
  epaper.setTextDatum(TL_DATUM);
  int y = body.dayTop + headerHeight;
  for (int index = 0; index < shown; ++index) {
    const ::calendar::Event& event = *events[index];
    const uint32_t ink = nearestCalendarInk(event.colorRgb);
    epaper.fillRect(contentLeft, y + 5, markerWidth, rowHeight - 10, ink);
    const int textLeft = contentLeft + markerWidth + config::ui(5);
    const int textWidth =
        body.dayLeft + body.dayWidth - margin - textLeft;
    selectFont(epaper, FontSize::Tiny);
    const String timeLabel =
        event.allDay ? "All day"
                     : event.start < dayStart ? "Ongoing"
                                              : formatTime(event.start);
    epaper.setTextColor(PANEL_ACCENT, PANEL_WHITE, true);
    epaper.drawString(timeLabel, textLeft, y + 2);
    selectFont(epaper, FontSize::Tiny, false);
    epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
    epaper.drawString(ellipsize(epaper, event.title, textWidth),
                      textLeft, y + titleOffset);
    epaper.drawFastHLine(contentLeft, y + rowHeight - 1, contentWidth,
                        PANEL_MUTED);
    y += rowHeight;
  }
  if (shown < static_cast<int>(events.size())) {
    selectFont(epaper, FontSize::Tiny);
    epaper.drawString("+" + String(events.size() - shown) + " more",
                      contentLeft, y + config::ui(5));
  }
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
    epaper.setTextColor(PANEL_ACCENT, PANEL_WHITE, true);
    epaper.drawString(event->allDay ? "All day" : formatTime(event->start),
                      body.left + 22, y + 8);
    epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
    epaper.drawString(
        ellipsize(epaper, event->title, body.width - timeWidth - 34),
        body.left + timeWidth, y + 8);
    if (!event->location.empty()) {
      selectFont(epaper, FontSize::Tiny, false);
      const int locationIconSize = std::max(8, config::ui(9));
      const int locationY =
          y +
#if RETERMINAL_MODEL == 1003
          62;
#else
          34;
#endif
      const int locationIconX =
          body.left + timeWidth + locationIconSize / 2;
      drawLocationIcon(epaper, locationIconX,
                       locationY + epaper.fontHeight(1) / 2,
                       locationIconSize, PANEL_ACCENT, PANEL_WHITE);
      const int locationTextX =
          body.left + timeWidth + locationIconSize + config::ui(4);
      epaper.drawString(
          ellipsize(epaper, event->location,
                    body.left + body.width - locationTextX - 12),
          locationTextX, locationY);
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
  epaper.fillRect(body.left, body.top, body.width, headerHeight,
                  PANEL_SECTION_HEADER);
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(PANEL_SECTION_HEADER_TEXT, PANEL_SECTION_HEADER, true);
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
                       PANEL_TODAY_BACKGROUND);
      }
      epaper.setTextDatum(TL_DATUM);
      epaper.setTextColor(outsideMonth ? PANEL_MUTED : PANEL_BLACK,
                         today ? PANEL_TODAY_BACKGROUND : PANEL_WHITE, true);
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
  const int iconSize = config::ui(38);
  drawAlertIcon(epaper, config::PANEL_WIDTH / 2,
                config::PANEL_HEIGHT / 2 - config::ui(72),
                iconSize, COLOR_ALERT, PANEL_WHITE);
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
  drawDayCard(epaper, data, body, calendar_logic::localMidnight(now));
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
