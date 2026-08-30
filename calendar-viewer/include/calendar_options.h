#pragma once

namespace config {

enum class CalendarProvider {
  Ical,
  Google,
};

enum class CalendarView {
  Today,
  Week,
  Month,
};

enum class WeekStart {
  Monday,
  Sunday,
};

enum class TimeFormat {
  TwelveHour,
  TwentyFourHour,
};

}  // namespace config
