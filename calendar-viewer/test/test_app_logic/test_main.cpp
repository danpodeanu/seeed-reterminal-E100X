#include <unity.h>

#include <cstdlib>
#include <string>

#include "calendar_logic.h"
#include "ical_parser.h"
#include "local_time.h"

namespace {

void setTimezone(const char* value) {
#ifdef _WIN32
  _putenv_s("TZ", value);
#else
  setenv("TZ", value, 1);
#endif
  tzset();
}

time_t utc(int year, int month, int day, int hour = 0, int minute = 0) {
  return local_time::utcEpochSeconds(year, month, day, hour, minute, 0);
}

calendar::Data parseCalendar(const char* payload,
                             time_t start = 0,
                             time_t end = 0) {
  calendar::Data data;
  calendar::Window window{
      start == 0 ? utc(2026, 8, 1) : start,
      end == 0 ? utc(2026, 9, 15) : end,
  };
  std::string failure;
  TEST_ASSERT_TRUE_MESSAGE(
      ical::parse(payload, window, 128, data, failure),
      failure.c_str());
  return data;
}

}  // namespace

void setUp() {
  setTimezone("UTC0");
}

void tearDown() {}

void test_calendar_provider_configuration_requires_selected_source() {
  TEST_ASSERT_TRUE(calendar_logic::hasConfiguredCalendarProvider(
      config::CalendarProvider::Ical, "https://example.com/calendar.ics",
      false));
  TEST_ASSERT_FALSE(calendar_logic::hasConfiguredCalendarProvider(
      config::CalendarProvider::Ical, "", true));
  TEST_ASSERT_FALSE(calendar_logic::hasConfiguredCalendarProvider(
      config::CalendarProvider::Ical, nullptr, true));
  TEST_ASSERT_TRUE(calendar_logic::hasConfiguredCalendarProvider(
      config::CalendarProvider::Google, "", true));
  TEST_ASSERT_FALSE(calendar_logic::hasConfiguredCalendarProvider(
      config::CalendarProvider::Google, "https://example.com/calendar.ics",
      false));
}

void test_google_transport_failure_classification() {
  TEST_ASSERT_TRUE(calendar_logic::isGoogleTransportFailure(0));
  TEST_ASSERT_TRUE(calendar_logic::isGoogleTransportFailure(-1));
  TEST_ASSERT_TRUE(calendar_logic::isGoogleTransportFailure(-11));
  TEST_ASSERT_FALSE(calendar_logic::isGoogleTransportFailure(200));
  TEST_ASSERT_FALSE(calendar_logic::isGoogleTransportFailure(503));
}

void test_google_oauth_failure_classification() {
  using calendar_logic::GoogleAuthFailure;

  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::Transport),
      static_cast<int>(
          calendar_logic::classifyGoogleAuthFailure(-1, "invalid_scope")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::ScopeOrAuthorization),
      static_cast<int>(
          calendar_logic::classifyGoogleAuthFailure(400, "invalid_scope")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::ScopeOrAuthorization),
      static_cast<int>(calendar_logic::classifyGoogleAuthFailure(
          400, "unauthorized_client")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::ScopeOrAuthorization),
      static_cast<int>(
          calendar_logic::classifyGoogleAuthFailure(403, "access_denied")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::Other),
      static_cast<int>(
          calendar_logic::classifyGoogleAuthFailure(400, "invalid_grant")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::Other),
      static_cast<int>(
          calendar_logic::classifyGoogleAuthFailure(401, "")));
  TEST_ASSERT_EQUAL(
      static_cast<int>(GoogleAuthFailure::Other),
      static_cast<int>(calendar_logic::classifyGoogleAuthFailure(500, "")));
}

void test_google_api_fallback_statuses() {
  TEST_ASSERT_TRUE(calendar_logic::shouldUseGoogleEventOnlyFallback(401));
  TEST_ASSERT_TRUE(calendar_logic::shouldUseGoogleEventOnlyFallback(403));
  TEST_ASSERT_FALSE(calendar_logic::shouldUseGoogleEventOnlyFallback(0));
  TEST_ASSERT_FALSE(calendar_logic::shouldUseGoogleEventOnlyFallback(-1));
  TEST_ASSERT_FALSE(calendar_logic::shouldUseGoogleEventOnlyFallback(400));
  TEST_ASSERT_FALSE(calendar_logic::shouldUseGoogleEventOnlyFallback(404));
  TEST_ASSERT_FALSE(calendar_logic::shouldUseGoogleEventOnlyFallback(500));
}

