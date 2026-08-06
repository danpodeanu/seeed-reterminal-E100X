#include <unity.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <time.h>

#include "app_logic.h"
#include "battery_gauge_pure.h"
#include "compact_portrait_layout.h"
#include "local_time.h"
#include "text_render_pure.h"
#include "weather_quotes_bucket.h"

#ifdef _WIN32
static inline struct tm* gmtime_r(const time_t* t, struct tm* out) {
  return gmtime_s(out, t) == 0 ? out : nullptr;
}
#endif

void setUp() {}
void tearDown() {}

void test_e1005_compact_layout_stays_inside_panel() {
  using namespace compact_portrait_layout;
  TEST_ASSERT_TRUE(fitsPanel(480, 800));
  TEST_ASSERT_FALSE(fitsPanel(800, 480));
  TEST_ASSERT_GREATER_OR_EQUAL(ALERT_TOP + ALERT_HEIGHT,
                               heroTop(true));
  TEST_ASSERT_EQUAL_INT(FORECAST_TOP, forecastRowTop(0));
  TEST_ASSERT_EQUAL_INT(FOOTER_TOP, forecastRowsBottom() + 1);
}

void test_battery_gauge_decodes_e1005_words_and_rejects_missing_adc() {
  using namespace battery::pure;
  TEST_ASSERT_FALSE(adcPinsUsable(-1, -1));
  TEST_ASSERT_TRUE(adcPinsUsable(40, 1));
  TEST_ASSERT_EQUAL_HEX8(0x08, BQ27220_VOLTAGE_REGISTER);
  TEST_ASSERT_EQUAL_HEX8(0x14, BQ27220_AVERAGE_CURRENT_REGISTER);
  TEST_ASSERT_EQUAL_HEX8(0x2C, BQ27220_STATE_OF_CHARGE_REGISTER);
  TEST_ASSERT_EQUAL_HEX16(0x1234, littleEndianWord(0x34, 0x12));
  TEST_ASSERT_EQUAL_INT16(-100, littleEndianSignedWord(0x9C, 0xFF));
  TEST_ASSERT_TRUE(stateOfChargeValid(100));
  TEST_ASSERT_FALSE(stateOfChargeValid(101));
}

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

void test_qweather_alert_severity_rank_orders_worst_first() {
  TEST_ASSERT_EQUAL_INT(4, app_logic::qweatherAlertSeverityRank("Extreme"));
  TEST_ASSERT_EQUAL_INT(3, app_logic::qweatherAlertSeverityRank("Severe"));
  TEST_ASSERT_EQUAL_INT(2, app_logic::qweatherAlertSeverityRank("Moderate"));
  TEST_ASSERT_EQUAL_INT(1, app_logic::qweatherAlertSeverityRank("Minor"));
  // Any unknown / null / empty severity ranks as 0 so a labelled entry
  // always wins the "pick highest" comparison in qweather.cpp.
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherAlertSeverityRank("Unknown"));
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherAlertSeverityRank(""));
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherAlertSeverityRank(nullptr));
  // Case-sensitive: QWeather always emits the capitalised form; anything
  // else falls back to 0 so we never mis-rank a rogue value.
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherAlertSeverityRank("extreme"));
  TEST_ASSERT_EQUAL_INT(0, app_logic::qweatherAlertSeverityRank("SEVERE"));
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

