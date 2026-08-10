#include <unity.h>

#include <string>
#include <vector>

#include "app_logic.h"
#include "low_battery.h"
#include "panel_traits.h"
#include "photo_geom.h"
#include "photo_manifest.h"
#include "photo_orientation.h"
#include "screen_capture_png_layout.h"
#include "usb_screen_capture_protocol.h"

void setUp() {}
void tearDown() {}

void test_low_battery_warns_when_below_threshold() {
  // enabled + charger present + not on USB + pct < 5 -> warn
  TEST_ASSERT_TRUE(low_battery::shouldWarn(true, true, false, 4));
  TEST_ASSERT_TRUE(low_battery::shouldWarn(true, true, false, 0));
}

void test_low_battery_skips_when_disabled() {
  TEST_ASSERT_FALSE(low_battery::shouldWarn(false, true, false, 0));
}

void test_low_battery_skips_when_charger_missing() {
  // Older E1001/E1002 without SY6974B: batteryValid=false -> skip
  TEST_ASSERT_FALSE(low_battery::shouldWarn(true, false, false, 0));
}

void test_low_battery_skips_when_charging() {
  // externalPower=true means USB is plugged in
  TEST_ASSERT_FALSE(low_battery::shouldWarn(true, true, true, 2));
}

void test_low_battery_skips_when_pct_uninitialised() {
  // sensors::Readings default pct=-1 (readAll not called yet)
  TEST_ASSERT_FALSE(low_battery::shouldWarn(true, true, false, -1));
}

void test_low_battery_skips_at_or_above_threshold() {
  TEST_ASSERT_FALSE(low_battery::shouldWarn(true, true, false, 5));
  TEST_ASSERT_FALSE(low_battery::shouldWarn(true, true, false, 100));
}