void test_primary_button_hold_classification() {
  using calendar_logic::PrimaryButtonAction;

  TEST_ASSERT_EQUAL(
      static_cast<int>(PrimaryButtonAction::Refresh),
      static_cast<int>(
          calendar_logic::classifyPrimaryButtonHold(1999, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(PrimaryButtonAction::Portal),
      static_cast<int>(
          calendar_logic::classifyPrimaryButtonHold(2000, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(PrimaryButtonAction::Portal),
      static_cast<int>(
          calendar_logic::classifyPrimaryButtonHold(4999, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(PrimaryButtonAction::Screenshot),
      static_cast<int>(
          calendar_logic::classifyPrimaryButtonHold(5000, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(PrimaryButtonAction::Portal),
      static_cast<int>(
          calendar_logic::classifyPrimaryButtonHold(5000, false)));
}

void test_display_windows_respect_week_start_and_month_grid() {
  const time_t saturday = utc(2026, 8, 29, 12);
  const calendar::Window mondayWeek = calendar_logic::displayWindow(
      config::CalendarView::Week, saturday, config::WeekStart::Monday);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 24), mondayWeek.start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 31), mondayWeek.end);

  const calendar::Window sundayWeek = calendar_logic::displayWindow(
      config::CalendarView::Week, saturday, config::WeekStart::Sunday);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 23), sundayWeek.start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 30), sundayWeek.end);

  const calendar::Window month = calendar_logic::displayWindow(
      config::CalendarView::Month, saturday, config::WeekStart::Monday);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 7, 27), month.start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 7), month.end);

  const calendar::Window dashboard =
      calendar_logic::dashboardWindow(saturday, config::WeekStart::Monday);
  TEST_ASSERT_EQUAL_INT64(month.start, dashboard.start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 11), dashboard.end);
}

void test_ical_parser_reads_timed_all_day_folded_text_and_colors() {
  const char* payload =
      "BEGIN:VCALENDAR\r\n"
      "VERSION:2.0\r\n"
      "X-WR-CALNAME:Family\r\n"
      "X-APPLE-CALENDAR-COLOR:#33B679CC\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:timed-1\r\n"
      "DTSTART:20260829T090000Z\r\n"
      "DTEND:20260829T100000Z\r\n"
      "SUMMARY:Project planning with a very\r\n"
      " long folded title\r\n"
      "LOCATION:Room 2\\, west wing\r\n"
      "COLOR:#000000\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\n"
      "UID:day-1\r\n"
      "DTSTART;VALUE=DATE:20260830\r\n"
      "DTEND;VALUE=DATE:20260831\r\n"
      "SUMMARY:Bank holiday\r\n"
      "END:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  const calendar::Data data = parseCalendar(payload);
  TEST_ASSERT_EQUAL_UINT32(0x33B679, data.sources[0].colorRgb);
  TEST_ASSERT_EQUAL_STRING("Family", data.sourceLabel.c_str());
  TEST_ASSERT_EQUAL_UINT(2, data.events.size());
  TEST_ASSERT_EQUAL_STRING(
      "Project planning with a verylong folded title",
      data.events[0].title.c_str());
  TEST_ASSERT_EQUAL_STRING("Room 2, west wing",
                           data.events[0].location.c_str());
  TEST_ASSERT_EQUAL_UINT32(0x000000, data.events[0].colorRgb);
  TEST_ASSERT_FALSE(data.events[0].allDay);
  TEST_ASSERT_TRUE(data.events[1].allDay);
  TEST_ASSERT_EQUAL_INT64(86400, data.events[1].end - data.events[1].start);
}

