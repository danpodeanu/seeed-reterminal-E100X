#include "calendar_render.h"

#include <TFT_eSPI.h>
#include <math.h>
#include <stdlib.h>

#include <algorithm>

#include "app_logger.h"
#include "calendar_config_runtime.h"
#include "calendar_latin_font.h"
#include "calendar_logic.h"
#include "calendar_portrait_layout.h"
#include "calendar_render_geometry.h"
#include "config.h"
#include "dither.h"
#include "driver.h"
#include "repo_qr.h"
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
constexpr int kAgendaRowHeight = 88;
#elif RETERMINAL_MODEL == 1004
constexpr int kAgendaMargin = 18;
constexpr int kAgendaHeaderHeight = 54;
constexpr int kAgendaRowHeight = 70;
#else
constexpr int kAgendaMargin = 10;
constexpr int kAgendaHeaderHeight = 36;
constexpr int kAgendaRowHeight = 48;
#endif

void selectFont(EPaper& epaper, FontSize size) {
  epaper.setTextSize(1);
  int pixelSize = 18;
#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
  if (size == FontSize::Large) {
    pixelSize = 48;
  } else if (size == FontSize::Medium) {
    pixelSize = 36;
  } else if (size == FontSize::Small) {
    pixelSize = 24;
  }
#elif RETERMINAL_MODEL == 1005
  if (size == FontSize::Large) {
    pixelSize = 36;
  } else if (size == FontSize::Medium) {
    pixelSize = 24;
  } else if (size == FontSize::Tiny) {
    pixelSize = 16;
  }
#else
  if (size == FontSize::Large) {
    pixelSize = 36;
  } else if (size == FontSize::Medium) {
    pixelSize = 24;
  }
#endif
  epaper.setFreeFont(calendar_latin_font::uiFont(pixelSize));
}

void fillTodayCell(EPaper& epaper,
                   const calendar_render_geometry::Rect& rect) {
  text_render::fillDitheredRect(
      epaper, rect.left, rect.top, rect.width, rect.height, config::PANEL_WIDTH,
      config::PANEL_HEIGHT, PANEL_TODAY_BASE,
      PANEL_TODAY_DITHER != PANEL_TODAY_BASE, PANEL_TODAY_DITHER, 5);
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
#elif RETERMINAL_MODEL == 1005
  return calendar_logic::luminance(rgb) >= 128 ? TFT_WHITE : TFT_BLACK;
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
#elif RETERMINAL_MODEL == 1005
  return PAL_BW;
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
#elif RETERMINAL_MODEL == 1005
  return code == 0 ? TFT_BLACK : TFT_WHITE;
#else
  return TFT_GRAY_0 + std::min<uint8_t>(3, code);
#endif
}

#if RETERMINAL_MODEL == 1005
constexpr uint8_t kMonochromeBayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

uint8_t monochromeDitherThreshold(uint32_t rgb) {
  const uint16_t darkness =
      static_cast<uint16_t>(255 - calendar_logic::luminance(rgb));
  return static_cast<uint8_t>(std::min<uint16_t>(16, (darkness + 15) / 16));
}

bool monochromeDitherPixel(uint32_t rgb, int x, int y) {
  return kMonochromeBayer4[y & 3][x & 3] <
         monochromeDitherThreshold(rgb);
}
#endif

class ColorDitherer {
 public:
  ~ColorDitherer() {
    free(rgb_);
    free(indices_);
  }

  void fillRect(EPaper& epaper, int left, int top, int width, int height,
                uint32_t rgb) {
#if RETERMINAL_MODEL == 1005
    const uint8_t threshold = monochromeDitherThreshold(rgb);
    text_render::fillDitheredRect(
        epaper, left, top, width, height, config::PANEL_WIDTH,
        config::PANEL_HEIGHT, PANEL_WHITE, threshold != 0, PANEL_BLACK,
        threshold);
#else
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
#endif
  }

  void fillCircle(EPaper& epaper, int centerX, int centerY, int radius,
                  uint32_t rgb) {
#if RETERMINAL_MODEL == 1005
    const int radiusSquared = radius * radius;
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        if (x * x + y * y > radiusSquared) continue;
        const int panelX = centerX + x;
        const int panelY = centerY + y;
        epaper.drawPixel(panelX, panelY,
                         monochromeDitherPixel(rgb, panelX, panelY)
                             ? PANEL_BLACK
                             : PANEL_WHITE);
      }
    }
