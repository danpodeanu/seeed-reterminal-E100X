#include <unity.h>

#include "app_logic.h"

void setUp() {}
void tearDown() {}

void test_quiet_hours_boundaries() {
  const int start = app_logic::secondsOfDay(1, 0, 0);
  const int end = app_logic::secondsOfDay(7, 0, 0);
  TEST_ASSERT_FALSE(app_logic::quietHoursActive(true, start - 1, start, end));
  TEST_ASSERT_TRUE(app_logic::quietHoursActive(true, start, start, end));
  TEST_ASSERT_TRUE(app_logic::quietHoursActive(true, end - 1, start, end));
  TEST_ASSERT_FALSE(app_logic::quietHoursActive(true, end, start, end));
}

void test_sleep_until_same_time_means_next_day() {
  const int now = app_logic::secondsOfDay(7, 0, 0);
  TEST_ASSERT_EQUAL_UINT64(
      app_logic::SECONDS_PER_DAY,
      app_logic::secondsUntilTimeOfDay(now, now));
}

void test_daily_ntp_refresh_boundaries() {
  constexpr int64_t last = 1000;
  constexpr uint32_t day = 24 * 60 * 60;
  TEST_ASSERT_FALSE(
      app_logic::refreshDue(false, true, last + day - 1, last, day));
  TEST_ASSERT_TRUE(
      app_logic::refreshDue(false, true, last + day, last, day));
  TEST_ASSERT_TRUE(app_logic::refreshDue(true, true, last, last, day));
}

void test_photo_direction_matches_buttons() {
  TEST_ASSERT_EQUAL_INT(1, app_logic::photoDirection(false));
  TEST_ASSERT_EQUAL_INT(-1, app_logic::photoDirection(true));
}

void test_photo_index_wraps_in_both_directions() {
  TEST_ASSERT_EQUAL_INT32(0, app_logic::normalizePhotoIndex(0, 5));
  TEST_ASSERT_EQUAL_INT32(0, app_logic::normalizePhotoIndex(5, 5));
  TEST_ASSERT_EQUAL_INT32(4, app_logic::normalizePhotoIndex(-1, 5));
  TEST_ASSERT_EQUAL_INT32(4, app_logic::normalizePhotoIndex(-6, 5));
  TEST_ASSERT_EQUAL_INT32(-1, app_logic::normalizePhotoIndex(0, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_quiet_hours_boundaries);
  RUN_TEST(test_sleep_until_same_time_means_next_day);
  RUN_TEST(test_daily_ntp_refresh_boundaries);
  RUN_TEST(test_photo_direction_matches_buttons);
  RUN_TEST(test_photo_index_wraps_in_both_directions);
  return UNITY_END();
}