void test_ical_parser_expands_recurrence_and_applies_exdates() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:standup\n"
      "DTSTART:20260824T090000Z\n"
      "DTEND:20260824T093000Z\n"
      "RRULE:FREQ=WEEKLY;COUNT=5;BYDAY=MO,WE\n"
      "EXDATE:20260826T090000Z\n"
      "SUMMARY:Stand-up\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 24), utc(2026, 9, 10));
  TEST_ASSERT_EQUAL_UINT(4, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 24, 9), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 31, 9), data.events[1].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 2, 9), data.events[2].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 7, 9), data.events[3].start);
}

void test_ical_parser_applies_overrides_and_cancellations() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:series\n"
      "DTSTART:20260824T090000Z\n"
      "DTEND:20260824T100000Z\n"
      "RRULE:FREQ=DAILY;COUNT=3\n"
      "SUMMARY:Original\n"
      "END:VEVENT\n"
      "BEGIN:VEVENT\n"
      "UID:series\n"
      "RECURRENCE-ID:20260825T090000Z\n"
      "DTSTART:20260825T140000Z\n"
      "DTEND:20260825T150000Z\n"
      "SUMMARY:Moved\n"
      "END:VEVENT\n"
      "BEGIN:VEVENT\n"
      "UID:series\n"
      "RECURRENCE-ID:20260826T090000Z\n"
      "STATUS:CANCELLED\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 24), utc(2026, 8, 27));
  TEST_ASSERT_EQUAL_UINT(2, data.events.size());
  TEST_ASSERT_EQUAL_STRING("Original", data.events[0].title.c_str());
  TEST_ASSERT_EQUAL_STRING("Moved", data.events[1].title.c_str());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 25, 14), data.events[1].start);
}

void test_utc_recurrence_does_not_shift_with_device_dst() {
  setTimezone("GMT0BST,M3.5.0/1,M10.5.0/2");
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:utc-series\n"
      "DTSTART:20260328T120000Z\n"
      "DTEND:20260328T130000Z\n"
      "RRULE:FREQ=DAILY;COUNT=3\n"
      "SUMMARY:UTC event\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 3, 28), utc(2026, 4, 1));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 28, 12), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 29, 12), data.events[1].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 30, 12), data.events[2].start);
}

void test_all_day_recurrence_ends_at_local_midnight_across_dst() {
  setTimezone("GMT0BST,M3.5.0/1,M10.5.0/2");
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:all-day-series\n"
      "DTSTART;VALUE=DATE:20260328\n"
      "DTEND;VALUE=DATE:20260329\n"
      "RRULE:FREQ=DAILY;COUNT=3\n"
      "SUMMARY:All day\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 3, 27), utc(2026, 4, 2));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  for (const auto& event : data.events) {
    struct tm endLocal = {};
    TEST_ASSERT_NOT_NULL(localtime_r(&event.end, &endLocal));
    TEST_ASSERT_EQUAL_INT(0, endLocal.tm_hour);
    TEST_ASSERT_EQUAL_INT(0, endLocal.tm_min);
  }
}

void test_unbounded_old_recurrence_fast_forwards_to_display_window() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:long-running\n"
      "DTSTART:20000101T080000Z\n"
      "DTEND:20000101T083000Z\n"
      "RRULE:FREQ=DAILY\n"
      "SUMMARY:Daily habit\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 29), utc(2026, 9, 1));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 29, 8), data.events[0].start);
}

void test_ical_parser_accepts_an_empty_calendar() {
  const calendar::Data data = parseCalendar(
      "BEGIN:VCALENDAR\nVERSION:2.0\nX-WR-CALNAME:Empty\nEND:VCALENDAR\n");
  TEST_ASSERT_EQUAL_UINT(0, data.events.size());
  TEST_ASSERT_EQUAL_STRING("Empty", data.sourceLabel.c_str());
}

void test_ical_parser_rejects_non_calendar_payloads() {
  calendar::Data data;
  std::string failure;
  TEST_ASSERT_FALSE(ical::parse(
      "<html>sign in required</html>",
      {utc(2026, 8, 1), utc(2026, 9, 1)}, 10, data, failure));
  TEST_ASSERT_FALSE(failure.empty());
}

void test_ical_parser_rejects_malformed_datetimes() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:bad-date\n"
      "DTSTART:20260829T090000garbageZ\n"
      "SUMMARY:Must not appear\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  calendar::Data data;
  std::string failure;
  TEST_ASSERT_FALSE(ical::parse(
      payload, {utc(2026, 8, 1), utc(2026, 9, 1)}, 128, data, failure));
  TEST_ASSERT_FALSE(failure.empty());
}