#else
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
#endif
  }

  void fillRoundedRect(EPaper& epaper, int left, int top, int width,
                       int height, int radius, uint32_t rgb) {
    radius = std::min(radius, std::min(width, height) / 2);
    if (radius <= 0) {
      fillRect(epaper, left, top, width, height, rgb);
      return;
    }
    fillRect(epaper, left + radius, top, width - radius * 2, height, rgb);
    fillRect(epaper, left, top + radius, width, height - radius * 2, rgb);
    fillCircle(epaper, left + radius, top + radius, radius, rgb);
    fillCircle(epaper, left + width - radius - 1, top + radius, radius, rgb);
    fillCircle(epaper, left + radius, top + height - radius - 1, radius, rgb);
    fillCircle(epaper, left + width - radius - 1,
               top + height - radius - 1, radius, rgb);
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

void drawHeader(EPaper& epaper, config::CalendarView view, time_t now,
                time_t displayDay,
                const sensors::Readings& indoor) {
#if RETERMINAL_MODEL == 1003
  constexpr int height = 132;
  constexpr int margin = 38;
#elif RETERMINAL_MODEL == 1004
  constexpr int height = 112;
  constexpr int margin = 26;
#elif RETERMINAL_MODEL == 1005
  constexpr int height = calendar_portrait_layout::HEADER_HEIGHT;
  constexpr int margin = 12;
#else
  constexpr int height = 64;
  constexpr int margin = 16;
#endif
  epaper.fillRect(0, 0, config::PANEL_WIDTH, height,
                  PANEL_HEADER_BACKGROUND);
  epaper.drawFastHLine(0, height - 1, config::PANEL_WIDTH, PANEL_BLACK);
  epaper.setTextColor(PANEL_BLACK, PANEL_HEADER_BACKGROUND, true);
  selectFont(epaper, FontSize::Medium);
#if RETERMINAL_MODEL == 1005
  String heading;
  if (view == config::CalendarView::Month) {
    heading = formatDate(displayDay, "%B %Y");
  } else if (view == config::CalendarView::Week) {
    String weekDate = formatDate(
        calendar_logic::startOfWeek(
            displayDay, calendar_config::runtime::weekStart()),
        "%e %b");
    weekDate.trim();
    heading = "Week of " + weekDate;
  } else {
    heading = formatDate(displayDay, "%a, %e %b");
  }
  constexpr int headingSurfaceWidth = config::PANEL_WIDTH - 96;
  constexpr int calendarIconSize = 22;
#else
  static_cast<void>(view);
  const String heading = formatDate(now, "%A, %e %B");
  const int calendarIconSize = config::ui(26);
#endif
  const int calendarIconGap = config::ui(8);
  const int headingWidth = epaper.textWidth(heading);
#if RETERMINAL_MODEL == 1005
  const calendar_render_geometry::HeaderGroup headingGroup =
      calendar_render_geometry::centeredHeaderGroup(
          headingSurfaceWidth, calendarIconSize, calendarIconGap, headingWidth);
#else
  const calendar_render_geometry::HeaderGroup headingGroup =
      calendar_render_geometry::centeredHeaderGroup(
          config::PANEL_WIDTH, calendarIconSize, calendarIconGap, headingWidth);
#endif
  drawCalendarIcon(epaper,
                   headingGroup.iconLeft,
                   height / 2 - calendarIconSize * 7 / 16,
                   calendarIconSize, PANEL_ACCENT);
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString(heading, headingGroup.textLeft, height / 2);

  selectFont(epaper, FontSize::Small);
  const int batteryWidth = config::ui(22);
  const int batteryHeight = config::ui(12);
  const int terminalWidth = std::max(3, config::ui(5));
  const int batteryX =
      config::PANEL_WIDTH - margin - terminalWidth - batteryWidth;
#if RETERMINAL_MODEL == 1005
  const int batteryY = height / 2 + 2 - batteryHeight / 2;
  const int percentY = height / 2;
#else
  const int batteryY = std::max(3, margin / 2);
  const int percentY = batteryY + batteryHeight / 2;
#endif
  const int outline = std::max(1, config::ui(1));
  const int terminalHeight = std::max(3, config::ui(5));
  const int batteryPct = indoor.batteryValid ? indoor.batteryPct : -1;
  const String percent =
      indoor.batteryValid ? String(indoor.batteryPct) + "%" : "--%";

  epaper.setTextColor(PANEL_BLACK);
  epaper.setTextDatum(MR_DATUM);
  const int percentRight = batteryX - config::ui(9);
  epaper.drawString(percent, percentRight, percentY);
  text_render::drawBatteryGauge(
      epaper, batteryX, batteryY, batteryWidth, batteryHeight, batteryPct,
      outline, terminalWidth, terminalHeight, PANEL_BLACK,
      PANEL_WHITE,
#if RETERMINAL_MODEL == 1005
      false);
#else
      indoor.externalPowerValid && indoor.externalPower);
#endif
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
           620, 328, 168, 138};
#endif
  return value;
}

