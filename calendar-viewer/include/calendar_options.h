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

}  // namespace config
