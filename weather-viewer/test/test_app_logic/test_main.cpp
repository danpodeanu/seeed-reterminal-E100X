#include <unity.h>

#include <stdlib.h>
#include <time.h>

#include "app_logic.h"
#include "local_time.h"

#ifdef _WIN32
static inline struct tm* gmtime_r(const time_t* t, struct tm* out) {
  return gmtime_s(out, t) == 0 ? out : nullptr;
}
#endif

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

void test_qweather_response_code_accepts_only_200() {
  TEST_ASSERT_TRUE(app_logic::qweatherResponseOk("200"));
  TEST_ASSERT_FALSE(app_logic::qweatherResponseOk("201"));
  TEST_ASSERT_FALSE(app_logic::qweatherResponseOk("2000"));
  TEST_ASSERT_FALSE(app_logic::qweatherResponseOk("20"));
  TEST_ASSERT_FALSE(app_logic::qweatherResponseOk(""));
  TEST_ASSERT_FALSE(app_logic::qweatherResponseOk(nullptr));
  // Guard against a permissive prefix match. The response code is a fixed
  // 3-character decimal; trailing junk must not be accepted.
  TEST_ASSERT_FALSE(app_logic::qweatherResponseOk("200x"));
}

void test_qweather_icon_maps_to_wmo_buckets() {
  // Clear + partly cloudy pairs (day/night share the same downstream
  // bucket because conditionName() does not distinguish).
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherIconToWmoCode(100));  // clear day
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherIconToWmoCode(150));  // clear night
  TEST_ASSERT_EQUAL_INT(2, app_logic::qweatherIconToWmoCode(102));  // partly cloudy
  TEST_ASSERT_EQUAL_INT(3, app_logic::qweatherIconToWmoCode(104));  // overcast

  // Rain family stays in the WMO "Rain" bucket (61-67 or 80-82).
  const int lightRain = app_logic::qweatherIconToWmoCode(305);
  TEST_ASSERT_TRUE((lightRain >= 51 && lightRain <= 57) ||
                   (lightRain >= 61 && lightRain <= 67) ||
                   (lightRain >= 80 && lightRain <= 82));
  const int heavyRain = app_logic::qweatherIconToWmoCode(307);
  TEST_ASSERT_TRUE(heavyRain >= 61 && heavyRain <= 67);

  // Thunderstorm.
  const int thunder = app_logic::qweatherIconToWmoCode(302);
  TEST_ASSERT_TRUE(thunder >= 95);

  // Snow family stays in the WMO "Snow" bucket (71-77 or 85-86).
  const int snow = app_logic::qweatherIconToWmoCode(401);
  TEST_ASSERT_TRUE((snow >= 71 && snow <= 77) ||
                   (snow >= 85 && snow <= 86));

  // Fog / haze.
  TEST_ASSERT_EQUAL_INT(45, app_logic::qweatherIconToWmoCode(501));

  // Unknown / hot / cold return -1 so parseWeather can reject the payload.
  TEST_ASSERT_EQUAL_INT(-1, app_logic::qweatherIconToWmoCode(900));
  TEST_ASSERT_EQUAL_INT(-1, app_logic::qweatherIconToWmoCode(999));
  TEST_ASSERT_EQUAL_INT(-1, app_logic::qweatherIconToWmoCode(-1));
  TEST_ASSERT_EQUAL_INT(-1, app_logic::qweatherIconToWmoCode(12345));
}

void test_qweather_icon_night_flag_only_covers_150_range() {
  TEST_ASSERT_TRUE(app_logic::qweatherIconIsNight(150));
  TEST_ASSERT_TRUE(app_logic::qweatherIconIsNight(154));
  TEST_ASSERT_FALSE(app_logic::qweatherIconIsNight(149));
  TEST_ASSERT_FALSE(app_logic::qweatherIconIsNight(200));
  TEST_ASSERT_FALSE(app_logic::qweatherIconIsNight(100));
  // Rain / snow / fog codes always report as day (no paired night icon).
  TEST_ASSERT_FALSE(app_logic::qweatherIconIsNight(305));
  TEST_ASSERT_FALSE(app_logic::qweatherIconIsNight(501));
}