void drawIndoorClimate(EPaper& epaper, const BodyGeometry& body,
                       const sensors::Readings& indoor, int top, int height) {
  selectFont(epaper, FontSize::Tiny);
  epaper.setTextColor(PANEL_BLACK);
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
      26;
#endif
  const int iconSize =
#if RETERMINAL_MODEL == 1003
      96;
#elif RETERMINAL_MODEL == 1004
      78;
#elif RETERMINAL_MODEL == 1005
      64;
#else
      38;
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
      24;
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
    selectFont(epaper, FontSize::Small);
    epaper.setTextColor(PANEL_BLACK);
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
  epaper.setTextColor(PANEL_BLACK);
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
  selectFont(epaper, FontSize::Tiny);
#else
  selectFont(epaper, FontSize::Small);
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
  const time_t upcomingEnd = calendar_logic::addLocalDays(dayStart, 43);
  for (const auto& event : data.events) {
    if (event.start >= tomorrow && event.start < upcomingEnd) {
      result.push_back(&event);
    }
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
  if (upcoming) headerHeight = 24;
#endif
  const int headerIconSize = std::max(10, config::ui(14));

  const int contentLeft = card.left + kAgendaMargin;
  ditherer.fillRect(epaper, card.left, card.top, card.width, headerHeight,
                   CALENDAR_HEADER_RGB);
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
#if RETERMINAL_MODEL == 1005
    const int detailRight = card.left + card.width - kAgendaMargin;
    const int detailLeft = detailRight - epaper.textWidth(headerDetail);
    const int separator = headerDetail.indexOf(' ');
    const String dayNumber =
        separator < 0 ? headerDetail : headerDetail.substring(0, separator);

    // Remove only Bayer pixels that visually continue the 16 px digit 1 stem.
    constexpr int kHeaderBaselineOffset = 7;
    const int clearTop =
        card.top + headerHeight / 2 + kHeaderBaselineOffset;
    const int digitOneWidth = epaper.textWidth("1");
    const int stemWidth = std::max(1, digitOneWidth / 3);
    for (unsigned int index = 0; index < dayNumber.length(); ++index) {
      if (dayNumber[index] != '1') continue;
      const int digitLeft =
          detailLeft + epaper.textWidth(dayNumber.substring(0, index));
      const int stemLeft = digitLeft + digitOneWidth / 2;
      for (int y = clearTop; y < card.top + headerHeight; ++y) {
        for (int x = stemLeft; x < stemLeft + stemWidth; ++x) {
          if (monochromeDitherPixel(CALENDAR_HEADER_RGB, x, y)) {
            epaper.drawPixel(x, y, PANEL_WHITE);
          }
        }
      }
    }
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(headerDetail, detailLeft,
                      card.top + headerHeight / 2);
#else
    epaper.setTextDatum(MR_DATUM);
    epaper.drawString(headerDetail, card.left + card.width - kAgendaMargin,
                      card.top + headerHeight / 2);
#endif
  }
  if (events.empty()) {
    selectFont(epaper, FontSize::Tiny);
    epaper.setTextColor(PANEL_MUTED);
    epaper.setTextDatum(TL_DATUM);
    const String emptyLabel = upcoming ? "No upcoming events" : "No events";
    epaper.drawString(
        text_render::ellipsize(epaper, emptyLabel,
                              card.width - kAgendaMargin * 2),
        contentLeft, card.top + headerHeight + kAgendaMargin);
    return;
  }

  const int contentHeight = card.height - headerHeight;
  selectFont(epaper, FontSize::Tiny);
  const int moreLineHeight = epaper.fontHeight(1) + config::ui(2);
  const int minimumRowHeight =
      upcoming ? kAgendaRowHeight - config::ui(4) : kAgendaRowHeight;
  const calendar_logic::AgendaLayout layout = calendar_logic::agendaLayout(
      static_cast<int>(events.size()), contentHeight, kAgendaRowHeight,
      minimumRowHeight, moreLineHeight);
  if (layout.visibleRows == 0) return;
  const int rowHeight = layout.rowHeight;
  const int shown = layout.visibleRows;
  int y = card.top + headerHeight;
  for (int index = 0; index < shown; ++index) {
    const ::calendar::Event& event = *events[index];
    const calendar_render_geometry::Rect band =
        calendar_render_geometry::agendaBand(
            card.left, y, card.width, rowHeight, kAgendaRowHeight,
            config::ui(2), config::ui(3), upcoming);
    const int barLeft = band.left;
    const int barTop = band.top;
    const int barWidth = band.width;
    const int barHeight = band.height;
    const int radius = std::min(config::ui(8), barHeight / 2);
    const int textPadding = std::max(config::ui(7), radius);
#if RETERMINAL_MODEL == 1005
    const int colorStripeWidth = 7;
    epaper.fillRect(barLeft, barTop, barWidth, barHeight, PANEL_WHITE);
    epaper.drawRect(barLeft, barTop, barWidth, barHeight, PANEL_BLACK);
    ditherer.fillRect(epaper, barLeft + 1, barTop + 1, colorStripeWidth,
                      std::max(1, barHeight - 2), event.colorRgb);
    const int textLeft = barLeft + colorStripeWidth + textPadding;
    const int textWidth =
        std::max(0, barWidth - colorStripeWidth - textPadding * 2);
    constexpr uint32_t eventInk = PANEL_BLACK;
#else
    const int textLeft = barLeft + textPadding;
    const int textWidth = barWidth - 2 * textPadding;
    ditherer.fillRoundedRect(epaper, barLeft, barTop, barWidth, barHeight,
                             radius, event.colorRgb);
    const uint32_t eventInk = eventTextInk(event.colorRgb);
#endif
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
    selectFont(epaper, FontSize::Tiny);
#else
    selectFont(epaper, FontSize::Small);
#endif
    epaper.setTextColor(eventInk);
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
    const String title = calendar_latin_font::ellipsize(
        event.title, textWidth, calendar_latin_font::Size::Agenda);
    calendar_latin_font::drawLeftMiddle(
        epaper, title, textLeft, titleY, calendar_latin_font::Size::Agenda,
        eventInk);
    y += rowHeight;
  }
  if (layout.showMore) {
    selectFont(epaper, FontSize::Tiny);
    epaper.setTextColor(PANEL_BLACK);
    epaper.setTextDatum(MR_DATUM);
    epaper.drawString("+" + String(events.size() - shown) + " more",
                      card.left + card.width - kAgendaMargin,
                      y + (card.top + card.height - y) / 2);
  }
}