void test_low_battery_custom_threshold() {
  // Used at build time to force the screen without draining a battery
  TEST_ASSERT_TRUE(low_battery::shouldWarn(true, true, false, 50, 100));
  TEST_ASSERT_FALSE(low_battery::shouldWarn(true, true, false, 50, 20));
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

void test_failed_photo_advances_after_refresh_only_redraw() {
  TEST_ASSERT_EQUAL_INT(1, app_logic::failedPhotoAdvance(0));
  TEST_ASSERT_EQUAL_INT(1, app_logic::failedPhotoAdvance(1));
  TEST_ASSERT_EQUAL_INT(-1, app_logic::failedPhotoAdvance(-1));
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

// Landscape rotation for E1004. Native panel is 1200 x 1600 (portrait);
// effective landscape frame is 1600 x 1200.
void test_photo_geom_native_is_identity() {
  int nx, ny;
  photo_geom::effToNative(photo_geom::kNative,
                          /*effW=*/1200, /*effH=*/1600,
                          /*eX=*/17, /*eY=*/23, nx, ny);
  TEST_ASSERT_EQUAL_INT(17, nx);
  TEST_ASSERT_EQUAL_INT(23, ny);
}

void test_photo_geom_rotate_cw_maps_corners() {
  const int effW = 1600, effH = 1200;
  int nx, ny;
  // Top-left of the effective frame lands at native top-right on E1004.
  photo_geom::effToNative(photo_geom::kRotateCW, effW, effH, 0, 0, nx, ny);
  TEST_ASSERT_EQUAL_INT(0, nx);
  TEST_ASSERT_EQUAL_INT(effW - 1, ny);
  // Top-right of effective -> native top-left.
  photo_geom::effToNative(photo_geom::kRotateCW, effW, effH, effW - 1, 0,
                          nx, ny);
  TEST_ASSERT_EQUAL_INT(0, nx);
  TEST_ASSERT_EQUAL_INT(0, ny);
  // Bottom-right of effective -> native bottom-left.
  photo_geom::effToNative(photo_geom::kRotateCW, effW, effH,
                          effW - 1, effH - 1, nx, ny);
  TEST_ASSERT_EQUAL_INT(effH - 1, nx);
  TEST_ASSERT_EQUAL_INT(0, ny);
}

void test_photo_geom_rotate_ccw_maps_corners() {
  const int effW = 1600, effH = 1200;
  int nx, ny;
  photo_geom::effToNative(photo_geom::kRotateCCW, effW, effH, 0, 0,
                          nx, ny);
  TEST_ASSERT_EQUAL_INT(effH - 1, nx);
  TEST_ASSERT_EQUAL_INT(0, ny);
  photo_geom::effToNative(photo_geom::kRotateCCW, effW, effH, effW - 1, 0,
                          nx, ny);
  TEST_ASSERT_EQUAL_INT(effH - 1, nx);
  TEST_ASSERT_EQUAL_INT(effW - 1, ny);
  photo_geom::effToNative(photo_geom::kRotateCCW, effW, effH,
                          effW - 1, effH - 1, nx, ny);
  TEST_ASSERT_EQUAL_INT(0, nx);
  TEST_ASSERT_EQUAL_INT(effW - 1, ny);
}

void test_photo_geom_rotate_cw_and_ccw_are_inverses() {
  // Chaining a CW and CCW mapping (with swapped effective / native dims on
  // the way back) must return the original point. Guards the sign in the
  // "effW - 1 - eX" style expressions.
  const int effW = 1600, effH = 1200;
  for (int eX = 0; eX < effW; eX += 137) {
    for (int eY = 0; eY < effH; eY += 91) {
      int nx, ny;
      photo_geom::effToNative(photo_geom::kRotateCW, effW, effH, eX, eY,
                              nx, ny);
      // Undo CW by treating native as the effective frame of a CCW mapping:
      // native dims are (effH, effW) after the CW turn.
      int backX, backY;
      photo_geom::effToNative(photo_geom::kRotateCCW, effH, effW, nx, ny,
                              backX, backY);
      TEST_ASSERT_EQUAL_INT(eX, backX);
      TEST_ASSERT_EQUAL_INT(eY, backY);
    }
  }
}

void test_panel_rotation_is_only_applied_to_e1005() {
  TEST_ASSERT_EQUAL_INT(0, panel_traits::displayRotationForModel(1001));
  TEST_ASSERT_EQUAL_INT(0, panel_traits::displayRotationForModel(1002));
  TEST_ASSERT_EQUAL_INT(0, panel_traits::displayRotationForModel(1003));
  TEST_ASSERT_EQUAL_INT(0, panel_traits::displayRotationForModel(1004));
  TEST_ASSERT_EQUAL_INT(1, panel_traits::displayRotationForModel(1005));
  TEST_ASSERT_EQUAL_INT(0, panel_traits::DISPLAY_ROTATION);
}

void test_e1005_orientation_geometry_and_rotation() {
  using photo_orientation::Orientation;
  TEST_ASSERT_FALSE(photo_orientation::isLandscape(Orientation::Portrait));
  TEST_ASSERT_TRUE(photo_orientation::isLandscape(Orientation::RotateCW));
  TEST_ASSERT_TRUE(photo_orientation::isLandscape(Orientation::RotateCCW));
  TEST_ASSERT_EQUAL_INT(1,
                        photo_orientation::panelRotation(Orientation::Portrait));
  TEST_ASSERT_EQUAL_INT(0,
                        photo_orientation::panelRotation(Orientation::RotateCW));
  TEST_ASSERT_EQUAL_INT(
      2, photo_orientation::panelRotation(Orientation::RotateCCW));
  TEST_ASSERT_EQUAL_INT(480,
                        photo_orientation::panelWidth(Orientation::Portrait));
  TEST_ASSERT_EQUAL_INT(800,
                        photo_orientation::panelHeight(Orientation::Portrait));
  TEST_ASSERT_EQUAL_INT(800,
                        photo_orientation::panelWidth(Orientation::RotateCW));
  TEST_ASSERT_EQUAL_INT(480,
                        photo_orientation::panelHeight(Orientation::RotateCCW));
}

void test_screen_capture_png_layout_sizes_streamed_payload() {
  const screen_capture_png::Layout small =
      screen_capture_png::layout(5, 2);
  TEST_ASSERT_EQUAL_UINT32(6, small.rowBytes);
  TEST_ASSERT_EQUAL_UINT32(11, small.deflateBlockBytes);
  TEST_ASSERT_EQUAL_UINT32(28, small.idatDataBytes);
  TEST_ASSERT_EQUAL_UINT32(865, small.fileSize);

  const screen_capture_png::Layout e1003 =
      screen_capture_png::layout(1872, 1404);
  TEST_ASSERT_EQUAL_UINT32(1873, e1003.rowBytes);
  TEST_ASSERT_EQUAL_UINT32(1878, e1003.deflateBlockBytes);
  TEST_ASSERT_EQUAL_UINT32(2636718, e1003.idatDataBytes);
  TEST_ASSERT_EQUAL_UINT32(2637555, e1003.fileSize);
}

void test_screen_capture_crc32_matches_standard_vector_incrementally() {
  const uint8_t first[] = {'1', '2', '3', '4'};
  const uint8_t second[] = {'5', '6', '7', '8', '9'};
  uint32_t crc = usb_screen_capture::updateCrc32(
      0xFFFFFFFFUL, first, sizeof(first));
  crc = usb_screen_capture::updateCrc32(crc, second, sizeof(second));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, crc ^ 0xFFFFFFFFUL);
}

void test_png_chunk_crc32_and_adler32_match_standard_vectors() {
  const uint8_t first[] = {'1', '2', '3', '4'};
  const uint8_t second[] = {'5', '6', '7', '8', '9'};
  uint32_t crc =
      screen_capture_png::updateCrc32(0xFFFFFFFFUL, first, sizeof(first));
  crc = screen_capture_png::updateCrc32(crc, second, sizeof(second));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, crc ^ 0xFFFFFFFFUL);

  uint32_t adler = screen_capture_png::updateAdler32(1, first, sizeof(first));
  adler = screen_capture_png::updateAdler32(adler, second, sizeof(second));
  TEST_ASSERT_EQUAL_HEX32(0x091E01DEUL, adler);
}