void test_ical_parser_handles_explicit_date_time_and_duration() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:short\n"
      "DTSTART;VALUE=DATE-TIME:20260829T090000Z\n"
      "DURATION:PT15M\n"
      "SUMMARY:Short meeting\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data = parseCalendar(payload);
  TEST_ASSERT_EQUAL_UINT(1, data.events.size());
  TEST_ASSERT_FALSE(data.events[0].allDay);
  TEST_ASSERT_EQUAL_INT64(15 * 60,
                          data.events[0].end - data.events[0].start);
}

void test_ical_parser_resolves_known_tzid_and_rejects_unknown_tzid() {
  const char* supported =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:zoned\n"
      "DTSTART;TZID=America/New_York:20260829T090000\n"
      "DURATION:PT30M\n"
      "SUMMARY:Breakfast\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data = parseCalendar(supported);
  TEST_ASSERT_EQUAL_UINT(1, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 29, 13), data.events[0].start);

  const char* unsupported =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:unknown-zone\n"
      "DTSTART;TZID=Antarctica/Troll:20260829T090000\n"
      "SUMMARY:Unknown\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  calendar::Data rejected;
  std::string failure;
  TEST_ASSERT_FALSE(ical::parse(
      unsupported, {utc(2026, 8, 1), utc(2026, 9, 1)}, 128, rejected,
      failure));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, failure.find("TZID"));
}

void test_tzid_recurrence_keeps_wall_time_across_dst() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:zoned-series\n"
      "DTSTART;TZID=America/New_York:20260307T090000\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=WEEKLY;COUNT=3\n"
      "SUMMARY:Weekly breakfast\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 3, 1), utc(2026, 3, 31));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 7, 14), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 14, 13), data.events[1].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 21, 13), data.events[2].start);
}

void test_calendar_day_duration_keeps_wall_time_across_dst() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:day-duration\n"
      "DTSTART;TZID=America/New_York:20260307T090000\n"
      "DURATION:P1D\n"
      "SUMMARY:One local day\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 3, 1), utc(2026, 3, 15));
  TEST_ASSERT_EQUAL_UINT(1, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 7, 14), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 3, 8, 13), data.events[0].end);
  TEST_ASSERT_EQUAL_INT64(23 * 3600,
                          data.events[0].end - data.events[0].start);
}

void test_cairo_tzid_applies_summer_time() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:cairo\n"
      "DTSTART;TZID=Africa/Cairo:20260715T090000\n"
      "DURATION:PT30M\n"
      "SUMMARY:Cairo meeting\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 7, 1), utc(2026, 8, 1));
  TEST_ASSERT_EQUAL_UINT(1, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 7, 15, 6), data.events[0].start);
}

void test_valarm_properties_do_not_override_event_properties() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:alarm-parent\n"
      "DTSTART:20260829T090000Z\n"
      "DTEND:20260829T100000Z\n"
      "SUMMARY:Parent event\n"
      "BEGIN:VALARM\n"
      "ACTION:DISPLAY\n"
      "TRIGGER:-PT10M\n"
      "DURATION:PT5M\n"
      "REPEAT:2\n"
      "SUMMARY:Alarm text\n"
      "END:VALARM\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data = parseCalendar(payload);
  TEST_ASSERT_EQUAL_UINT(1, data.events.size());
  TEST_ASSERT_EQUAL_STRING("Parent event", data.events[0].title.c_str());
  TEST_ASSERT_EQUAL_INT64(3600,
                          data.events[0].end - data.events[0].start);

  const char* unbalanced =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:bad-alarm\n"
      "DTSTART:20260829T090000Z\n"
      "BEGIN:VALARM\n"
      "ACTION:DISPLAY\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  calendar::Data rejected;
  std::string failure;
  TEST_ASSERT_FALSE(ical::parse(
      unbalanced, {utc(2026, 8, 1), utc(2026, 9, 1)}, 128, rejected,
      failure));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, failure.find("structurally"));
}