void drawAgendaCards(EPaper& epaper, ColorDitherer& ditherer,
                     const ::calendar::Data& data, const BodyGeometry& body,
                     time_t dayStart, int weekBottom, int monthTop) {
  const int todayHeight = weekBottom - body.dayTop;
  const int upcomingBottom =
      body.weatherTop - (monthTop - weekBottom);
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
#if RETERMINAL_MODEL == 1003
  constexpr int kMonthHeaderHeight = 72;
#elif RETERMINAL_MODEL == 1004
  constexpr int kMonthHeaderHeight = 52;
#else
  constexpr int kMonthHeaderHeight = 34;
#endif
  const int headerHeight = monthView ? kMonthHeaderHeight : 0;
  const int gridHeight = body.height - headerHeight;
  const int cellInset = std::max(1, config::ui(2));
  if (monthView) {
    ditherer.fillRect(epaper, body.left, body.top, body.width, headerHeight,
                     CALENDAR_HEADER_RGB);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextColor(eventTextInk(CALENDAR_HEADER_RGB));
    epaper.setTextSize(1);
    epaper.setFreeFont(calendar_latin_font::uiFont(24));
    for (int column = 0; column < 7; ++column) {
      const int columnLeft = body.left + body.width * column / 7;
      const int columnRight = body.left + body.width * (column + 1) / 7;
      epaper.drawString(weekdayLabel(column, weekStart),
                       (columnLeft + columnRight) / 2,
                       body.top + headerHeight / 2);
    }
  }
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
      const int y = body.top + headerHeight + gridHeight * row / rows;
      const int nextY =
          body.top + headerHeight + gridHeight * (row + 1) / rows;
      const int cellHeight = nextY - y;
      struct tm dayTm = {};
      localtime_r(&day, &dayTm);
      const bool today = calendar_logic::sameLocalDate(day, now);
      const bool weekend = dayTm.tm_wday == 0 || dayTm.tm_wday == 6;
      if (haveCalendarBackground) {
        ditherer.fillRect(epaper, x + cellInset, y + 1,
                          cellWidth - cellInset * 2, cellHeight - 1,
                          calendarBackgroundRgb);
      } else if (weekend && !today) {
        ditherer.fillRect(epaper, x + cellInset, y + 1,
                          cellWidth - cellInset * 2, cellHeight - 1,
                          WEEKEND_BACKGROUND_RGB);
      }
      if (row > 0 && column == 0) {
        ditherer.fillRect(epaper, body.left, y, body.width, 1, GRID_RULE_RGB);
      }

      const bool outsideMonth =
          monthView && (dayTm.tm_year != anchorTm.tm_year ||
                        dayTm.tm_mon != anchorTm.tm_mon);
      const calendar_render_geometry::Rect cellInterior =
          calendar_render_geometry::gridCellInterior(
              x, y, cellWidth, cellHeight, cellInset);
      if (today) {
        fillTodayCell(epaper, cellInterior);
      }
      epaper.setTextDatum(TL_DATUM);
      const uint32_t dateInk =
          today ? PANEL_BLACK
          : haveCalendarBackground ? calendarBackgroundText
                                   : outsideMonth ? PANEL_MUTED : PANEL_BLACK;
      epaper.setTextColor(dateInk);
#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
      selectFont(epaper, FontSize::Small);
#else
      selectFont(epaper, FontSize::Medium);
#endif
      String dayLabel;
      if (monthView) {
        dayLabel = String(dayTm.tm_mday);
      } else {
        dayLabel = weekdayLabel(column, weekStart);
        dayLabel += " ";
        dayLabel += String(dayTm.tm_mday);
      }
      epaper.drawString(dayLabel, x + cellInset + config::ui(3),
                        calendar_render_geometry::gridDayLabelTop(
                            y, config::ui(3), config::ui(2), monthView));

      const auto dayEvents = eventsForDay(data, day);
      int eventY = y +
#if RETERMINAL_MODEL == 1003
          54;
#elif RETERMINAL_MODEL == 1004
          38;
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
        const int barInset = cellInset + config::ui(2);
        const int verticalGap = std::max(1, config::ui(2));
        const int barLeft = x + barInset;
        const int barTop = eventY + verticalGap;
        const int barWidth = cellWidth - barInset * 2;
        const int barHeight = lineHeight - verticalGap * 2;
        const int radius = std::min(config::ui(5), barHeight / 2);
        const int textPadding = std::max(config::ui(5), radius);
        const int textX = barLeft + textPadding;
        ditherer.fillRoundedRect(epaper, barLeft, barTop, barWidth,
                                 barHeight, radius, event->colorRgb);
        const String title = calendar_latin_font::ellipsize(
            event->title, barWidth - 2 * textPadding,
            calendar_latin_font::Size::Grid);
        calendar_latin_font::drawLeftMiddle(
            epaper, title, textX, barTop + barHeight / 2,
            calendar_latin_font::Size::Grid,
            eventTextInk(event->colorRgb));
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
          epaper.setTextColor(PANEL_BLACK);
        }
        epaper.setTextDatum(BR_DATUM);
        epaper.drawString("+" + String(dayEvents.size() - shown),
                          x + cellWidth - cellInset - config::ui(3),
                          y + cellHeight - config::ui(3));
      }
    }
  }
  epaper.setTextDatum(TL_DATUM);
}

