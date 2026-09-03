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

enum class E1005PowerMode {
  DeepSleepBatterySaver,
  AlwaysOn,
};

}  // namespace config