void test_e1005_capture_palette_preserves_gray4_and_monochrome() {
  TEST_ASSERT_EQUAL_UINT8(0, screen_capture_png::e1005PaletteGray(0, 1));
  TEST_ASSERT_EQUAL_UINT8(255, screen_capture_png::e1005PaletteGray(1, 1));
  TEST_ASSERT_EQUAL_UINT8(0, screen_capture_png::e1005PaletteGray(0, 4));
  TEST_ASSERT_EQUAL_UINT8(85, screen_capture_png::e1005PaletteGray(1, 4));
  TEST_ASSERT_EQUAL_UINT8(170, screen_capture_png::e1005PaletteGray(2, 4));
  TEST_ASSERT_EQUAL_UINT8(255, screen_capture_png::e1005PaletteGray(3, 4));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_low_battery_warns_when_below_threshold);
  RUN_TEST(test_low_battery_skips_when_disabled);
  RUN_TEST(test_low_battery_skips_when_charger_missing);
  RUN_TEST(test_low_battery_skips_when_charging);
  RUN_TEST(test_low_battery_skips_when_pct_uninitialised);
  RUN_TEST(test_low_battery_skips_at_or_above_threshold);
  RUN_TEST(test_low_battery_custom_threshold);
  RUN_TEST(test_startup_beep_only_for_cold_boot_and_button_wake);
  RUN_TEST(test_quiet_hours_boundaries);
  RUN_TEST(test_sleep_until_same_time_means_next_day);
  RUN_TEST(test_daily_ntp_refresh_boundaries);
  RUN_TEST(test_photo_direction_matches_buttons);
  RUN_TEST(test_failed_photo_advances_after_refresh_only_redraw);
  RUN_TEST(test_photo_index_wraps_in_both_directions);
  RUN_TEST(test_shuffle_is_a_permutation_and_uses_only_valid_indices);
  RUN_TEST(test_shuffle_identity_when_rng_picks_last_index);
  RUN_TEST(test_shuffle_handles_empty_and_singleton_lists);
  RUN_TEST(test_photo_manifest_matches_when_version_is_expected);
  RUN_TEST(test_photo_manifest_reports_stale_when_versions_differ);
  RUN_TEST(test_photo_manifest_rejects_mismatched_schema);
  RUN_TEST(test_photo_manifest_rejects_missing_version_field);
  RUN_TEST(test_photo_manifest_ignores_nested_matching_field_names);
  RUN_TEST(test_photo_geom_native_is_identity);
  RUN_TEST(test_photo_geom_rotate_cw_maps_corners);
  RUN_TEST(test_photo_geom_rotate_ccw_maps_corners);
  RUN_TEST(test_photo_geom_rotate_cw_and_ccw_are_inverses);
  RUN_TEST(test_panel_rotation_is_only_applied_to_e1005);
  RUN_TEST(test_e1005_orientation_geometry_and_rotation);
  RUN_TEST(test_screen_capture_png_layout_sizes_streamed_payload);
  RUN_TEST(test_screen_capture_crc32_matches_standard_vector_incrementally);
  RUN_TEST(test_png_chunk_crc32_and_adler32_match_standard_vectors);
  RUN_TEST(test_e1005_capture_palette_preserves_gray4_and_monochrome);
  return UNITY_END();
}