void test_rain_slot_threshold_is_shared_by_both_providers() {
  constexpr float minMm = 0.1f;
  constexpr int minProb = 30;
  // Both thresholds cleared.
  TEST_ASSERT_TRUE(app_logic::rainSlotQualifies(0.2f, 50, minMm, minProb));
  // Boundary: mm exactly at threshold, probability exactly at threshold.
  TEST_ASSERT_TRUE(app_logic::rainSlotQualifies(0.1f, 30, minMm, minProb));
  // Below mm threshold: reject.
  TEST_ASSERT_FALSE(app_logic::rainSlotQualifies(0.05f, 90, minMm, minProb));
  // Below probability threshold: reject.
  TEST_ASSERT_FALSE(app_logic::rainSlotQualifies(1.0f, 20, minMm, minProb));
  // Missing probability (-1): accept, so slots without a `pop` field still
  // trigger on liquid amount alone. This matches parseWeather behaviour.
  TEST_ASSERT_TRUE(app_logic::rainSlotQualifies(0.5f, -1, minMm, minProb));
  TEST_ASSERT_FALSE(app_logic::rainSlotQualifies(0.05f, -1, minMm, minProb));
}

void test_hex_decode_round_trips_known_bytes_and_rejects_bad_input() {
  uint8_t buffer[8] = {};
  // Valid 8-byte decode.
  const size_t n = app_logic::decodeHex("00ff1234deadbeef", 16, buffer,
                                        sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT(8u, n);
  const uint8_t expected[8] = {0x00, 0xFF, 0x12, 0x34,
                               0xDE, 0xAD, 0xBE, 0xEF};
  TEST_ASSERT_EQUAL_MEMORY(expected, buffer, sizeof(expected));

  // Uppercase and mixed case are accepted.
  TEST_ASSERT_EQUAL_UINT(
      1u, app_logic::decodeHex("aB", 2, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_UINT8(0xAB, buffer[0]);

  // Odd length rejected.
  TEST_ASSERT_EQUAL_UINT(
      0u, app_logic::decodeHex("abc", 3, buffer, sizeof(buffer)));
  // Non-hex character rejected.
  TEST_ASSERT_EQUAL_UINT(
      0u, app_logic::decodeHex("gg", 2, buffer, sizeof(buffer)));
  // Buffer too small rejected.
  uint8_t tiny[1] = {0xAA};
  TEST_ASSERT_EQUAL_UINT(0u, app_logic::decodeHex("1122", 4, tiny, 1));
  TEST_ASSERT_EQUAL_UINT8(0xAA, tiny[0]);  // buffer must be untouched
}

void test_base64url_encodes_canonical_examples() {
  char out[64] = {};
  // RFC 4648 §10 examples, adapted to URL-safe alphabet without padding.
  const uint8_t empty[1] = {0};
  TEST_ASSERT_EQUAL_UINT(0u,
      app_logic::encodeBase64Url(empty, 0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);

  TEST_ASSERT_EQUAL_UINT(2u, app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>("f"), 1, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Zg", out);

  TEST_ASSERT_EQUAL_UINT(3u, app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>("fo"), 2, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Zm8", out);

  TEST_ASSERT_EQUAL_UINT(4u, app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>("foo"), 3, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Zm9v", out);

  TEST_ASSERT_EQUAL_UINT(11u, app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>("foobar!?"), 8, out, sizeof(out)));
  // "foobar!?" uses the URL-safe '-' and '_' characters in the output.
  TEST_ASSERT_EQUAL_STRING("Zm9vYmFyIT8", out);
}

void test_base64url_rejects_overflow_and_preserves_bytes() {
  char small[3] = {'x', 'x', 'x'};
  // "foo" needs 4 chars + NUL; a 3-char buffer must return 0 and leave
  // the buffer alone.
  TEST_ASSERT_EQUAL_UINT(0u, app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>("foo"), 3, small, sizeof(small)));
  TEST_ASSERT_EQUAL_UINT8('x', static_cast<uint8_t>(small[0]));
}

void test_jwt_lifetime_clamps_to_qweather_bounds() {
  TEST_ASSERT_EQUAL_INT64(60, app_logic::clampJwtLifetime(0));
  TEST_ASSERT_EQUAL_INT64(60, app_logic::clampJwtLifetime(-5));
  TEST_ASSERT_EQUAL_INT64(60, app_logic::clampJwtLifetime(59));
  TEST_ASSERT_EQUAL_INT64(60, app_logic::clampJwtLifetime(60));
  TEST_ASSERT_EQUAL_INT64(300, app_logic::clampJwtLifetime(300));
  TEST_ASSERT_EQUAL_INT64(24 * 60 * 60,
      app_logic::clampJwtLifetime(24 * 60 * 60));
  TEST_ASSERT_EQUAL_INT64(24 * 60 * 60,
      app_logic::clampJwtLifetime(48 * 60 * 60));
}

void test_parse_iso8601_local_accepts_full_and_minute_form() {
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t ts = 0;
  TEST_ASSERT_TRUE(
      local_time::parseIso8601Local("2025-03-14T15:09:26", ts));
  struct tm r = {};
  gmtime_r(&ts, &r);
  TEST_ASSERT_EQUAL_INT(2025, r.tm_year + 1900);
  TEST_ASSERT_EQUAL_INT(3, r.tm_mon + 1);
  TEST_ASSERT_EQUAL_INT(14, r.tm_mday);
  TEST_ASSERT_EQUAL_INT(15, r.tm_hour);
  TEST_ASSERT_EQUAL_INT(9, r.tm_min);
  TEST_ASSERT_EQUAL_INT(26, r.tm_sec);

  // Seconds optional, default to zero.
  ts = 0;
  TEST_ASSERT_TRUE(local_time::parseIso8601Local("2025-03-14T15:09", ts));
  gmtime_r(&ts, &r);
  TEST_ASSERT_EQUAL_INT(0, r.tm_sec);
  TEST_ASSERT_EQUAL_INT(15, r.tm_hour);
}

void test_parse_iso8601_local_rejects_invalid_input() {
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t ts = 0;
  TEST_ASSERT_FALSE(local_time::parseIso8601Local(nullptr, ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("garbage", ts));
  // Missing time portion (only 3 numeric fields parsed).
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("2025-03-14", ts));
  // Out-of-range fields.
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("2025-13-01T00:00", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("2025-03-14T24:00", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("2025-03-14T15:60", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("1969-12-31T23:59", ts));
  // Silently-normalised dates must be rejected via the round-trip check.
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("2025-02-30T12:00", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Local("2025-04-31T12:00", ts));
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
  RUN_TEST(test_qweather_response_code_accepts_only_200);
  RUN_TEST(test_qweather_icon_maps_to_wmo_buckets);
  RUN_TEST(test_qweather_icon_night_flag_only_covers_150_range);
  RUN_TEST(test_rain_slot_threshold_is_shared_by_both_providers);
  RUN_TEST(test_hex_decode_round_trips_known_bytes_and_rejects_bad_input);
  RUN_TEST(test_base64url_encodes_canonical_examples);
  RUN_TEST(test_base64url_rejects_overflow_and_preserves_bytes);
  RUN_TEST(test_jwt_lifetime_clamps_to_qweather_bounds);
  RUN_TEST(test_parse_iso8601_local_accepts_full_and_minute_form);
  RUN_TEST(test_parse_iso8601_local_rejects_invalid_input);
  return UNITY_END();
}
