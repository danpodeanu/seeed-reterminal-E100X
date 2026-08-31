#pragma once

#include <Arduino.h>

#include "calendar_data.h"
#include "calendar_options.h"
#include "sensors.h"
#include "weather_data.h"

class EPaper;

namespace calendar_render {

void connectionStatus(EPaper& epaper, const String& title,
                      const String& detail, const String& deviceInfo,
                      const String& footer);

void status(EPaper& epaper, const String& title, const String& detail,
            const String& footer = "");

void calendar(EPaper& epaper, const ::calendar::Data& data,
              const ::calendar::Window& window, config::CalendarView view,
              config::WeekStart weekStart, time_t now,
              time_t displayDay,
              const sensors::Readings& indoor, const WeatherData& weather,
              const String& footer);

void sleepStatus(EPaper& epaper);

}  // namespace calendar_render