// The openssl `priv:` output ships as "94:d1:33:..." across multiple lines,
// and users often paste it that way into secrets.h. The tester and the
// firmware both must accept it, otherwise a green tester run gives a false
// sense of confidence and the device silently rejects the key at runtime.
void test_normalize_hex_digits_strips_common_key_formats() {
  char out[128];
  // Colons between bytes (openssl `pkey -text -noout` format).
  size_t n = app_logic::normalizeHexDigits("94:d1:33", out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(6u, n);
  TEST_ASSERT_EQUAL_STRING("94d133", out);

  // Whitespace and newlines between groups.
  n = app_logic::normalizeHexDigits(
      "  94 d1\n33\t94\r\n1e ec 4d c5  ", out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(16u, n);
  TEST_ASSERT_EQUAL_STRING("94d133941eec4dc5", out);

  // Leading 0x / 0X prefix stripped exactly once.
  n = app_logic::normalizeHexDigits("0xdeadbeef", out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(8u, n);
  TEST_ASSERT_EQUAL_STRING("deadbeef", out);
  n = app_logic::normalizeHexDigits("0Xdeadbeef", out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(8u, n);
  TEST_ASSERT_EQUAL_STRING("deadbeef", out);

  // Empty input produces an empty string, not a failure.
  n = app_logic::normalizeHexDigits("", out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(0u, n);
  TEST_ASSERT_EQUAL_STRING("", out);

  // Non-hex, non-separator characters are rejected so garbage doesn't
  // silently decode to something plausible-looking.
  TEST_ASSERT_EQUAL_UINT(static_cast<size_t>(-1),
      app_logic::normalizeHexDigits("94g1", out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT(static_cast<size_t>(-1),
      app_logic::normalizeHexDigits("94,1", out, sizeof(out)));
  // ssh-style key dumps sometimes use '_' or '-' as separators; both
  // sides must refuse them so a paste from the wrong tool fails loudly
  // rather than decoding to a subtly different key.
  TEST_ASSERT_EQUAL_UINT(static_cast<size_t>(-1),
      app_logic::normalizeHexDigits("94_d1", out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT(static_cast<size_t>(-1),
      app_logic::normalizeHexDigits("94-d1", out, sizeof(out)));

  // Output buffer overflow is signalled the same way.
  char tiny[4];  // room for 3 chars + NUL
  TEST_ASSERT_EQUAL_UINT(static_cast<size_t>(-1),
      app_logic::normalizeHexDigits("abcd", tiny, sizeof(tiny)));

  // End-to-end: cleaned then decoded matches the raw-hex path.
  const char* colonForm =
      "94:d1:33:94:1e:ec:4d:c5:de:f2:b8:ff:76:01:ee:06:"
      "30:eb:38:20:1b:1b:b0:3a:23:16:f2:5f:fa:4c:bd:81";
  n = app_logic::normalizeHexDigits(colonForm, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT(64u, n);
  uint8_t decoded[32];
  const size_t decodedLen =
      app_logic::decodeHex(out, n, decoded, sizeof(decoded));
  TEST_ASSERT_EQUAL_UINT(32u, decodedLen);
  TEST_ASSERT_EQUAL_UINT8(0x94, decoded[0]);
  TEST_ASSERT_EQUAL_UINT8(0x81, decoded[31]);
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

// parseIso8601Utc turns a provider timestamp with an offset marker
// (Z or +/-HH:MM) into a UTC epoch, regardless of the device timezone.
// This is the QWeather "2026-08-01T03:16+01:00" case that used to be
// silently reinterpreted as 03:16 in the device's own timezone.
void test_parse_iso8601_utc_normalises_offset() {
  // Force device to CST-8 to prove the parser ignores it.
  setenv("TZ", "CST-8", 1);
  tzset();
  time_t ts = 0;
  // 03:16 in BST (+01:00) is 02:16 UTC = 1754014560 (Aug 1 2025 02:16Z).
  TEST_ASSERT_TRUE(
      local_time::parseIso8601Utc("2025-08-01T03:16+01:00", ts));
  TEST_ASSERT_EQUAL(1754014560, static_cast<long>(ts));
  // Explicit Z (UTC) round-trips identically.
  TEST_ASSERT_TRUE(
      local_time::parseIso8601Utc("2025-08-01T02:16Z", ts));
  TEST_ASSERT_EQUAL(1754014560, static_cast<long>(ts));
  // Negative offset shifts the other direction: 22:00-05:00 wall clock
  // is 03:00 UTC on the *next* day (Aug 2), i.e. 1754103600.
  TEST_ASSERT_TRUE(
      local_time::parseIso8601Utc("2025-08-01T22:00-05:00", ts));
  TEST_ASSERT_EQUAL(1754103600, static_cast<long>(ts));
  // Full seconds form is accepted.
  TEST_ASSERT_TRUE(
      local_time::parseIso8601Utc("2025-08-01T02:16:30Z", ts));
  TEST_ASSERT_EQUAL(1754014590, static_cast<long>(ts));
}

void test_parse_iso8601_utc_rejects_missing_offset_or_garbage() {
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t ts = 0;
  // No offset marker at all: the caller must reach for parseIso8601Local.
  TEST_ASSERT_FALSE(local_time::parseIso8601Utc("2025-08-01T02:16", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Utc(nullptr, ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Utc("", ts));
  TEST_ASSERT_FALSE(local_time::parseIso8601Utc("garbage", ts));
  // Out-of-range fields still rejected.
  TEST_ASSERT_FALSE(local_time::parseIso8601Utc("2025-13-01T00:00Z", ts));
}

// Every branch of conditionName() -- the string that actually ends up on the
// e-paper panel. If someone tweaks the WMO bucket table without updating the
// UI (or vice versa) this catches the divergence immediately.
void test_condition_name_covers_every_wmo_bucket() {
  TEST_ASSERT_EQUAL_STRING("Clear", app_logic::conditionName(0));
  TEST_ASSERT_EQUAL_STRING("Partly cloudy", app_logic::conditionName(1));
  TEST_ASSERT_EQUAL_STRING("Partly cloudy", app_logic::conditionName(2));
  TEST_ASSERT_EQUAL_STRING("Overcast", app_logic::conditionName(3));
  TEST_ASSERT_EQUAL_STRING("Fog", app_logic::conditionName(45));
  TEST_ASSERT_EQUAL_STRING("Fog", app_logic::conditionName(48));
  TEST_ASSERT_EQUAL_STRING("Drizzle", app_logic::conditionName(51));
  TEST_ASSERT_EQUAL_STRING("Drizzle", app_logic::conditionName(57));
  TEST_ASSERT_EQUAL_STRING("Rain", app_logic::conditionName(61));
  TEST_ASSERT_EQUAL_STRING("Rain", app_logic::conditionName(65));
  TEST_ASSERT_EQUAL_STRING("Rain", app_logic::conditionName(80));
  TEST_ASSERT_EQUAL_STRING("Rain", app_logic::conditionName(82));
  TEST_ASSERT_EQUAL_STRING("Snow", app_logic::conditionName(71));
  TEST_ASSERT_EQUAL_STRING("Snow", app_logic::conditionName(75));
  TEST_ASSERT_EQUAL_STRING("Snow", app_logic::conditionName(85));
  TEST_ASSERT_EQUAL_STRING("Snow", app_logic::conditionName(86));
  TEST_ASSERT_EQUAL_STRING("Thunderstorm", app_logic::conditionName(95));
  TEST_ASSERT_EQUAL_STRING("Thunderstorm", app_logic::conditionName(99));
}

// Codes that fall between named buckets (or a garbled provider payload) must
// degrade to "Mixed weather" rather than crash or print an empty string.
void test_condition_name_falls_back_to_mixed_weather() {
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(4));
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(50));
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(60));
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(70));
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(78));
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(84));
  TEST_ASSERT_EQUAL_STRING("Mixed weather", app_logic::conditionName(-1));
}

// End-to-end mapping check: for every canonical QWeather icon we care about,
// icon -> WMO -> UI label produces the label a user would expect to see on
// the panel. This is the layer that would have covered the "did we display
// sunny correctly" concern -- and the reason it survived the Chinese-lang
// era is that the firmware only ever consumed the icon, never the text.
void test_qweather_icon_to_condition_name_end_to_end() {
  struct Case {
    int qweatherIcon;
    const char* expected;
  };
  const Case cases[] = {
      // 1xx cloud cover (both day and night halves).
      {100, "Clear"},          {150, "Clear"},
      {101, "Partly cloudy"},  {151, "Partly cloudy"},
      {102, "Partly cloudy"},  {152, "Partly cloudy"},
      {103, "Partly cloudy"},  {153, "Partly cloudy"},
      {104, "Overcast"},       {154, "Overcast"},
      // 3xx precipitation.
      {300, "Rain"},           {305, "Drizzle"},
      {306, "Rain"},           {307, "Rain"},
      {308, "Rain"},           {313, "Rain"},
      {316, "Rain"},           {318, "Rain"},
      {302, "Thunderstorm"},   {303, "Thunderstorm"},
      {304, "Thunderstorm"},
      // 4xx snow.
      {400, "Snow"},           {401, "Snow"},
      {402, "Snow"},           {406, "Snow"},
      {404, "Rain"},           // sleet is bucketed with freezing rain (66)
      // 5xx fog / haze.
      {500, "Fog"},            {501, "Fog"},
      {502, "Fog"},            {503, "Fog"},
  };
  for (const auto& c : cases) {
    const int wmo = app_logic::qweatherIconToWmoCode(c.qweatherIcon);
    TEST_ASSERT_NOT_EQUAL(-1, wmo);
    const char* got = app_logic::conditionName(wmo);
    if (strcmp(got, c.expected) != 0) {
      char msg[96];
      snprintf(msg, sizeof(msg),
               "QWeather icon %d -> WMO %d -> \"%s\" (expected \"%s\")",
               c.qweatherIcon, wmo, got, c.expected);
      TEST_FAIL_MESSAGE(msg);
    }
  }
}

// Open-Meteo already returns WMO codes directly, so parseWeather feeds them
// into conditionName untouched. Spot-check the same buckets to make sure the
// two providers converge on identical labels for equivalent conditions.
void test_open_meteo_weathercode_to_condition_name() {
  TEST_ASSERT_EQUAL_STRING("Clear", app_logic::conditionName(0));
  TEST_ASSERT_EQUAL_STRING("Partly cloudy", app_logic::conditionName(2));
  TEST_ASSERT_EQUAL_STRING("Overcast", app_logic::conditionName(3));
  TEST_ASSERT_EQUAL_STRING("Fog", app_logic::conditionName(45));
  TEST_ASSERT_EQUAL_STRING("Drizzle", app_logic::conditionName(53));
  TEST_ASSERT_EQUAL_STRING("Rain", app_logic::conditionName(63));
  TEST_ASSERT_EQUAL_STRING("Rain", app_logic::conditionName(81));
  TEST_ASSERT_EQUAL_STRING("Snow", app_logic::conditionName(73));
  TEST_ASSERT_EQUAL_STRING("Snow", app_logic::conditionName(85));
  TEST_ASSERT_EQUAL_STRING("Thunderstorm", app_logic::conditionName(95));
  TEST_ASSERT_EQUAL_STRING("Thunderstorm", app_logic::conditionName(96));
}

// The gzip framing on QWeather's paid host adds a small envelope around
// the raw deflate stream (see RFC 1952). If we misread that envelope we
// feed miniz garbage and inflate fails; if we skip too much we clip real
// deflate bytes. These fixtures exercise the flag combinations we actually
// see in the wild (minimal + all-flags-set) plus common corruption modes.
void test_gzip_deflate_span_parses_minimal_and_full_headers() {
  size_t start = 0;
  size_t length = 0;

  // Minimal gzip header (10 bytes, no flags) + 1-byte deflate stream + 8
  // bytes trailer. This is exactly what miniz/gzip produce with no name/OS
  // metadata. app_logic::gzipDeflateSpan must report the stream at offset
  // 10 with length 1.
  const uint8_t minimalGzip[] = {
      0x1f, 0x8b, 0x08, 0x00,        // magic, deflate, no flags
      0x00, 0x00, 0x00, 0x00,        // mtime
      0x00,                          // xfl
      0x00,                          // OS
      0xAA,                          // deflate stream (1 byte)
      0x00, 0x00, 0x00, 0x00,        // CRC32
      0x00, 0x00, 0x00, 0x00,        // ISIZE
  };
  TEST_ASSERT_TRUE(app_logic::gzipDeflateSpan(
      minimalGzip, sizeof(minimalGzip), &start, &length));
  TEST_ASSERT_EQUAL_UINT(10u, start);
  TEST_ASSERT_EQUAL_UINT(1u, length);

  // Full flags: FEXTRA (3 bytes payload) + FNAME "hi" + FCOMMENT "c" +
  // FHCRC (2 bytes). Deflate stream is 2 bytes.
  const uint8_t fullFlagsGzip[] = {
      0x1f, 0x8b, 0x08,
      0x1e,                          // flags: FEXTRA|FNAME|FCOMMENT|FHCRC
      0x00, 0x00, 0x00, 0x00,        // mtime
      0x00, 0x00,                    // xfl, OS
      0x03, 0x00,                    // FEXTRA length = 3
      0xAA, 0xBB, 0xCC,              // FEXTRA payload
      'h', 'i', 0x00,                // FNAME
      'c', 0x00,                     // FCOMMENT
      0xDD, 0xEE,                    // FHCRC
      0xF1, 0xF2,                    // deflate stream (2 bytes)
      0x00, 0x00, 0x00, 0x00,        // CRC32
      0x00, 0x00, 0x00, 0x00,        // ISIZE
  };
  TEST_ASSERT_TRUE(app_logic::gzipDeflateSpan(
      fullFlagsGzip, sizeof(fullFlagsGzip), &start, &length));
  // 10 header + 2 xlen + 3 extra + 3 fname + 2 fcomment + 2 fhcrc = 22
  TEST_ASSERT_EQUAL_UINT(22u, start);
  TEST_ASSERT_EQUAL_UINT(2u, length);
}

void test_gzip_deflate_span_rejects_malformed_inputs() {
  size_t start = 0;
  size_t length = 0;
  // Too short to be a valid gzip stream.
  const uint8_t tooShort[10] = {0x1f, 0x8b, 0x08, 0};
  TEST_ASSERT_FALSE(app_logic::gzipDeflateSpan(
      tooShort, sizeof(tooShort), &start, &length));
  // Wrong magic (plain JSON should pass through the caller as "not gzip").
  const uint8_t notGzip[20] = {'{', '"', 'a', '"', ':', '1', '}', 0};
  TEST_ASSERT_FALSE(app_logic::gzipDeflateSpan(
      notGzip, sizeof(notGzip), &start, &length));
  // Correct magic but unsupported compression method.
  uint8_t badMethod[20] = {0x1f, 0x8b, 0x09, 0};
  TEST_ASSERT_FALSE(app_logic::gzipDeflateSpan(
      badMethod, sizeof(badMethod), &start, &length));
  // FEXTRA declares a length that runs past the buffer -- must fail rather
  // than reading garbage.
  uint8_t truncatedExtra[20] = {
      0x1f, 0x8b, 0x08, 0x04,        // flags = FEXTRA
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      0xFF, 0xFF,                    // xlen = 65535, way past buffer
  };
  TEST_ASSERT_FALSE(app_logic::gzipDeflateSpan(
      truncatedExtra, sizeof(truncatedExtra), &start, &length));
  // FNAME never terminates before the buffer ends.
  uint8_t truncatedName[20] = {
      0x1f, 0x8b, 0x08, 0x08,        // flags = FNAME
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      'n', 'a', 'm', 'e',            // no NUL before end
      'n', 'a', 'm', 'e',
      'n', 'a',
  };
  TEST_ASSERT_FALSE(app_logic::gzipDeflateSpan(
      truncatedName, sizeof(truncatedName), &start, &length));
}

// LOCATION_NAME can contain 2-byte UTF-8 (e.g. "München", "São Paulo").
// displayText must pass those bytes through untouched so a smooth VLW
// font on the device can render the diacritic glyphs.
void test_display_text_preserves_utf8_city_names() {
  TEST_ASSERT_EQUAL_STRING("M\xC3\xBCnchen",
      text_render::pure::displayText("M\xC3\xBCnchen").c_str());
  TEST_ASSERT_EQUAL_STRING("S\xC3\xA3o Paulo",
      text_render::pure::displayText("S\xC3\xA3o Paulo").c_str());
  TEST_ASSERT_EQUAL_STRING("Z\xC3\xBCrich",
      text_render::pure::displayText("Z\xC3\xBCrich").c_str());
  TEST_ASSERT_EQUAL_STRING("Bogot\xC3\xA1",
      text_render::pure::displayText("Bogot\xC3\xA1").c_str());
  TEST_ASSERT_EQUAL_STRING("K\xC3\xB8benhavn",
      text_render::pure::displayText("K\xC3\xB8benhavn").c_str());
}

// 3-byte UTF-8 (e.g. CJK city names) must survive normalization as well,
// even though DejaVu Sans Bold doesn't cover most of that range - the
// on-device fallback simply misses glyphs; it shouldn't corrupt bytes.
void test_display_text_preserves_3byte_utf8_city_names() {
  // "Beijing" 北京 - two 3-byte codepoints U+5317 U+4EAC
  TEST_ASSERT_EQUAL_STRING("\xE5\x8C\x97\xE4\xBA\xAC",
      text_render::pure::displayText("\xE5\x8C\x97\xE4\xBA\xAC").c_str());
}

// The footer proverb's bucket for a given WMO code MUST match the
// bucket ranges used by weather_icons::wmoToIcon; otherwise the hero
// icon (e.g. showing rain) and the proverb (e.g. talking about snow)
// disagree on the current condition. The mapping is duplicated because
// the icon module doesn't depend on the quotes module -- this test
// pins the ranges so drift is caught before it ships.
void test_wmo_to_bucket_covers_shared_ranges() {
  using namespace weather_quotes::pure;

  // Single-code buckets that mirror wmoToIcon's switch head.
  TEST_ASSERT_EQUAL(IDX_CLEAR, wmoToBucketIndex(0));
  TEST_ASSERT_EQUAL(IDX_PARTLY_CLOUDY, wmoToBucketIndex(1));
  TEST_ASSERT_EQUAL(IDX_PARTLY_CLOUDY, wmoToBucketIndex(2));
  TEST_ASSERT_EQUAL(IDX_OVERCAST, wmoToBucketIndex(3));
  TEST_ASSERT_EQUAL(IDX_FOG, wmoToBucketIndex(45));
  TEST_ASSERT_EQUAL(IDX_HAZE, wmoToBucketIndex(48));

  // Drizzle 51-55, freezing drizzle 56/57 goes to sleet.
  for (int c = 51; c <= 55; ++c) {
    TEST_ASSERT_EQUAL(IDX_DRIZZLE, wmoToBucketIndex(c));
  }
  TEST_ASSERT_EQUAL(IDX_SLEET, wmoToBucketIndex(56));
  TEST_ASSERT_EQUAL(IDX_SLEET, wmoToBucketIndex(57));

  // Rain 61-65, freezing rain 66/67 also sleet.
  for (int c = 61; c <= 65; ++c) {
    TEST_ASSERT_EQUAL(IDX_RAIN, wmoToBucketIndex(c));
  }
  TEST_ASSERT_EQUAL(IDX_SLEET, wmoToBucketIndex(66));
  TEST_ASSERT_EQUAL(IDX_SLEET, wmoToBucketIndex(67));

  // Snow 71-77 and 85/86 (snow showers) share the SNOW bucket.
  for (int c = 71; c <= 77; ++c) {
    TEST_ASSERT_EQUAL(IDX_SNOW, wmoToBucketIndex(c));
  }
  TEST_ASSERT_EQUAL(IDX_SNOW, wmoToBucketIndex(85));
  TEST_ASSERT_EQUAL(IDX_SNOW, wmoToBucketIndex(86));

  // Rain showers 80-82 map to SHOWERS.
  for (int c = 80; c <= 82; ++c) {
    TEST_ASSERT_EQUAL(IDX_SHOWERS, wmoToBucketIndex(c));
  }

  // Thunderstorms 95-99.
  for (int c = 95; c <= 99; ++c) {
    TEST_ASSERT_EQUAL(IDX_THUNDERSTORM, wmoToBucketIndex(c));
  }
}

// Codes outside any known WMO group must fall back to the
// condition-agnostic UNIVERSAL bucket so callers never end up with a
// nullptr sub-array. Negative sentinels used by the providers
// (weather.weatherCode=-1 for "unknown") are the common case here.
void test_wmo_to_bucket_falls_back_to_universal_for_unknown() {
  using namespace weather_quotes::pure;
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(-1));
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(4));   // no code 4
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(50));  // gap
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(70));  // gap
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(87));  // gap
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(94));  // gap
  TEST_ASSERT_EQUAL(IDX_UNIVERSAL, wmoToBucketIndex(100)); // above range
}

#include "sd_ota_pure.h"

// ---------------------------------------------------------------------------
// sd_ota::TagScanner - streaming search for the model tag inside the
// incoming OTA image. Naive one-pass matcher because our tag has no
// self-overlap; these tests pin that assumption.
// ---------------------------------------------------------------------------

namespace {
constexpr const char kTag[] = "reterminal-ota:E1004";
constexpr size_t kTagLen = sizeof(kTag) - 1;

// Feed helper: run the scanner over a std::string treated as bytes.
bool scan(const char* stream, size_t n) {
  sd_ota::TagScanner s(reinterpret_cast<const uint8_t*>(kTag), kTagLen);
  s.feed(reinterpret_cast<const uint8_t*>(stream), n);
  return s.found;
}
}  // namespace

void test_sd_ota_tag_empty_stream_no_match() {
  TEST_ASSERT_FALSE(scan("", 0));
}

void test_sd_ota_tag_exact_match_alone() {
  TEST_ASSERT_TRUE(scan(kTag, kTagLen));
}

void test_sd_ota_tag_embedded_in_larger_payload() {
  const std::string payload =
      std::string("\x00\x01\x02random header\xffmore", 20) + kTag +
      std::string("trailer bytes", 13);
  TEST_ASSERT_TRUE(scan(payload.data(), payload.size()));
}

void test_sd_ota_tag_missing_when_wrong_model() {
  // Same shape, different model - must NOT match.
  const char other[] = "reterminal-ota:E1001";
  TEST_ASSERT_FALSE(scan(other, sizeof(other) - 1));
}

void test_sd_ota_tag_split_across_chunks() {
  // Split the tag across a chunk boundary at every possible position; the
  // scanner must find it because it carries state between feed() calls.
  for (size_t cut = 1; cut < kTagLen; ++cut) {
    sd_ota::TagScanner s(reinterpret_cast<const uint8_t*>(kTag), kTagLen);
    s.feed(reinterpret_cast<const uint8_t*>(kTag), cut);
    TEST_ASSERT_FALSE_MESSAGE(s.found, "matched too early");
    s.feed(reinterpret_cast<const uint8_t*>(kTag) + cut, kTagLen - cut);
    TEST_ASSERT_TRUE_MESSAGE(s.found, "did not match after boundary");
  }
}

void test_sd_ota_tag_partial_then_restart() {
  // "reterminal-ota:E10..." then falls off, then the real tag arrives.
  // The naive restart works because 'r' does not repeat inside our tag
  // until well past position 1 - guarding the "tag has no self-overlap"
  // assumption. If someone renames the tag to something like
  // "reter reterminal..." this test would catch the regression.
  const std::string junk = "reterminal-ota:E10ZZ";  // wrong tail
  std::string stream = junk + std::string(kTag) + "trailer";
  TEST_ASSERT_TRUE(scan(stream.data(), stream.size()));
}

void test_sd_ota_tag_stays_matched_across_more_data() {
  // Once matched, further feed() calls do not un-match.
  sd_ota::TagScanner s(reinterpret_cast<const uint8_t*>(kTag), kTagLen);
  s.feed(reinterpret_cast<const uint8_t*>(kTag), kTagLen);
  TEST_ASSERT_TRUE(s.found);
  s.feed(reinterpret_cast<const uint8_t*>("more junk"), 9);
  TEST_ASSERT_TRUE(s.found);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_e1005_compact_layout_stays_inside_panel);
  RUN_TEST(test_battery_gauge_decodes_e1005_words_and_rejects_missing_adc);
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
  RUN_TEST(test_qweather_alert_severity_rank_orders_worst_first);
  RUN_TEST(test_rain_slot_threshold_is_shared_by_both_providers);
  RUN_TEST(test_hex_decode_round_trips_known_bytes_and_rejects_bad_input);
  RUN_TEST(test_normalize_hex_digits_strips_common_key_formats);
  RUN_TEST(test_base64url_encodes_canonical_examples);
  RUN_TEST(test_base64url_rejects_overflow_and_preserves_bytes);
  RUN_TEST(test_jwt_lifetime_clamps_to_qweather_bounds);
  RUN_TEST(test_parse_iso8601_local_accepts_full_and_minute_form);
  RUN_TEST(test_parse_iso8601_local_rejects_invalid_input);
  RUN_TEST(test_parse_iso8601_utc_normalises_offset);
  RUN_TEST(test_parse_iso8601_utc_rejects_missing_offset_or_garbage);
  RUN_TEST(test_condition_name_covers_every_wmo_bucket);
  RUN_TEST(test_condition_name_falls_back_to_mixed_weather);
  RUN_TEST(test_qweather_icon_to_condition_name_end_to_end);
  RUN_TEST(test_open_meteo_weathercode_to_condition_name);
  RUN_TEST(test_gzip_deflate_span_parses_minimal_and_full_headers);
  RUN_TEST(test_gzip_deflate_span_rejects_malformed_inputs);
  RUN_TEST(test_display_text_preserves_utf8_city_names);
  RUN_TEST(test_display_text_preserves_3byte_utf8_city_names);
  RUN_TEST(test_wmo_to_bucket_covers_shared_ranges);
  RUN_TEST(test_wmo_to_bucket_falls_back_to_universal_for_unknown);
  RUN_TEST(test_sd_ota_tag_empty_stream_no_match);
  RUN_TEST(test_sd_ota_tag_exact_match_alone);
  RUN_TEST(test_sd_ota_tag_embedded_in_larger_payload);
  RUN_TEST(test_sd_ota_tag_missing_when_wrong_model);
  RUN_TEST(test_sd_ota_tag_split_across_chunks);
  RUN_TEST(test_sd_ota_tag_partial_then_restart);
  RUN_TEST(test_sd_ota_tag_stays_matched_across_more_data);
  return UNITY_END();
}