#if RETERMINAL_MODEL == 1005
calendar_render_geometry::Rect portraitRect(
    const calendar_portrait_layout::Rect& rect) {
  return {rect.left, rect.top, rect.width, rect.height};
}

void drawPortraitEventBand(
    EPaper& epaper, ColorDitherer& ditherer,
    const ::calendar::Event& event, int left, int top, int width, int height,
    const String& label) {
  constexpr int colorStripeWidth = 7;
  constexpr int textPadding = 7;
  epaper.fillRect(left, top, width, height, PANEL_WHITE);
  epaper.drawRect(left, top, width, height, PANEL_BLACK);
  ditherer.fillRect(epaper, left + 1, top + 1, colorStripeWidth,
                    std::max(1, height - 2), event.colorRgb);
  const int textLeft = left + colorStripeWidth + textPadding;
  const int textWidth =
      std::max(0, width - colorStripeWidth - textPadding * 2);
  const String clipped = calendar_latin_font::ellipsize(
      std::string(label.c_str()), textWidth,
      calendar_latin_font::Size::Grid);
  calendar_latin_font::drawLeftMiddle(
      epaper, clipped, textLeft, top + height / 2,
      calendar_latin_font::Size::Grid, PANEL_BLACK);
}

void drawPortraitToday(EPaper& epaper, ColorDitherer& ditherer,
                       const ::calendar::Data& data, time_t displayDay,
                       const sensors::Readings& indoor,
                       const WeatherData& weather) {
  BodyGeometry weatherBody{};
  weatherBody.weatherLeft = calendar_portrait_layout::WEATHER.left;
  weatherBody.weatherTop = calendar_portrait_layout::WEATHER.top;
  weatherBody.weatherWidth = calendar_portrait_layout::WEATHER.width;
  weatherBody.weatherHeight = calendar_portrait_layout::WEATHER.height;
  drawWeatherCard(epaper, ditherer, weatherBody, weather, indoor);

  const time_t dayStart = calendar_logic::localMidnight(displayDay);
  const auto& today = calendar_portrait_layout::TODAY;
  const auto& upcoming = calendar_portrait_layout::UPCOMING;
  drawAgendaCard(
      epaper, ditherer,
      {today.left, today.top, today.width, today.height}, "TODAY",
      eventStartDate(dayStart), eventsForDay(data, dayStart), dayStart, false);
  drawAgendaCard(
      epaper, ditherer,
      {upcoming.left, upcoming.top, upcoming.width, upcoming.height},
      "UPCOMING", "", upcomingEvents(data, dayStart), dayStart, true);
}

