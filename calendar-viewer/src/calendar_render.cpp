#include "calendar_render.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <stdlib.h>

#include <algorithm>

#include "app_logger.h"
#include "calendar_config_runtime.h"
#include "calendar_logic.h"
#include "config.h"
#include "dither.h"
#include "driver.h"
#include "text_render.h"
#include "theme.h"
#include "weather_app_logic.h"
#include "weather_icons.h"

namespace calendar_render {
namespace {

using namespace theme;

enum class FontSize {
  Tiny,
  Small,
  Medium,
  Large,
};

#if RETERMINAL_MODEL == 1003
constexpr int kAgendaMargin = 24;
constexpr int kAgendaHeaderHeight = 68;
constexpr int kAgendaRowHeight = 82;
constexpr int kAgendaCardGap = 16;
#elif RETERMINAL_MODEL == 1004
constexpr int kAgendaMargin = 18;
constexpr int kAgendaHeaderHeight = 54;
constexpr int kAgendaRowHeight = 64;
constexpr int kAgendaCardGap = 12;
#else
constexpr int kAgendaMargin = 10;
constexpr int kAgendaHeaderHeight = 36;
constexpr int kAgendaRowHeight = 44;
constexpr int kAgendaCardGap = 8;
#endif

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

void fillTodayRect(EPaper& epaper, int x, int y, int width, int height) {
  text_render::fillDitheredRect(
      epaper, x, y, width, height, config::PANEL_WIDTH, config::PANEL_HEIGHT,
      PANEL_TODAY_BASE, PANEL_TODAY_DITHER != PANEL_TODAY_BASE,
      PANEL_TODAY_DITHER, 5);
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

uint32_t nearestCalendarInk(uint32_t rgb) {
#if RETERMINAL_MODEL == 1002 || RETERMINAL_MODEL == 1004
  struct Candidate {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t ink;
  };
  static const Candidate kCandidates[] = {
      {255, 255, 255, TFT_WHITE}, {29, 185, 84, TFT_GREEN},
      {229, 57, 53, TFT_RED},     {255, 216, 0, TFT_YELLOW},
      {0, 76, 255, TFT_BLUE},     {0, 0, 0, TFT_BLACK},
  };
  const int r = calendar_logic::red(rgb);
  const int g = calendar_logic::green(rgb);
  const int b = calendar_logic::blue(rgb);
  uint32_t bestInk = kCandidates[0].ink;
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
      static_cast<uint8_t>(std::min(15, (calendar_logic::luminance(rgb) + 8) /
                                             17));
  return TFT_GRAY_0 + level;
#else
  const uint8_t level =
      static_cast<uint8_t>(std::min(3, (calendar_logic::luminance(rgb) + 42) /
                                            85));
  return TFT_GRAY_0 + level;
#endif
}

uint32_t eventTextInk(uint32_t rgb) {
  return calendar_logic::luminance(rgb) >= 145 ? PANEL_BLACK : PANEL_WHITE;
}

constexpr DitherPalette eventDitherPalette() {
#if RETERMINAL_MODEL == 1001
  return PAL_GRAY4;
#elif RETERMINAL_MODEL == 1003
  return PAL_GRAY16;
#else
  return PAL_E6;
#endif
}

uint8_t* allocateDitherBytes(size_t size) {
  uint8_t* bytes = static_cast<uint8_t*>(ps_malloc(size));
  if (!bytes) bytes = static_cast<uint8_t*>(malloc(size));
  return bytes;
}

uint32_t panelInkForDitherCode(uint8_t code) {
#if RETERMINAL_MODEL == 1002 || RETERMINAL_MODEL == 1004
  switch (code) {
    case 0x0:
      return TFT_WHITE;
    case 0x2:
      return TFT_GREEN;
    case 0x6:
      return TFT_RED;
    case 0xB:
      return TFT_YELLOW;
    case 0xD:
      return TFT_BLUE;
    case 0xF:
    default:
      return TFT_BLACK;
  }
#elif RETERMINAL_MODEL == 1003
  return TFT_GRAY_0 + std::min<uint8_t>(15, code);
#else
  return TFT_GRAY_0 + std::min<uint8_t>(3, code);
#endif
}

class ColorDitherer {
 public:
  ~ColorDitherer() {
    free(rgb_);
    free(indices_);
  }

  void fillRect(EPaper& epaper, int left, int top, int width, int height,
                uint32_t rgb) {
    if (!render(rgb, width, height)) {
      warnOnce();
      epaper.fillRect(left, top, width, height, nearestCalendarInk(rgb));
      return;
    }

    const size_t rowBytes = static_cast<size_t>(width + 1) / 2;
    for (int y = 0; y < height; ++y) {
      const size_t source = static_cast<size_t>(y) * width;
      const size_t destination = static_cast<size_t>(y) * rowBytes;
      for (int x = 0; x < width; x += 2) {
        const uint8_t leftCode = indices_[source + x] & 0x0F;
        const uint8_t rightCode =
            x + 1 < width ? indices_[source + x + 1] & 0x0F : 0;
        indices_[destination + x / 2] =
            static_cast<uint8_t>((leftCode << 4) | rightCode);
      }
    }

    if ((width & 1) == 0) {
      epaper.pushImage(left, top, width, height,
                       reinterpret_cast<uint16_t*>(indices_));
      return;
    }
    for (int y = 0; y < height; ++y) {
      epaper.pushImage(left, top + y, width, 1,
                       reinterpret_cast<uint16_t*>(indices_ + y * rowBytes));
    }
  }

  void fillCircle(EPaper& epaper, int centerX, int centerY, int radius,
                  uint32_t rgb) {
    const int diameter = radius * 2 + 1;
    if (!render(rgb, diameter, diameter)) {
      warnOnce();
      epaper.fillCircle(centerX, centerY, radius, nearestCalendarInk(rgb));
      return;
    }
    const int radiusSquared = radius * radius;
    for (int y = 0; y < diameter; ++y) {
      const int dy = y - radius;
      for (int x = 0; x < diameter; ++x) {
        const int dx = x - radius;
        if (dx * dx + dy * dy > radiusSquared) continue;
        epaper.drawPixel(centerX + dx, centerY + dy,
                         panelInkForDitherCode(indices_[y * diameter + x]));
      }
    }
  }

 private:
  bool reserve(size_t pixelCount) {
    if (pixelCount <= capacity_) return true;
    if (pixelCount > SIZE_MAX / 3) return false;

    uint8_t* rgb = allocateDitherBytes(pixelCount * 3);
    if (!rgb) return false;
    uint8_t* indices = allocateDitherBytes(pixelCount);
    if (!indices) {
      free(rgb);
      return false;
    }

    free(rgb_);
    free(indices_);
    rgb_ = rgb;
    indices_ = indices;
    capacity_ = pixelCount;
    return true;
  }

  bool render(uint32_t rgb, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const size_t pixelCount =
        static_cast<size_t>(width) * static_cast<size_t>(height);
    if (!reserve(pixelCount)) return false;

    const uint8_t red = calendar_logic::red(rgb);
    const uint8_t green = calendar_logic::green(rgb);
    const uint8_t blue = calendar_logic::blue(rgb);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
      rgb_[pixel * 3] = red;
      rgb_[pixel * 3 + 1] = green;
      rgb_[pixel * 3 + 2] = blue;
    }
    return dither_image(rgb_, width, height, eventDitherPalette(), DITHER_FS,
                        1.0f, false, indices_);
  }

  void warnOnce() {
    if (warned_) return;
    warned_ = true;
    LOG.println("[render] color dithering unavailable; using nearest panel ink");
  }

  uint8_t* rgb_ = nullptr;
  uint8_t* indices_ = nullptr;
  size_t capacity_ = 0;
  bool warned_ = false;
};

String ellipsize(EPaper& epaper, const std::string& value, int width) {
  return text_render::ellipsize(epaper, String(value.c_str()), width);
}

String formatTime(time_t value) {
  return String(calendar_logic::formatClockTime(
                    value, calendar_config::runtime::timeFormat())
                    .c_str());
}

String formatEventTime(const ::calendar::Event& event, time_t dayStart) {
  if (event.allDay) return "All day";
  if (event.end <= event.start) {
    return event.start < dayStart ? "Ongoing" : formatTime(event.start);
  }
  if (event.start < dayStart) return "Until " + formatTime(event.end);
  return String(calendar_logic::formatClockRange(
                    event.start, event.end,
                    calendar_config::runtime::timeFormat())
                    .c_str());
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

void drawHeader(EPaper& epaper, time_t now,
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
  epaper.setTextColor(PANEL_BLACK, PANEL_HEADER_BACKGROUND, true);
  selectFont(epaper, FontSize::Medium);
  const String heading = formatDate(now, "%A, %e %B");
  const int calendarIconSize = config::ui(26);
  const int calendarIconGap = config::ui(8);
  const int headingLeft =
      config::PANEL_WIDTH / 2 - epaper.textWidth(heading) / 2;
  drawCalendarIcon(epaper,
                   headingLeft - calendarIconGap - calendarIconSize,
                   height / 2 - calendarIconSize * 7 / 16,
                   calendarIconSize, PANEL_ACCENT);
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString(heading, config::PANEL_WIDTH / 2, height / 2);

  selectFont(epaper, FontSize::Small);
  const int batteryWidth = config::ui(22);
  const int batteryHeight = config::ui(12);
  const int terminalWidth = std::max(3, config::ui(5));
  const int batteryX =
      config::PANEL_WIDTH - margin - terminalWidth - batteryWidth;
  const int batteryY = std::max(3, margin / 2);
  const int outline = std::max(1, config::ui(1));
  const int terminalHeight = std::max(3, config::ui(5));
  const int batteryPct = indoor.batteryValid ? indoor.batteryPct : -1;
  const String percent =
      indoor.batteryValid ? String(indoor.batteryPct) + "%" : "--%";

  epaper.setTextColor(PANEL_BLACK);
  epaper.setTextDatum(MR_DATUM);
  const int percentRight = batteryX - config::ui(9);
  epaper.drawString(percent, percentRight, batteryY + batteryHeight / 2);
  text_render::drawBatteryGauge(
      epaper, batteryX, batteryY, batteryWidth, batteryHeight, batteryPct,
      outline, terminalWidth, terminalHeight, PANEL_BLACK,
      PANEL_WHITE,
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
  int weatherLeft;
  int weatherTop;
  int weatherWidth;
  int weatherHeight;
};

BodyGeometry bodyGeometry() {
  BodyGeometry value{};
#if RETERMINAL_MODEL == 1003
  value = {32, 156, 1340, 1192,
           1404, 156, 436,
           1404, 988, 436, 360};
#elif RETERMINAL_MODEL == 1004
  value = {24, 132, 1168, 1044,
           1216, 132, 360,
           1216, 876, 360, 300};
#else
  value = {12, 76, 596, 390,
           620, 76, 168,
           620, 316, 168, 150};
#endif
  return value;
}

void drawIndoorClimate(EPaper& epaper, const BodyGeometry& body,
                       const sensors::Readings& indoor, int top, int height) {
  epaper.drawFastHLine(body.weatherLeft, top, body.weatherWidth, PANEL_BLACK);
  selectFont(epaper, FontSize::Tiny);
  epaper.setTextColor(PANEL_BLACK, PANEL_WEATHER_BACKGROUND, true);
  if (!indoor.climateValid) {
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString("Indoor --",
                      body.weatherLeft + body.weatherWidth / 2,
                      top + height / 2);
    return;
  }

  const int iconHeight = std::max(8, config::ui(11));
  const int gap = config::ui(5);
  const int halfWidth = body.weatherWidth / 2;
  const int centerY = top + height / 2;
  const String indoorTemperature = temperature(indoor.temperatureC);
  const String humidity = String(indoor.humidityPct, 0) + "%";

  epaper.setTextDatum(ML_DATUM);
  const int temperatureGroupWidth =
      iconHeight + gap + epaper.textWidth(indoorTemperature);
  const int temperatureLeft =
      body.weatherLeft + (halfWidth - temperatureGroupWidth) / 2;
  drawThermometerIcon(epaper, temperatureLeft + iconHeight / 2, centerY,
                      iconHeight, COLOR_TEMPERATURE);
  epaper.drawString(indoorTemperature,
                    temperatureLeft + iconHeight + gap, centerY);

  const int humidityGroupWidth = iconHeight + gap + epaper.textWidth(humidity);
  const int humidityLeft =
      body.weatherLeft + halfWidth +
      (body.weatherWidth - halfWidth - humidityGroupWidth) / 2;
  drawDropletIcon(epaper, humidityLeft + iconHeight / 2, centerY,
                  iconHeight, COLOR_HUMIDITY);
  epaper.drawString(humidity, humidityLeft + iconHeight + gap, centerY);
}

void drawWeatherCard(EPaper& epaper, ColorDitherer& ditherer,
                     const BodyGeometry& body, const WeatherData& weather,
                     const sensors::Readings& indoor) {
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
      34;
#endif
  const int climateHeight =
#if RETERMINAL_MODEL == 1003
      52;
#elif RETERMINAL_MODEL == 1004
      44;
#else
      28;
#endif
  const int detailStep =
#if RETERMINAL_MODEL == 1003
      44;
#elif RETERMINAL_MODEL == 1004
      38;
#else
      25;
#endif

  ditherer.fillRect(epaper, body.weatherLeft + 1, body.weatherTop + 1,
                   body.weatherWidth - 2, headerHeight - 1,
                   WEATHER_HEADER_RGB);
  epaper.drawFastHLine(body.weatherLeft, body.weatherTop + headerHeight,
                      body.weatherWidth, PANEL_BLACK);
  const int locationIconSize = std::max(10, config::ui(13));
  const int locationIconX =
      body.weatherLeft + margin + locationIconSize / 2;
  const int headerCenterY = body.weatherTop + headerHeight / 2;
  drawLocationIcon(epaper, locationIconX, headerCenterY,
                   locationIconSize, PANEL_ACCENT,
                   nearestCalendarInk(WEATHER_HEADER_RGB));
  epaper.setTextColor(PANEL_BLACK);
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
  const int climateTop = cardBottom - climateHeight;
  drawIndoorClimate(epaper, body, indoor, climateTop, climateHeight);
  int contentBottom = climateTop - config::ui(5);
  if (!weather.alertTitle.isEmpty()) {
    const int alertTop = climateTop - alertHeight;
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
  weather_icons::draw(epaper, iconCenterX, iconCenterY, iconSize,
                      weather.weatherCode, weather.isDay, PANEL_BLACK);

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
  int y = contentTop + margin + iconSize +
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
          4;
#else
          config::ui(16);
#endif
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

std::vector<const ::calendar::Event*> upcomingEvents(
    const ::calendar::Data& data, time_t dayStart) {
  std::vector<const ::calendar::Event*> result;
  const time_t tomorrow = calendar_logic::addLocalDays(dayStart, 1);
  for (const auto& event : data.events) {
    if (event.start >= tomorrow) result.push_back(&event);
  }
  return result;
}

String eventStartDate(time_t value, bool compact = false) {
  struct tm local = {};
  if (localtime_r(&value, &local) == nullptr) return "--";
  if (compact) return String(local.tm_mday) + "/" + String(local.tm_mon + 1);
  String label = formatDate(value, "%e %b");
  label.trim();
  return label;
}

String upcomingEventTime(const ::calendar::Event& event) {
  if (event.allDay) return "All day";
  if (event.end <= event.start) return formatTime(event.start);
  return String(calendar_logic::formatClockRange(
                   event.start, event.end,
                   calendar_config::runtime::timeFormat())
                   .c_str());
}

struct AgendaCard {
  int left;
  int top;
  int width;
  int height;
};

void drawAgendaCard(EPaper& epaper, ColorDitherer& ditherer,
                   const AgendaCard& card, const String& heading,
                   const String& headerDetail,
                   const std::vector<const ::calendar::Event*>& events,
                   time_t dayStart, bool upcoming) {
  int headerHeight = kAgendaHeaderHeight;
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
  if (upcoming) headerHeight = 28;
#endif
  epaper.fillRect(card.left, card.top, card.width, card.height, PANEL_WHITE);
  epaper.drawRect(card.left, card.top, card.width, card.height, PANEL_BLACK);
  const int headerIconSize = std::max(10, config::ui(14));

  const int contentLeft = card.left + kAgendaMargin;
  ditherer.fillRect(epaper, card.left + 1, card.top + 1, card.width - 2,
                   headerHeight - 1, CALENDAR_HEADER_RGB);
  const uint32_t headerTextInk = eventTextInk(CALENDAR_HEADER_RGB);
  epaper.setTextColor(headerTextInk);
#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
  selectFont(epaper, FontSize::Small);
#else
  selectFont(epaper, FontSize::Tiny);
#endif
  drawCalendarIcon(epaper, contentLeft,
                   card.top +
                       (headerHeight - headerIconSize * 7 / 8) / 2,
                   headerIconSize, headerTextInk);
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString(heading,
                    contentLeft + headerIconSize + config::ui(6),
                    card.top + headerHeight / 2);
  if (!headerDetail.isEmpty()) {
    epaper.setTextDatum(MR_DATUM);
    epaper.drawString(headerDetail, card.left + card.width - kAgendaMargin,
                      card.top + headerHeight / 2);
  }
  epaper.drawFastHLine(card.left, card.top + headerHeight, card.width,
                      PANEL_BLACK);

  if (events.empty()) {
    selectFont(epaper, FontSize::Tiny, false);
    epaper.setTextColor(PANEL_MUTED, PANEL_WHITE, true);
    epaper.setTextDatum(TL_DATUM);
    const String emptyLabel = upcoming ? "No upcoming events" : "No events";
    epaper.drawString(
        text_render::ellipsize(epaper, emptyLabel,
                              card.width - kAgendaMargin * 2),
        contentLeft, card.top + headerHeight + kAgendaMargin);
    return;
  }

  const int contentHeight = card.height - headerHeight;
  const int rowHeight = std::min(kAgendaRowHeight, contentHeight);
  const int capacity =
      rowHeight > 0 ? std::max(1, contentHeight / rowHeight) : 0;
  if (capacity == 0) return;
  selectFont(epaper, FontSize::Tiny, false);
  const int moreLineHeight = epaper.fontHeight(1) + config::ui(4);
  const int shown = calendar_logic::agendaVisibleRows(
      static_cast<int>(events.size()), contentHeight, rowHeight,
      moreLineHeight);
  int y = card.top + headerHeight;
  for (int index = 0; index < shown; ++index) {
    const ::calendar::Event& event = *events[index];
    const int barLeft = card.left + 1;
    const int barTop = y + 2;
    const int barWidth = card.width - 2;
    const int barHeight = rowHeight - 4;
    const int textPadding = config::ui(5);
    const int textLeft = barLeft + textPadding;
    const int textWidth = barWidth - 2 * textPadding;
    ditherer.fillRect(epaper, barLeft, barTop, barWidth, barHeight,
                      event.colorRgb);
    selectFont(epaper, FontSize::Tiny, false);
    epaper.setTextColor(eventTextInk(event.colorRgb));
    const int metadataY = barTop + barHeight / 4;
    const int titleY = barTop + barHeight * 3 / 4;
    epaper.setTextDatum(ML_DATUM);
    if (upcoming) {
      String dateLabel = eventStartDate(event.start);
      String timeLabel = upcomingEventTime(event);
      if (epaper.textWidth(dateLabel) + config::ui(4) +
              epaper.textWidth(timeLabel) >
          textWidth) {
        dateLabel = eventStartDate(event.start, true);
      }
      epaper.drawString(dateLabel, textLeft, metadataY);
      epaper.setTextDatum(MR_DATUM);
      epaper.drawString(timeLabel, barLeft + barWidth - textPadding, metadataY);
      epaper.setTextDatum(ML_DATUM);
    } else {
      const String timeLabel = formatEventTime(event, dayStart);
      epaper.drawString(text_render::ellipsize(epaper, timeLabel, textWidth),
                      textLeft, metadataY);
    }
    selectFont(epaper, FontSize::Tiny, true);
    epaper.drawString(ellipsize(epaper, event.title, textWidth),
                      textLeft, titleY);
    y += rowHeight;
  }
  const bool showMore = shown < static_cast<int>(events.size()) &&
                        contentHeight - shown * rowHeight >= moreLineHeight;
  if (showMore) {
    selectFont(epaper, FontSize::Tiny);
    epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString("+" + String(events.size() - shown) + " more",
                      contentLeft,
                      y + (card.top + card.height - y) / 2);
  }
}

void drawAgendaCards(EPaper& epaper, ColorDitherer& ditherer,
                     const ::calendar::Data& data, const BodyGeometry& body,
                     time_t dayStart, int weekBottom, int monthTop) {
  const int todayHeight = weekBottom - body.dayTop;
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
  const int upcomingBottom = body.weatherTop;
#else
  const int upcomingBottom = body.weatherTop - kAgendaCardGap;
#endif
  const AgendaCard today{
      body.dayLeft, body.dayTop, body.dayWidth, todayHeight};
  const AgendaCard upcoming{
      body.dayLeft,
      monthTop,
      body.dayWidth,
      upcomingBottom - monthTop,
  };

  drawAgendaCard(epaper, ditherer, today, "TODAY",
                 eventStartDate(dayStart), eventsForDay(data, dayStart),
                 dayStart, false);
  constexpr const char* kUpcomingHeading = "UPCOMING";
  drawAgendaCard(epaper, ditherer, upcoming, kUpcomingHeading, "",
                 upcomingEvents(data, dayStart), dayStart, true);
}

const char* weekdayLabel(int index, config::WeekStart weekStart) {
  static const char* kMonday[] = {"Mon", "Tue", "Wed", "Thu",
                                   "Fri", "Sat", "Sun"};
  static const char* kSunday[] = {"Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat"};
  return weekStart == config::WeekStart::Sunday ? kSunday[index]
                                                 : kMonday[index];
}

void drawGrid(EPaper& epaper, ColorDitherer& ditherer,
              const ::calendar::Data& data, const BodyGeometry& body,
              time_t gridStart, int rows, config::WeekStart weekStart,
              time_t now, bool monthView) {
  const int headerHeight =
#if RETERMINAL_MODEL == 1003
      72;
#elif RETERMINAL_MODEL == 1004
      52;
#else
      34;
#endif
  const int gridHeight = body.height - headerHeight;
  ditherer.fillRect(epaper, body.left, body.top, body.width, headerHeight,
                    CALENDAR_HEADER_RGB);
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(eventTextInk(CALENDAR_HEADER_RGB));
  selectFont(epaper, FontSize::Tiny);
  for (int column = 0; column < 7; ++column) {
    const int columnLeft = body.left + body.width * column / 7;
    const int columnRight = body.left + body.width * (column + 1) / 7;
    epaper.drawString(weekdayLabel(column, weekStart),
                      (columnLeft + columnRight) / 2,
                      body.top + headerHeight / 2);
  }
  epaper.drawFastHLine(body.left, body.top + headerHeight, body.width,
                      PANEL_BLACK);

  const time_t monthAnchor = calendar_logic::startOfMonth(now);
  uint32_t calendarBackgroundRgb = 0;
  const bool haveCalendarBackground =
      calendar_config::runtime::showSingleCalendarBackground() &&
      calendar_logic::singleGoogleCalendarColor(data, calendarBackgroundRgb);
  const uint32_t calendarBackgroundText =
      eventTextInk(calendarBackgroundRgb);
  struct tm anchorTm = {};
  localtime_r(&monthAnchor, &anchorTm);
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < 7; ++column) {
      const int cell = row * 7 + column;
      const time_t day = calendar_logic::addLocalDays(gridStart, cell);
      const int x = body.left + body.width * column / 7;
      const int nextX = body.left + body.width * (column + 1) / 7;
      const int cellWidth = nextX - x;
      const int y =
          body.top + headerHeight + gridHeight * row / rows;
      const int nextY =
          body.top + headerHeight + gridHeight * (row + 1) / rows;
      const int cellHeight = nextY - y;
      struct tm dayTm = {};
      localtime_r(&day, &dayTm);
      const bool today = calendar_logic::sameLocalDate(day, now);
      const bool weekend = dayTm.tm_wday == 0 || dayTm.tm_wday == 6;
      if (haveCalendarBackground) {
        ditherer.fillRect(epaper, x + 1, y + 1, cellWidth - 1,
                          cellHeight - 1, calendarBackgroundRgb);
      } else if (weekend && !today) {
        ditherer.fillRect(epaper, x + 1, y + 1, cellWidth - 1,
                          cellHeight - 1, WEEKEND_BACKGROUND_RGB);
      }
      if (column > 0) epaper.drawFastVLine(x, y, cellHeight, PANEL_MUTED);
      if (row > 0) epaper.drawFastHLine(x, y, cellWidth, PANEL_MUTED);

      const bool outsideMonth =
          monthView && (dayTm.tm_year != anchorTm.tm_year ||
                        dayTm.tm_mon != anchorTm.tm_mon);
      if (today) {
        fillTodayRect(epaper, x + 1, y + 1, cellWidth - 1,
                      cellHeight - 1);
      }
      epaper.setTextDatum(TL_DATUM);
      const uint32_t dateInk =
          today ? PANEL_BLACK
          : haveCalendarBackground ? calendarBackgroundText
                                   : outsideMonth ? PANEL_MUTED : PANEL_BLACK;
      if (today) {
        epaper.setTextColor(dateInk);
      } else if (haveCalendarBackground) {
        epaper.setTextColor(dateInk);
      } else if (weekend) {
        epaper.setTextColor(dateInk);
      } else {
        epaper.setTextColor(dateInk, PANEL_WHITE, true);
      }
#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
      selectFont(epaper, FontSize::Small, today);
#else
      selectFont(epaper, FontSize::Medium, today);
#endif
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
      const int capacity = std::max(
          0, (cellHeight - (eventY - y) - 4) / lineHeight);
      if (capacity == 0) {
        const int dotSize =
#if RETERMINAL_MODEL == 1003
            10;
#elif RETERMINAL_MODEL == 1004
            8;
#else
            5;
#endif
        const int dotGap = std::max(2, dotSize / 2);
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
        const int dotAreaWidth = std::max(1, cellWidth - 36);
#else
        const int dotAreaWidth = cellWidth - 8;
#endif
        const int maxDots =
            std::max(1, (dotAreaWidth + dotGap) / (dotSize + dotGap));
        const int visible =
            std::min(static_cast<int>(dayEvents.size()), maxDots);
        const int dotY = y + cellHeight - dotSize - 3;
        const int dotStart =
            x + cellWidth - 4 - dotSize / 2 -
            (visible - 1) * (dotSize + dotGap);
        for (int index = 0; index < visible; ++index) {
          ditherer.fillCircle(
              epaper,
              dotStart + index * (dotSize + dotGap),
              dotY + dotSize / 2, std::max(1, dotSize / 2),
              dayEvents[index]->colorRgb);
        }
        continue;
      }
      int shown = 0;
      int eventCapacity = capacity;
      if (static_cast<int>(dayEvents.size()) > capacity && capacity > 1) {
        --eventCapacity;
      }
      for (const ::calendar::Event* event : dayEvents) {
        if (shown >= eventCapacity) break;
        const int barLeft = x + 1;
        const int barTop = eventY + 1;
        const int barWidth = cellWidth - 1;
        const int barHeight = lineHeight - 2;
        const int textPadding = config::ui(4);
        const int textX = barLeft + textPadding;
        ditherer.fillRect(epaper, barLeft, barTop, barWidth, barHeight,
                          event->colorRgb);
        selectFont(epaper, FontSize::Tiny, true);
        epaper.setTextColor(eventTextInk(event->colorRgb));
        epaper.setTextDatum(ML_DATUM);
        epaper.drawString(
            ellipsize(epaper, event->title,
                      barWidth - 2 * textPadding),
            textX, barTop + barHeight / 2);
        eventY += lineHeight;
        ++shown;
      }
      if (shown < static_cast<int>(dayEvents.size())) {
        selectFont(epaper, FontSize::Tiny);
        if (today) {
          epaper.setTextColor(PANEL_BLACK);
        } else if (haveCalendarBackground) {
          epaper.setTextColor(calendarBackgroundText);
        } else if (weekend) {
          epaper.setTextColor(PANEL_BLACK);
        } else {
          epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
        }
        epaper.setTextDatum(BR_DATUM);
        epaper.drawString("+" + String(dayEvents.size() - shown),
                          x + cellWidth - 4, y + cellHeight - 3);
      }
    }
  }
  epaper.drawRect(body.left, body.top, body.width, body.height, PANEL_BLACK);
  epaper.setTextDatum(TL_DATUM);
}

void drawFooter(EPaper& epaper, ColorDitherer& ditherer,
                const String& footer,
                const ::calendar::Data& data) {
  if (footer.isEmpty() &&
      !calendar_config::runtime::debugShowStatusBadges()) {
    return;
  }
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(1);
  epaper.setTextSize(1);
  constexpr int footerTextHeight = 8;
#else
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  epaper.setTextSize(1);
  constexpr int footerTextHeight = 16;
#endif
  String label = footer;
  if (calendar_config::runtime::debugShowStatusBadges()) {
    if (!label.isEmpty()) label += "  ";
    label += String(data.events.size()) + " events";
    if (data.truncated) label += " (limited)";
  }
  const int horizontalPadding = config::ui(5);
  const int verticalPadding = std::max(1, config::ui(2));
  const int badgeHeight = footerTextHeight + verticalPadding * 2;
  const int badgeWidth =
      std::min(config::PANEL_WIDTH,
               epaper.textWidth(label) + horizontalPadding * 2);
  const int badgeTop = config::PANEL_HEIGHT - badgeHeight;
  ditherer.fillRect(epaper, 0, badgeTop, badgeWidth, badgeHeight,
                    STATUS_BACKGROUND_RGB);
  epaper.setTextColor(PANEL_BLACK);
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString(label, horizontalPadding,
                    badgeTop + badgeHeight / 2);
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
  constexpr int kMaximumDetailLines = 4;
  String detailLines[kMaximumDetailLines];
  int detailLineCount = 0;
  int detailStart = 0;
  while (detailStart <= static_cast<int>(detail.length()) &&
         detailLineCount < kMaximumDetailLines) {
    int detailEnd = detail.indexOf('\n', detailStart);
    if (detailEnd < 0) detailEnd = detail.length();
    String line = detail.substring(detailStart, detailEnd);
    line.trim();
    if (!line.isEmpty()) {
      detailLines[detailLineCount++] = text_render::ellipsize(
          epaper, line, config::PANEL_WIDTH - 60);
    }
    if (detailEnd >= static_cast<int>(detail.length())) break;
    detailStart = detailEnd + 1;
  }
  const int detailLineHeight = epaper.fontHeight(1) + config::ui(5);
  const int detailCenterY = config::PANEL_HEIGHT / 2 + 30;
  const int detailTop =
      detailCenterY - (detailLineCount - 1) * detailLineHeight / 2;
  for (int line = 0; line < detailLineCount; ++line) {
    epaper.drawString(detailLines[line], config::PANEL_WIDTH / 2,
                      detailTop + line * detailLineHeight);
  }
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
              const ::calendar::Window& window, config::WeekStart weekStart,
              time_t now,
              const sensors::Readings& indoor, const WeatherData& weather,
              const String& footer) {
  epaper.fillSprite(PANEL_WHITE);
  drawHeader(epaper, now, indoor);
  const BodyGeometry body = bodyGeometry();
  ColorDitherer ditherer;
  BodyGeometry week = body;
  BodyGeometry month = body;
#if RETERMINAL_MODEL == 1003
  constexpr int sectionGap = 24;
  constexpr int weekHeight = 420;
#elif RETERMINAL_MODEL == 1004
  constexpr int sectionGap = 20;
  constexpr int weekHeight = 360;
#else
  constexpr int sectionGap = 12;
  constexpr int weekHeight = 158;
#endif
  week.height = weekHeight;
  month.top = week.top + week.height + sectionGap;
  month.height = body.top + body.height - month.top;

  drawAgendaCards(epaper, ditherer, data, body,
                  calendar_logic::localMidnight(now),
                  week.top + week.height, month.top);
  drawWeatherCard(epaper, ditherer, body, weather, indoor);
  drawGrid(epaper, ditherer, data, week,
           calendar_logic::startOfWeek(now, weekStart), 1, weekStart, now,
           false);
  drawGrid(epaper, ditherer, data, month, window.start, 6, weekStart, now,
           true);
  drawFooter(epaper, ditherer, footer, data);
}

}  // namespace calendar_render