void test_long_finite_recurrence_reaches_current_window() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:long-finite\n"
      "DTSTART:20100104T090000Z\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=WEEKLY;COUNT=1000\n"
      "SUMMARY:Long-running weekly\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 1, 1), utc(2026, 2, 1));
  TEST_ASSERT_GREATER_THAN_UINT(0, data.events.size());
  TEST_ASSERT_FALSE(data.truncated);
  TEST_ASSERT_GREATER_OR_EQUAL_INT64(utc(2026, 1, 1),
                                    data.events.front().start);
}

void test_far_future_until_stops_at_display_window() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:far-until\n"
      "DTSTART:20260801T090000Z\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=DAILY;UNTIL=99991231T235959Z\n"
      "SUMMARY:Daily with distant end\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 1), utc(2026, 8, 4));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_FALSE(data.truncated);
}

void test_ical_parser_rejects_truncated_documents() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:partial\n"
      "DTSTART:20260829T090000Z\n"
      "DTEND:20260829T100000Z\n"
      "SUMMARY:Partial\n"
      "END:VEVENT\n";
  calendar::Data data;
  std::string failure;
  TEST_ASSERT_FALSE(ical::parse(
      payload, {utc(2026, 8, 1), utc(2026, 9, 1)}, 128, data, failure));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, failure.find("truncated"));
}

void test_monthly_recurrence_supports_ordinal_weekdays() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:first-monday\n"
      "DTSTART:20260803T090000Z\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=MONTHLY;COUNT=3;BYDAY=1MO\n"
      "SUMMARY:First Monday\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 1), utc(2026, 11, 1));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 3, 9), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 7, 9), data.events[1].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 5, 9), data.events[2].start);
}

void test_monthly_recurrence_supports_negative_month_days() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:last-day\n"
      "DTSTART:20260831T090000Z\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=MONTHLY;COUNT=3;BYMONTHDAY=-1\n"
      "SUMMARY:Month close\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 1), utc(2026, 11, 1));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 31, 9), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 30, 9), data.events[1].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 10, 31, 9), data.events[2].start);
}

void test_weekly_recurrence_uses_wkst_for_intervals() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:fortnightly\n"
      "DTSTART:20260826T090000Z\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=MO,WE;WKST=MO\n"
      "SUMMARY:Fortnightly\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 20), utc(2026, 9, 30));
  TEST_ASSERT_EQUAL_UINT(4, data.events.size());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 26, 9), data.events[0].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 7, 9), data.events[1].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 9, 9), data.events[2].start);
  TEST_ASSERT_EQUAL_INT64(utc(2026, 9, 21, 9), data.events[3].start);
}

void test_date_until_ends_on_local_day_across_dst() {
  setTimezone("GMT0BST,M3.5.0/1,M10.5.0/2");
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:until-date\n"
      "DTSTART;VALUE=DATE:20260327\n"
      "DURATION:P1D\n"
      "RRULE:FREQ=DAILY;UNTIL=20260329\n"
      "SUMMARY:DST run\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 3, 26), utc(2026, 4, 1));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  struct tm last = {};
  TEST_ASSERT_NOT_NULL(localtime_r(&data.events.back().start, &last));
  TEST_ASSERT_EQUAL_INT(29, last.tm_mday);
}

void test_override_moved_backward_into_window_is_included() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:moved-series\n"
      "DTSTART:20260830T090000Z\n"
      "DURATION:PT1H\n"
      "RRULE:FREQ=DAILY;COUNT=3\n"
      "SUMMARY:Original\n"
      "END:VEVENT\n"
      "BEGIN:VEVENT\n"
      "UID:moved-series\n"
      "RECURRENCE-ID:20260901T090000Z\n"
      "DTSTART:20260829T120000Z\n"
      "DURATION:PT30M\n"
      "SUMMARY:Moved earlier\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  const calendar::Data data =
      parseCalendar(payload, utc(2026, 8, 29), utc(2026, 8, 30));
  TEST_ASSERT_EQUAL_UINT(1, data.events.size());
  TEST_ASSERT_EQUAL_STRING("Moved earlier", data.events[0].title.c_str());
  TEST_ASSERT_EQUAL_INT64(utc(2026, 8, 29, 12), data.events[0].start);
}