void drawPortraitWeek(EPaper& epaper, ColorDitherer& ditherer,
                      const ::calendar::Data& data,
                      config::WeekStart weekStart, time_t displayDay,
                      time_t now) {
  constexpr int dateColumnWidth = 68;
  constexpr int bandGap = 4;
  const time_t weekStartTime =
      calendar_logic::startOfWeek(displayDay, weekStart);

  for (int index = 0; index < 7; ++index) {
    const calendar_portrait_layout::Rect row =
        calendar_portrait_layout::weekRow(index);
    const time_t day = calendar_logic::addLocalDays(weekStartTime, index);
    struct tm dayTm = {};
    localtime_r(&day, &dayTm);
    const bool today = calendar_logic::sameLocalDate(day, now);
    const bool weekend = dayTm.tm_wday == 0 || dayTm.tm_wday == 6;

    if (today) {
      fillTodayCell(epaper, portraitRect(row));
    } else if (weekend) {
      ditherer.fillRect(epaper, row.left, row.top, row.width, row.height,
                        WEEKEND_BACKGROUND_RGB);
    }
    if (index > 0) {
      epaper.drawFastHLine(row.left, row.top, row.width, PANEL_BLACK);
    }
    epaper.drawFastVLine(row.left + dateColumnWidth, row.top, row.height,
                        PANEL_BLACK);

    selectFont(epaper, FontSize::Small);
    epaper.setTextDatum(MC_DATUM);
    epaper.setTextColor(PANEL_BLACK);
    epaper.drawString(weekdayLabel(index, weekStart),
                      row.left + dateColumnWidth / 2,
                      row.top + row.height / 3);
    epaper.drawString(String(dayTm.tm_mday),
                      row.left + dateColumnWidth / 2,
                      row.top + row.height * 2 / 3);

    const auto dayEvents = eventsForDay(data, day);
    const int bandLeft = row.left + dateColumnWidth + 8;
    const int bandWidth = row.width - dateColumnWidth - 12;
    const int bandHeight = (row.height - 12 - bandGap) / 2;
    const int visible = std::min(2, static_cast<int>(dayEvents.size()));
    for (int eventIndex = 0; eventIndex < visible; ++eventIndex) {
      const ::calendar::Event& event = *dayEvents[eventIndex];
      String timeLabel;
      if (event.allDay) {
        timeLabel = "All day";
      } else if (event.start < day) {
        timeLabel = "Ongoing";
      } else {
        timeLabel = formatTime(event.start);
      }
      const String label =
          timeLabel + "  " + String(event.title.c_str());
      drawPortraitEventBand(
          epaper, ditherer, event, bandLeft,
          row.top + 6 + eventIndex * (bandHeight + bandGap), bandWidth,
          bandHeight, label);
    }
    if (static_cast<int>(dayEvents.size()) > visible) {
      selectFont(epaper, FontSize::Tiny);
      epaper.setTextDatum(BR_DATUM);
      epaper.drawString(
          "+" + String(dayEvents.size() - visible),
          row.left + dateColumnWidth - 5, row.top + row.height - 4);
    }
  }
  epaper.setTextDatum(TL_DATUM);
}

void drawPortraitMonth(EPaper& epaper, ColorDitherer& ditherer,
                       const ::calendar::Data& data,
                       config::WeekStart weekStart, time_t displayDay,
                       time_t now) {
  const auto& calendar = calendar_portrait_layout::CALENDAR;
  ditherer.fillRect(epaper, calendar.left, calendar.top, calendar.width,
                    calendar_portrait_layout::MONTH_HEADER_HEIGHT,
                    CALENDAR_HEADER_RGB);
  selectFont(epaper, FontSize::Tiny);
  epaper.setTextDatum(MC_DATUM);
  epaper.setTextColor(eventTextInk(CALENDAR_HEADER_RGB));
  for (int column = 0; column < 7; ++column) {
    const auto cell = calendar_portrait_layout::monthCell(0, column);
    epaper.drawString(
        weekdayLabel(column, weekStart), cell.left + cell.width / 2,
        calendar.top + calendar_portrait_layout::MONTH_HEADER_HEIGHT / 2);
  }

  uint32_t calendarBackgroundRgb = 0;
  const bool haveCalendarBackground =
      calendar_config::runtime::showSingleCalendarBackground() &&
      calendar_logic::singleGoogleCalendarColor(data, calendarBackgroundRgb);
  const uint32_t calendarBackgroundText =
      eventTextInk(calendarBackgroundRgb);
  const time_t monthAnchor = calendar_logic::startOfMonth(displayDay);
  const time_t gridStart =
      calendar_logic::startOfWeek(monthAnchor, weekStart);
  struct tm anchorTm = {};
  localtime_r(&monthAnchor, &anchorTm);

  for (int rowIndex = 0; rowIndex < 6; ++rowIndex) {
    for (int column = 0; column < 7; ++column) {
      const auto cell =
          calendar_portrait_layout::monthCell(rowIndex, column);
      const int cellIndex = rowIndex * 7 + column;
      const time_t day =
          calendar_logic::addLocalDays(gridStart, cellIndex);
      struct tm dayTm = {};
      localtime_r(&day, &dayTm);
      const bool today = calendar_logic::sameLocalDate(day, now);
      const bool weekend = dayTm.tm_wday == 0 || dayTm.tm_wday == 6;
      const bool outsideMonth =
          dayTm.tm_year != anchorTm.tm_year ||
          dayTm.tm_mon != anchorTm.tm_mon;

      if (haveCalendarBackground) {
        ditherer.fillRect(epaper, cell.left + 1, cell.top + 1,
                          cell.width - 1, cell.height - 1,
                          calendarBackgroundRgb);
      } else if (weekend) {
        ditherer.fillRect(epaper, cell.left + 1, cell.top + 1,
                          cell.width - 1, cell.height - 1,
                          WEEKEND_BACKGROUND_RGB);
      }
      epaper.drawRect(cell.left, cell.top, cell.width, cell.height,
                      PANEL_BLACK);

      selectFont(epaper, FontSize::Small);
      epaper.setTextDatum(MC_DATUM);
      const int dateCenterX = cell.left + cell.width / 2;
      const int dateCenterY = cell.top + 22;
      if (today) {
        epaper.fillCircle(dateCenterX, dateCenterY, 15, PANEL_BLACK);
        epaper.setTextColor(PANEL_WHITE);
      } else {
        epaper.setTextColor(
            haveCalendarBackground ? calendarBackgroundText : PANEL_BLACK);
      }
      String dateLabel = String(dayTm.tm_mday);
      if (outsideMonth) dateLabel = "(" + dateLabel + ")";
      epaper.drawString(dateLabel, dateCenterX, dateCenterY);

      const auto dayEvents = eventsForDay(data, day);
      constexpr int dotRadius = 4;
      constexpr int dotStep = 12;
      const int visible =
          std::min(4, static_cast<int>(dayEvents.size()));
      const int dotStartX =
          dateCenterX - (visible - 1) * dotStep / 2;
      const int dotY = cell.top + cell.height - 14;
      for (int eventIndex = 0; eventIndex < visible; ++eventIndex) {
        epaper.fillCircle(
            dotStartX + eventIndex * dotStep, dotY, dotRadius, PANEL_BLACK);
      }
      if (static_cast<int>(dayEvents.size()) > visible) {
        selectFont(epaper, FontSize::Tiny);
        epaper.setTextColor(
            haveCalendarBackground ? calendarBackgroundText : PANEL_BLACK);
        epaper.setTextDatum(BR_DATUM);
        epaper.drawString(
            "+" + String(dayEvents.size() - visible),
            cell.left + cell.width - 3, cell.top + cell.height - 3);
      }
    }
  }
  epaper.setTextDatum(TL_DATUM);
}

