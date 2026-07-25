#include <unity.h>

#include <string>
#include <vector>

#include "app_logic.h"
#include "photo_manifest.h"

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

void test_shuffle_is_a_permutation_and_uses_only_valid_indices() {
  // Feed a deterministic sequence that maxes out the "swap with last"
  // choice at every step (j == 0 for each i). That still produces a
  // permutation, and lets the test assert exact expected order.
  std::vector<int> list = {1, 2, 3, 4, 5};
  auto rng = [](size_t /*upperExclusive*/) -> size_t { return 0; };
  app_logic::shuffleInPlace(list, rng);
  // With j == 0 the algorithm rotates left one step per iteration.
  // Starting {1,2,3,4,5} → i=5 swap(4,0):{5,2,3,4,1} → i=4 swap(3,0):{4,2,3,5,1}
  // → i=3 swap(2,0):{3,2,4,5,1} → i=2 swap(1,0):{2,3,4,5,1}
  TEST_ASSERT_EQUAL_INT(5, static_cast<int>(list.size()));
  TEST_ASSERT_EQUAL_INT(2, list[0]);
  TEST_ASSERT_EQUAL_INT(3, list[1]);
  TEST_ASSERT_EQUAL_INT(4, list[2]);
  TEST_ASSERT_EQUAL_INT(5, list[3]);
  TEST_ASSERT_EQUAL_INT(1, list[4]);
}

void test_shuffle_identity_when_rng_picks_last_index() {
  // If the RNG always returns i-1, no swap is needed and the list is
  // unchanged. This guards against an off-by-one that would corrupt the
  // list when the RNG happens to draw the current position.
  std::vector<int> list = {10, 20, 30, 40};
  auto rng = [](size_t upperExclusive) -> size_t {
    return upperExclusive - 1;
  };
  app_logic::shuffleInPlace(list, rng);
  TEST_ASSERT_EQUAL_INT(10, list[0]);
  TEST_ASSERT_EQUAL_INT(20, list[1]);
  TEST_ASSERT_EQUAL_INT(30, list[2]);
  TEST_ASSERT_EQUAL_INT(40, list[3]);
}

void test_shuffle_handles_empty_and_singleton_lists() {
  auto rng = [](size_t) -> size_t {
    TEST_FAIL_MESSAGE("RNG must not be called for lists of length <= 1");
    return 0;
  };
  std::vector<int> empty;
  app_logic::shuffleInPlace(empty, rng);
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(empty.size()));
  std::vector<int> singleton = {42};
  app_logic::shuffleInPlace(singleton, rng);
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(singleton.size()));
  TEST_ASSERT_EQUAL_INT(42, singleton[0]);
}

static photo_manifest::Status inspectStr(const std::string& json,
                                         const char* expected,
                                         std::string& found) {
  return photo_manifest::inspect(json.c_str(), json.size(), expected, found);
}

void test_photo_manifest_matches_when_version_is_expected() {
  const std::string json =
      R"({"_schema":"reterminal-photos-v1","dither_version":"v1"})";
  std::string found;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(photo_manifest::Status::Matches),
      static_cast<int>(inspectStr(json, "v1", found)));
  TEST_ASSERT_EQUAL_STRING("v1", found.c_str());
}

void test_photo_manifest_reports_stale_when_versions_differ() {
  const std::string json =
      R"({"_schema":"reterminal-photos-v1","dither_version":"v2"})";
  std::string found;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(photo_manifest::Status::StaleDither),
      static_cast<int>(inspectStr(json, "v1", found)));
  TEST_ASSERT_EQUAL_STRING("v2", found.c_str());
}

void test_photo_manifest_rejects_mismatched_schema() {
  const std::string json =
      R"({"_schema":"some-other-schema","dither_version":"v1"})";
  std::string found;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(photo_manifest::Status::Unrecognised),
      static_cast<int>(inspectStr(json, "v1", found)));
}

void test_photo_manifest_rejects_missing_version_field() {
  const std::string json = R"({"_schema":"reterminal-photos-v1"})";
  std::string found;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(photo_manifest::Status::Unrecognised),
      static_cast<int>(inspectStr(json, "v1", found)));
}

void test_photo_manifest_ignores_nested_matching_field_names() {
  // A `dither_version` string nested inside another object must not fool the
  // parser into thinking the manifest carries one at the top level.
  const std::string json =
      R"({"_schema":"reterminal-photos-v1",)"
      R"("nested":{"dither_version":"v9"}})";
  std::string found;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(photo_manifest::Status::Unrecognised),
      static_cast<int>(inspectStr(json, "v1", found)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_startup_beep_only_for_cold_boot_and_button_wake);
  RUN_TEST(test_quiet_hours_boundaries);
  RUN_TEST(test_sleep_until_same_time_means_next_day);
  RUN_TEST(test_daily_ntp_refresh_boundaries);
  RUN_TEST(test_photo_direction_matches_buttons);
  RUN_TEST(test_photo_index_wraps_in_both_directions);
  RUN_TEST(test_shuffle_is_a_permutation_and_uses_only_valid_indices);
  RUN_TEST(test_shuffle_identity_when_rng_picks_last_index);
  RUN_TEST(test_shuffle_handles_empty_and_singleton_lists);
  RUN_TEST(test_photo_manifest_matches_when_version_is_expected);
  RUN_TEST(test_photo_manifest_reports_stale_when_versions_differ);
  RUN_TEST(test_photo_manifest_rejects_mismatched_schema);
  RUN_TEST(test_photo_manifest_rejects_missing_version_field);
  RUN_TEST(test_photo_manifest_ignores_nested_matching_field_names);
  return UNITY_END();
}