void test_unsupported_recurrence_selectors_are_rejected() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:unsupported-rule\n"
      "DTSTART:20260803T090000Z\n"
      "DURATION:PT30M\n"
      "RRULE:FREQ=MONTHLY;BYDAY=MO;BYSETPOS=1\n"
      "SUMMARY:Unsupported\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  calendar::Data data;
  std::string failure;
  TEST_ASSERT_FALSE(ical::parse(
      payload, {utc(2026, 8, 1), utc(2026, 10, 1)}, 128, data, failure));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, failure.find("BYSETPOS"));
}

void test_ical_parser_caps_event_count_and_marks_truncation() {
  const char* payload =
      "BEGIN:VCALENDAR\n"
      "BEGIN:VEVENT\n"
      "UID:daily\n"
      "DTSTART:20260801T100000Z\n"
      "DTEND:20260801T110000Z\n"
      "RRULE:FREQ=DAILY;COUNT=20\n"
      "SUMMARY:Daily\n"
      "END:VEVENT\n"
      "END:VCALENDAR\n";
  calendar::Data data;
  std::string failure;
  TEST_ASSERT_TRUE(ical::parse(
      payload, {utc(2026, 8, 1), utc(2026, 9, 1)}, 3, data, failure));
  TEST_ASSERT_EQUAL_UINT(3, data.events.size());
  TEST_ASSERT_TRUE(data.truncated);
}

void test_data_fingerprint_changes_with_visible_event_content_only() {
  calendar::Data data;
  calendar::Event event;
  event.uid = "one";
  event.title = "Visible";
  event.start = utc(2026, 8, 29, 9);
  event.end = utc(2026, 8, 29, 10);
  data.events.push_back(event);
  const calendar::Window window{utc(2026, 8, 29), utc(2026, 8, 30)};
  const uint64_t original = calendar_logic::dataFingerprint(data, window);
  data.events[0].title = "Changed";
  TEST_ASSERT_NOT_EQUAL(
      original, calendar_logic::dataFingerprint(data, window));
  data.events[0].start = utc(2026, 8, 30, 9);
  data.events[0].end = utc(2026, 8, 30, 10);
  data.events[0].title = "Outside";
  const uint64_t outside = calendar_logic::dataFingerprint(data, window);
  data.events[0].title = "Outside changed";
  TEST_ASSERT_EQUAL_UINT64(
      outside, calendar_logic::dataFingerprint(data, window));

  calendar::Source source;
  data.sources.push_back(source);
  const uint64_t fallbackColor =
      calendar_logic::dataFingerprint(data, window);
  data.sources[0].googleColorAvailable = true;
  TEST_ASSERT_NOT_EQUAL(
      fallbackColor, calendar_logic::dataFingerprint(data, window));
}

void test_frame_component_changes_identify_each_changed_input() {
  using calendar_logic::FrameComponentChange;
  calendar_logic::FrameComponents previous;
  previous.renderer = 1;
  previous.calendar = 2;
  previous.presentation = 3;
  previous.date = 4;
  previous.indoorClimate = 5;
  previous.power = 6;
  previous.weather = 7;
  previous.footer = 8;

  calendar_logic::FrameComponents current = previous;
  TEST_ASSERT_EQUAL_HEX16(
      0, calendar_logic::changedFrameComponents(previous, current));

  current.renderer++;
  current.calendar++;
  current.date++;
  current.power++;
  const uint16_t expected =
      calendar_logic::frameComponentBit(FrameComponentChange::Renderer) |
      calendar_logic::frameComponentBit(FrameComponentChange::Calendar) |
      calendar_logic::frameComponentBit(FrameComponentChange::Date) |
      calendar_logic::frameComponentBit(FrameComponentChange::Power);
  const uint16_t changes =
      calendar_logic::changedFrameComponents(previous, current);
  TEST_ASSERT_EQUAL_HEX16(expected, changes);
  TEST_ASSERT_TRUE(calendar_logic::frameComponentChanged(
      changes, FrameComponentChange::Calendar));
  TEST_ASSERT_FALSE(calendar_logic::frameComponentChanged(
      changes, FrameComponentChange::Weather));

  current.presentation++;
  current.indoorClimate++;
  current.weather++;
  current.footer++;
  TEST_ASSERT_EQUAL_HEX16(
      calendar_logic::kAllFrameComponentChanges,
      calendar_logic::changedFrameComponents(previous, current));
}

