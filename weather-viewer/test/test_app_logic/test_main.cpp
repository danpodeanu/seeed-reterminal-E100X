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
}

void test_next_wake_detects_quiet_boundary() {
  const int start = app_logic::secondsOfDay(1, 0, 0);
  const int end = app_logic::secondsOfDay(7, 0, 0);
  const int now = app_logic::secondsOfDay(0, 45, 0);
  TEST_ASSERT_FALSE(app_logic::nextWakeFallsInQuietHours(
      true, now, start, end, 14 * 60));
  TEST_ASSERT_TRUE(app_logic::nextWakeFallsInQuietHours(
      true, now, start, end, 15 * 60));
  TEST_ASSERT_FALSE(app_logic::nextWakeFallsInQuietHours(
      false, now, start, end, 15 * 60));
}

void test_daily_refresh_due_logic() {
  constexpr int64_t last = 1000;
  constexpr uint32_t day = 24 * 60 * 60;
  TEST_ASSERT_FALSE(
      app_logic::refreshDue(false, true, last + day - 1, last, day));
  TEST_ASSERT_TRUE(
      app_logic::refreshDue(false, true, last + day, last, day));
  TEST_ASSERT_TRUE(app_logic::refreshDue(true, true, last, last, day));
  TEST_ASSERT_TRUE(app_logic::refreshDue(false, true, last - 1, last, day));
}

void test_cached_weather_must_be_no_older_than_one_sleep_period() {
  constexpr int64_t forecast = 1000;
  constexpr uint64_t sleep = 30 * 60;
  TEST_ASSERT_TRUE(
      app_logic::cachedDataFresh(true, forecast, forecast, sleep));
  TEST_ASSERT_TRUE(
      app_logic::cachedDataFresh(true, forecast + sleep, forecast, sleep));
  TEST_ASSERT_FALSE(
      app_logic::cachedDataFresh(true, forecast + sleep + 1, forecast, sleep));
  TEST_ASSERT_FALSE(
      app_logic::cachedDataFresh(false, forecast, forecast, sleep));
  TEST_ASSERT_FALSE(
      app_logic::cachedDataFresh(true, forecast - 1, forecast, sleep));
  TEST_ASSERT_FALSE(app_logic::cachedDataFresh(true, forecast, 0, sleep));
}

void test_weather_age_rounds_to_nearest_five_minutes() {
  constexpr int64_t timestamp = 100000;
  TEST_ASSERT_EQUAL_INT64(
      0, app_logic::roundedAgeMinutes(timestamp + 149, timestamp));
  TEST_ASSERT_EQUAL_INT64(
      5, app_logic::roundedAgeMinutes(timestamp + 150, timestamp));
  TEST_ASSERT_EQUAL_INT64(
      15, app_logic::roundedAgeMinutes(timestamp + 17 * 60 + 29, timestamp));
  TEST_ASSERT_EQUAL_INT64(
      20, app_logic::roundedAgeMinutes(timestamp + 17 * 60 + 30, timestamp));
  TEST_ASSERT_EQUAL_INT64(
      150, app_logic::roundedAgeMinutes(timestamp + 2 * 3600 + 28 * 60,
                                        timestamp));
  TEST_ASSERT_EQUAL_INT64(
      2400, app_logic::roundedAgeMinutes(timestamp + 40 * 3600, timestamp));
}

void test_weather_age_rejects_invalid_or_future_timestamps() {
  TEST_ASSERT_EQUAL_INT64(-1, app_logic::roundedAgeMinutes(1000, 0));
  TEST_ASSERT_EQUAL_INT64(-1, app_logic::roundedAgeMinutes(999, 1000));
}

void test_quiet_suppression_preserves_override_wakes() {
  TEST_ASSERT_TRUE(app_logic::suppressForQuietHours(
      false, false, false, true, true));
  TEST_ASSERT_FALSE(app_logic::suppressForQuietHours(
      true, false, false, true, true));
  TEST_ASSERT_FALSE(app_logic::suppressForQuietHours(
      false, true, false, true, true));
  TEST_ASSERT_FALSE(app_logic::suppressForQuietHours(
      false, false, true, true, true));
  TEST_ASSERT_FALSE(app_logic::suppressForQuietHours(
      false, false, false, false, true));
}

void test_failure_window_uses_cache_only_when_clock_and_cache_are_valid() {
  // Live succeeded: never fall back to cache, regardless of cache state.
  TEST_ASSERT_FALSE(
      app_logic::useCachedForecastOnFailure(true, true, true, true));
  // Live failed, everything valid, cache is within the failure window:
  // render cached forecast (no error screen).
  TEST_ASSERT_TRUE(
      app_logic::useCachedForecastOnFailure(false, true, true, true));
  // Live failed but clock is invalid: cannot trust the recorded timestamp,
  // so don't rely on the cache.
  TEST_ASSERT_FALSE(
      app_logic::useCachedForecastOnFailure(false, false, true, true));
  // Live failed but no cache exists: fall through to the error screen.
  TEST_ASSERT_FALSE(
      app_logic::useCachedForecastOnFailure(false, true, false, true));
  // Live failed and cache is outside the failure window: show error screen.
  TEST_ASSERT_FALSE(
      app_logic::useCachedForecastOnFailure(false, true, true, false));
}

void test_failure_cache_boundary_at_one_hour() {
  // Documents the 1 h contract from config::FAILURE_CACHE_MAX_AGE_SECONDS:
  // a cache saved exactly one hour ago is still acceptable; a second later
  // it is not. This is checked through cachedDataFresh, which is the
  // predicate that gates the failure-window path.
  constexpr uint64_t oneHour = 60ULL * 60ULL;
  constexpr int64_t saved = 1'000'000;
  TEST_ASSERT_TRUE(
      app_logic::cachedDataFresh(true, saved + oneHour, saved, oneHour));
  TEST_ASSERT_FALSE(
      app_logic::cachedDataFresh(true, saved + oneHour + 1, saved, oneHour));
  TEST_ASSERT_FALSE(app_logic::cachedDataFresh(false, saved, saved, oneHour));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_startup_beep_only_for_cold_boot_and_button_wake);
  RUN_TEST(test_quiet_hours_boundaries);
  RUN_TEST(test_next_wake_detects_quiet_boundary);
  RUN_TEST(test_daily_refresh_due_logic);
  RUN_TEST(test_cached_weather_must_be_no_older_than_one_sleep_period);
  RUN_TEST(test_weather_age_rounds_to_nearest_five_minutes);
  RUN_TEST(test_weather_age_rejects_invalid_or_future_timestamps);
  RUN_TEST(test_quiet_suppression_preserves_override_wakes);
  RUN_TEST(test_failure_window_uses_cache_only_when_clock_and_cache_are_valid);
  RUN_TEST(test_failure_cache_boundary_at_one_hour);
  return UNITY_END();
}
