#include <unity.h>

#include "app_logic.h"

void setUp() {}
void tearDown() {}

void test_startup_beep_only_for_cold_boot_and_button_wake() {
  TEST_ASSERT_TRUE(app_logic::startupBeepRequired(true, false));
  TEST_ASSERT_TRUE(app_logic::startupBeepRequired(false, true));
  TEST_ASSERT_FALSE(app_logic::startupBeepRequired(false, false));
}

void test_quiet_hours_boundaries() {
  const int start = app_logic::secondsOfDay(1, 0, 0);
  const int end = app_logic::secondsOfDay(7, 0, 0);
  TEST_ASSERT_FALSE(app_logic::quietHoursActive(true, start - 1, start, end));
  TEST_ASSERT_TRUE(app_logic::quietHoursActive(true, start, start, end));
  TEST_ASSERT_TRUE(app_logic::quietHoursActive(true, end - 1, start, end));
  TEST_ASSERT_FALSE(app_logic::quietHoursActive(true, end, start, end));
  TEST_ASSERT_FALSE(app_logic::quietHoursActive(false, start, start, end));
}

void test_quiet_hours_can_wrap_midnight() {
  const int start = app_logic::secondsOfDay(22, 0, 0);
  const int end = app_logic::secondsOfDay(6, 0, 0);
  TEST_ASSERT_TRUE(app_logic::quietHoursActive(
      true, app_logic::secondsOfDay(23, 0, 0), start, end));
  TEST_ASSERT_TRUE(app_logic::quietHoursActive(
      true, app_logic::secondsOfDay(5, 59, 59), start, end));
  TEST_ASSERT_FALSE(app_logic::quietHoursActive(
      true, app_logic::secondsOfDay(12, 0, 0), start, end));
}

void test_refresh_due_handles_boundaries_and_clock_rollback() {
  constexpr int64_t last = 1000;
  constexpr uint32_t interval = 100;
  TEST_ASSERT_TRUE(app_logic::refreshDue(true, true, last, last, interval));
  TEST_ASSERT_TRUE(app_logic::refreshDue(false, false, last, last, interval));
  TEST_ASSERT_FALSE(
      app_logic::refreshDue(false, true, last + interval - 1, last, interval));
  TEST_ASSERT_TRUE(
      app_logic::refreshDue(false, true, last + interval, last, interval));
  TEST_ASSERT_TRUE(
      app_logic::refreshDue(false, true, last - 1, last, interval));
}

void test_cache_only_threshold_controls_network() {
  TEST_ASSERT_FALSE(app_logic::cacheOnly(false, 100, 10));
  TEST_ASSERT_FALSE(app_logic::cacheOnly(true, 9, 10));
  TEST_ASSERT_TRUE(app_logic::cacheOnly(true, 10, 10));
  TEST_ASSERT_FALSE(app_logic::networkPlanned(true, false));
  TEST_ASSERT_TRUE(app_logic::networkPlanned(true, true));
  TEST_ASSERT_TRUE(app_logic::networkPlanned(false, false));
}

void test_archive_maintenance_requires_timer_and_sd() {
  TEST_ASSERT_TRUE(app_logic::archiveMaintenanceDue(true, true, true));
  TEST_ASSERT_FALSE(app_logic::archiveMaintenanceDue(false, true, true));
  TEST_ASSERT_FALSE(app_logic::archiveMaintenanceDue(true, false, true));
  TEST_ASSERT_FALSE(app_logic::archiveMaintenanceDue(true, true, false));
}

void test_deadline_comparison_survives_millis_wrap() {
  TEST_ASSERT_FALSE(app_logic::deadlineReached(100, 200));
  TEST_ASSERT_TRUE(app_logic::deadlineReached(200, 200));
  TEST_ASSERT_FALSE(app_logic::deadlineReached(100, 0));
  TEST_ASSERT_TRUE(app_logic::deadlineReached(0x10U, 0xFFFFFFF0U));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_startup_beep_only_for_cold_boot_and_button_wake);
  RUN_TEST(test_quiet_hours_boundaries);
  RUN_TEST(test_quiet_hours_can_wrap_midnight);
  RUN_TEST(test_refresh_due_handles_boundaries_and_clock_rollback);
  RUN_TEST(test_cache_only_threshold_controls_network);
  RUN_TEST(test_archive_maintenance_requires_timer_and_sd);
  RUN_TEST(test_deadline_comparison_survives_millis_wrap);
  return UNITY_END();
}