void test_frame_component_changes_reject_incompatible_history() {
  calendar_logic::FrameComponents previous;
  calendar_logic::FrameComponents current;
  previous.version = 0;
  TEST_ASSERT_FALSE(calendar_logic::frameComponentsCompatible(previous));
  TEST_ASSERT_TRUE(calendar_logic::frameComponentsCompatible(current));
  TEST_ASSERT_EQUAL_HEX16(
      calendar_logic::kAllFrameComponentChanges,
      calendar_logic::changedFrameComponents(previous, current));
}

void test_color_parsing_accepts_hex_and_rejects_invalid_values() {
  TEST_ASSERT_EQUAL_HEX32(0x33B679, calendar_logic::parseRgb("#33B679"));
  TEST_ASSERT_EQUAL_HEX32(0xABCDEF, calendar_logic::parseRgb("abcdef"));
  TEST_ASSERT_EQUAL_HEX32(0x123456,
                          calendar_logic::parseRgb("#bad", 0x123456));
}

void test_single_google_calendar_color_controls_grid_background() {
  calendar::Data data;
  uint32_t color = 0;
  calendar::Source source;
  source.colorRgb = 0xCCA6AC;
  data.sources.push_back(source);
  TEST_ASSERT_FALSE(calendar_logic::singleGoogleCalendarColor(data, color));

  data.sources[0].googleColorAvailable = true;
  TEST_ASSERT_TRUE(calendar_logic::singleGoogleCalendarColor(data, color));
  TEST_ASSERT_EQUAL_HEX32(0xCCA6AC, color);

  data.sources.push_back(source);
  TEST_ASSERT_FALSE(calendar_logic::singleGoogleCalendarColor(data, color));
}

void test_agenda_compacts_upcoming_rows_for_more_count() {
  const calendar_logic::AgendaLayout e1004 =
      calendar_logic::agendaLayout(8, 290, 70, 64, 25);
  TEST_ASSERT_EQUAL_INT(4, e1004.visibleRows);
  TEST_ASSERT_EQUAL_INT(66, e1004.rowHeight);
  TEST_ASSERT_TRUE(e1004.showMore);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(
      25, 290 - e1004.visibleRows * e1004.rowHeight);

  const calendar_logic::AgendaLayout e1003 =
      calendar_logic::agendaLayout(8, 296, 88, 79, 27);
  TEST_ASSERT_EQUAL_INT(3, e1003.visibleRows);
  TEST_ASSERT_EQUAL_INT(88, e1003.rowHeight);
  TEST_ASSERT_TRUE(e1003.showMore);

  const calendar_logic::AgendaLayout today =
      calendar_logic::agendaLayout(8, 290, 70, 70, 25);
  TEST_ASSERT_EQUAL_INT(3, today.visibleRows);
  TEST_ASSERT_EQUAL_INT(70, today.rowHeight);
  TEST_ASSERT_TRUE(today.showMore);

  const calendar_logic::AgendaLayout compact =
      calendar_logic::agendaLayout(8, 46, 48, 44, 24);
  TEST_ASSERT_EQUAL_INT(1, compact.visibleRows);
  TEST_ASSERT_EQUAL_INT(46, compact.rowHeight);
  TEST_ASSERT_FALSE(compact.showMore);

  const calendar_logic::AgendaLayout noOverflow =
      calendar_logic::agendaLayout(4, 290, 70, 64, 25);
  TEST_ASSERT_EQUAL_INT(4, noOverflow.visibleRows);
  TEST_ASSERT_EQUAL_INT(70, noOverflow.rowHeight);
  TEST_ASSERT_FALSE(noOverflow.showMore);
}