void drawPortraitFooter(EPaper& epaper, config::CalendarView view,
                        const String& footer,
                        const ::calendar::Data& data) {
  String diagnostics = footer;
  if (calendar_config::runtime::debugShowStatusBadges()) {
    if (!diagnostics.isEmpty()) diagnostics += " / ";
    diagnostics += String(data.events.size()) + " events";
    if (data.truncated) diagnostics += " (limited)";
  }
  if (diagnostics.isEmpty()) {
    diagnostics = "UP previous  |  OK today  |  DOWN next";
  }

  epaper.fillRect(
      0, calendar_portrait_layout::DIAGNOSTIC_TOP, config::PANEL_WIDTH,
      calendar_portrait_layout::NAVIGATION_TOP -
          calendar_portrait_layout::DIAGNOSTIC_TOP,
      PANEL_WHITE);
  if (!diagnostics.isEmpty()) {
    epaper.setTextSize(1);
    epaper.setFreeFont(calendar_latin_font::uiFont(10));
    epaper.setTextColor(PANEL_BLACK);
    epaper.setTextDatum(MC_DATUM);
    epaper.drawString(
        text_render::ellipsize(epaper, diagnostics, config::PANEL_WIDTH - 16),
        config::PANEL_WIDTH / 2,
        (calendar_portrait_layout::DIAGNOSTIC_TOP +
         calendar_portrait_layout::NAVIGATION_TOP) /
            2);
  }

  struct NavigationItem {
    config::CalendarView view;
    const char* label;
  };
  static constexpr NavigationItem kItems[] = {
      {config::CalendarView::Today, "Today"},
      {config::CalendarView::Week, "Week"},
      {config::CalendarView::Month, "Month"},
  };
  epaper.drawFastHLine(0, calendar_portrait_layout::NAVIGATION_TOP,
                       config::PANEL_WIDTH, PANEL_BLACK);
  selectFont(epaper, FontSize::Tiny);
  epaper.setTextDatum(MC_DATUM);
  for (int index = 0; index < 3; ++index) {
    const int left = config::PANEL_WIDTH * index / 3;
    const int right = config::PANEL_WIDTH * (index + 1) / 3;
    const bool selected = view == kItems[index].view;
    if (selected) {
      epaper.fillRect(
          left, calendar_portrait_layout::NAVIGATION_TOP, right - left,
          calendar_portrait_layout::NAVIGATION_HEIGHT, PANEL_BLACK);
    } else if (index > 0) {
      epaper.drawFastVLine(
          left, calendar_portrait_layout::NAVIGATION_TOP,
          calendar_portrait_layout::NAVIGATION_HEIGHT, PANEL_BLACK);
    }
    epaper.setTextColor(selected ? PANEL_WHITE : PANEL_BLACK);
    epaper.drawString(
        kItems[index].label, (left + right) / 2,
        calendar_portrait_layout::NAVIGATION_TOP +
            calendar_portrait_layout::NAVIGATION_HEIGHT / 2);
  }
  epaper.setTextDatum(TL_DATUM);
}
#endif