void test_clock_time_format_supports_twelve_and_twenty_four_hour_clocks() {
  const time_t midnight = utc(2026, 8, 30, 0, 5);
  const time_t morning = utc(2026, 8, 30, 10, 15);
  const time_t eleven = utc(2026, 8, 30, 11);
  const time_t noon = utc(2026, 8, 30, 12, 30);
  const time_t one = utc(2026, 8, 30, 13);
  const time_t afternoon = utc(2026, 8, 30, 13, 45);

  TEST_ASSERT_EQUAL_STRING(
      "12:05am",
      calendar_logic::formatClockTime(midnight,
                                      config::TimeFormat::TwelveHour)
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "12:30pm",
      calendar_logic::formatClockTime(noon, config::TimeFormat::TwelveHour)
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "13:45",
      calendar_logic::formatClockTime(
          afternoon, config::TimeFormat::TwentyFourHour)
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "10:15am - 12:30pm",
      calendar_logic::formatClockRange(morning, noon,
                                       config::TimeFormat::TwelveHour)
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "12:30pm - 1:45pm",
      calendar_logic::formatClockRange(noon, afternoon,
                                       config::TimeFormat::TwelveHour)
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "11:00am - 1pm",
      calendar_logic::formatClockRange(eleven, one,
                                       config::TimeFormat::TwelveHour)
          .c_str());
  TEST_ASSERT_EQUAL_STRING(
      "12:30 - 13:45",
      calendar_logic::formatClockRange(noon, afternoon,
                                       config::TimeFormat::TwentyFourHour)
          .c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_calendar_provider_configuration_requires_selected_source);
  RUN_TEST(test_google_transport_failure_classification);
  RUN_TEST(test_google_oauth_failure_classification);
  RUN_TEST(test_google_api_fallback_statuses);
  RUN_TEST(test_primary_button_hold_classification);
  RUN_TEST(test_display_windows_respect_week_start_and_month_grid);
  RUN_TEST(test_ical_parser_reads_timed_all_day_folded_text_and_colors);
  RUN_TEST(test_ical_parser_expands_recurrence_and_applies_exdates);
  RUN_TEST(test_ical_parser_applies_overrides_and_cancellations);
  RUN_TEST(test_utc_recurrence_does_not_shift_with_device_dst);
  RUN_TEST(test_all_day_recurrence_ends_at_local_midnight_across_dst);
  RUN_TEST(test_unbounded_old_recurrence_fast_forwards_to_display_window);
  RUN_TEST(test_ical_parser_accepts_an_empty_calendar);
  RUN_TEST(test_ical_parser_rejects_non_calendar_payloads);
  RUN_TEST(test_ical_parser_rejects_malformed_datetimes);
  RUN_TEST(test_ical_parser_handles_explicit_date_time_and_duration);
  RUN_TEST(test_ical_parser_resolves_known_tzid_and_rejects_unknown_tzid);
  RUN_TEST(test_tzid_recurrence_keeps_wall_time_across_dst);
  RUN_TEST(test_calendar_day_duration_keeps_wall_time_across_dst);
  RUN_TEST(test_cairo_tzid_applies_summer_time);
  RUN_TEST(test_valarm_properties_do_not_override_event_properties);
  RUN_TEST(test_long_finite_recurrence_reaches_current_window);
  RUN_TEST(test_far_future_until_stops_at_display_window);
  RUN_TEST(test_ical_parser_rejects_truncated_documents);
  RUN_TEST(test_monthly_recurrence_supports_ordinal_weekdays);
  RUN_TEST(test_monthly_recurrence_supports_negative_month_days);
  RUN_TEST(test_weekly_recurrence_uses_wkst_for_intervals);
  RUN_TEST(test_date_until_ends_on_local_day_across_dst);
  RUN_TEST(test_override_moved_backward_into_window_is_included);
  RUN_TEST(test_unsupported_recurrence_selectors_are_rejected);
  RUN_TEST(test_ical_parser_caps_event_count_and_marks_truncation);
  RUN_TEST(test_data_fingerprint_changes_with_visible_event_content_only);
  RUN_TEST(test_frame_component_changes_identify_each_changed_input);
  RUN_TEST(test_frame_component_changes_reject_incompatible_history);
  RUN_TEST(test_color_parsing_accepts_hex_and_rejects_invalid_values);
  RUN_TEST(test_single_google_calendar_color_controls_grid_background);
  RUN_TEST(test_agenda_compacts_upcoming_rows_for_more_count);
  RUN_TEST(
      test_clock_time_format_supports_twelve_and_twenty_four_hour_clocks);
  return UNITY_END();
}