void drawFooter(EPaper& epaper, const String& footer,
                const ::calendar::Data& data) {
  if (footer.isEmpty() &&
      !calendar_config::runtime::debugShowStatusBadges()) {
    return;
  }
#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
  constexpr int footerPixelSize = 10;
#else
  constexpr int footerPixelSize = 16;
#endif
  epaper.setTextSize(1);
  epaper.setFreeFont(calendar_latin_font::uiFont(footerPixelSize));
  const int footerTextHeight = epaper.fontHeight(1);
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
  const calendar_render_geometry::Rect badge =
      calendar_render_geometry::footerBadge(
          config::PANEL_HEIGHT, badgeWidth, badgeHeight);
  calendar_render_geometry::fillPlainFooterBackground(
      epaper, badge, PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK);
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString(label, horizontalPadding,
                    badge.top + badge.height / 2);
  epaper.setTextDatum(TL_DATUM);
}

}  // namespace

void connectionStatus(EPaper& epaper, const String& title,
                      const String& detail, const String& deviceInfo,
                      const String& footer) {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);

  selectFont(epaper, FontSize::Large);
  epaper.drawString(
      text_render::ellipsize(
          epaper, title, config::PANEL_WIDTH - config::ui(60)),
      config::PANEL_WIDTH / 2,
      config::PANEL_HEIGHT / 2 - config::ui(15));

  if (!detail.isEmpty()) {
    selectFont(epaper, FontSize::Small);
    epaper.drawString(
        text_render::ellipsize(
            epaper, detail, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 + config::ui(25));
  }

  selectFont(epaper, FontSize::Tiny);
  if (!deviceInfo.isEmpty()) {
#if RETERMINAL_MODEL == 1005
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(
        text_render::ellipsize(epaper, deviceInfo, config::PANEL_WIDTH - 112),
        16, config::PANEL_HEIGHT - 46);
#else
    epaper.drawString(
        text_render::ellipsize(
            epaper, deviceInfo, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT - config::ui(46));
#endif
  }
  if (!footer.isEmpty()) {
#if RETERMINAL_MODEL == 1005
    epaper.setTextDatum(ML_DATUM);
    epaper.drawString(
        text_render::ellipsize(epaper, footer, config::PANEL_WIDTH - 112),
        16, config::PANEL_HEIGHT - 24);
#else
    epaper.drawString(
        text_render::ellipsize(
            epaper, footer, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT - config::ui(24));
#endif
  }

  repo_qr::drawBottomRight(
      epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT,
      std::max(2, config::ui(2)), config::ui(12), PANEL_BLACK, PANEL_WHITE);
  epaper.setTextDatum(TL_DATUM);
}

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
  selectFont(epaper, FontSize::Small);
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
    selectFont(epaper, FontSize::Tiny);
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
              time_t displayDay,
              const sensors::Readings& indoor, const WeatherData& weather,
              const String& footer) {
  epaper.fillSprite(PANEL_WHITE);
  drawHeader(epaper, view, now, displayDay, indoor);
  ColorDitherer ditherer;
#if RETERMINAL_MODEL == 1005
  static_assert(
      calendar_portrait_layout::fitsPanel(
          config::PANEL_WIDTH, config::PANEL_HEIGHT),
      "E1005 Calendar layout must fit the portrait panel");
  if (view == config::CalendarView::Month) {
    drawPortraitMonth(
        epaper, ditherer, data, weekStart, displayDay, now);
  } else if (view == config::CalendarView::Week) {
    drawPortraitWeek(
        epaper, ditherer, data, weekStart, displayDay, now);
  } else {
    drawPortraitToday(epaper, ditherer, data, displayDay, indoor, weather);
  }
  drawPortraitFooter(epaper, view, footer, data);
#else
  static_cast<void>(view);
  const BodyGeometry body = bodyGeometry();
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
  drawFooter(epaper, footer, data);
#endif
}

void sleepStatus(EPaper& epaper) {
#if RETERMINAL_MODEL == 1005
  epaper.fillRect(
      0, calendar_portrait_layout::NAVIGATION_TOP, config::PANEL_WIDTH,
      calendar_portrait_layout::NAVIGATION_HEIGHT, PANEL_WHITE);
  epaper.drawFastHLine(
      0, calendar_portrait_layout::NAVIGATION_TOP, config::PANEL_WIDTH,
      PANEL_BLACK);
  epaper.setTextSize(1);
  epaper.setFreeFont(calendar_latin_font::uiFont(16));
  epaper.setTextColor(PANEL_BLACK);
  epaper.setTextDatum(MC_DATUM);
  epaper.drawString(
      "Sleeping - press OK, UP, or DOWN to wake",
      config::PANEL_WIDTH / 2,
      calendar_portrait_layout::NAVIGATION_TOP +
          calendar_portrait_layout::NAVIGATION_HEIGHT / 2);
  epaper.setTextDatum(TL_DATUM);
#else
  static_cast<void>(epaper);
#endif
}

}  // namespace calendar_render
