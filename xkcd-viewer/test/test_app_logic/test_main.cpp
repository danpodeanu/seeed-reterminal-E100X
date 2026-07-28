#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "app_logic.h"
#include "text_render_pure.h"
#include "xkcd_cache_schema.h"
#include "xkcd_index_pure.h"
#include "xkcd_manifest_serialize.h"

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

void test_refresh_baseline_is_repaired_before_due_check() {
  constexpr int64_t now = 100000;
  constexpr int64_t previous = 90000;

  TEST_ASSERT_EQUAL_INT64(
      previous,
      app_logic::normalizeRefreshBaseline(false, true, now, previous));
  TEST_ASSERT_EQUAL_INT64(
      now, app_logic::normalizeRefreshBaseline(true, true, now, previous));
  TEST_ASSERT_EQUAL_INT64(
      now, app_logic::normalizeRefreshBaseline(false, true, now, 0));
  TEST_ASSERT_EQUAL_INT64(
      now,
      app_logic::normalizeRefreshBaseline(false, true, now, now + 1));
  TEST_ASSERT_EQUAL_INT64(
      0, app_logic::normalizeRefreshBaseline(false, false, now, 0));
  TEST_ASSERT_FALSE(
      app_logic::refreshDue(false, true, now, now, 6 * 60 * 60));
}

void test_published_comic_count_excludes_missing_404() {
  TEST_ASSERT_EQUAL_UINT32(0, app_logic::publishedComicCount(0));
  TEST_ASSERT_EQUAL_UINT32(404, app_logic::publishedComicCount(404));
  TEST_ASSERT_EQUAL_UINT32(404, app_logic::publishedComicCount(405));
  TEST_ASSERT_EQUAL_UINT32(3274, app_logic::publishedComicCount(3275));
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

void test_live_recovery_only_when_sd_present_and_offline_and_empty() {
  // The one case we WANT to trigger the live retry: SD is there, we
  // failed to get a comic locally, and the radio isn't already on.
  TEST_ASSERT_TRUE(app_logic::liveRecoveryAllowed(true, false, false));

  // Already got a comic -- nothing to recover.
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(true, true, false));
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(true, true, true));

  // Wi-Fi is already up: the initial acquireComic already had network
  // access, so retrying with the same network wouldn't help.
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(true, false, true));

  // No SD card: acquireComicWithoutSd owns the network path; we don't
  // want to re-enter Wi-Fi bring-up here.
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(false, false, false));
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(false, false, true));
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(false, true, false));
  TEST_ASSERT_FALSE(app_logic::liveRecoveryAllowed(false, true, true));
}

void test_deadline_comparison_survives_millis_wrap() {
  TEST_ASSERT_FALSE(app_logic::deadlineReached(100, 200));
  TEST_ASSERT_TRUE(app_logic::deadlineReached(200, 200));
  TEST_ASSERT_FALSE(app_logic::deadlineReached(100, 0));
  TEST_ASSERT_TRUE(app_logic::deadlineReached(0x10U, 0xFFFFFFF0U));
}

void test_pre_sync_quiet_suppression_lets_maintenance_run() {
  // Normal quiet-hour timer wake with no maintenance due: suppress.
  TEST_ASSERT_TRUE(app_logic::suppressPreSyncForQuietHours(
      false, false, false, true, true, false));
  // Same wake, but archive maintenance is due: do NOT suppress; the caller
  // should proceed so silent maintenance can happen.
  TEST_ASSERT_FALSE(app_logic::suppressPreSyncForQuietHours(
      false, false, false, true, true, true));
  // Cold boot always wins over quiet hours.
  TEST_ASSERT_FALSE(app_logic::suppressPreSyncForQuietHours(
      true, false, false, true, true, false));
  // NTP-refresh wakes always proceed.
  TEST_ASSERT_FALSE(app_logic::suppressPreSyncForQuietHours(
      false, true, false, true, true, false));
  // Button wakes always proceed.
  TEST_ASSERT_FALSE(app_logic::suppressPreSyncForQuietHours(
      false, false, true, true, true, false));
  // Invalid clock: quiet-hour check cannot be trusted; proceed and let NTP
  // path re-evaluate.
  TEST_ASSERT_FALSE(app_logic::suppressPreSyncForQuietHours(
      false, false, false, false, true, false));
  // Outside quiet hours: never suppress.
  TEST_ASSERT_FALSE(app_logic::suppressPreSyncForQuietHours(
      false, false, false, true, false, false));
}

void test_post_sync_quiet_suppression_matches_pre_sync_minus_ntp() {
  TEST_ASSERT_TRUE(app_logic::suppressPostSyncForQuietHours(
      false, false, true, true, false));
  // Archive maintenance overrides quiet suppression here too.
  TEST_ASSERT_FALSE(app_logic::suppressPostSyncForQuietHours(
      false, false, true, true, true));
  // Cold boot always wins.
  TEST_ASSERT_FALSE(app_logic::suppressPostSyncForQuietHours(
      true, false, true, true, false));
  // Button wakes always proceed.
  TEST_ASSERT_FALSE(app_logic::suppressPostSyncForQuietHours(
      false, true, true, true, false));
  // Outside quiet hours: never suppress.
  TEST_ASSERT_FALSE(app_logic::suppressPostSyncForQuietHours(
      false, false, true, false, false));
  // Invalid clock (unlikely at this point but handled): do not suppress.
  TEST_ASSERT_FALSE(app_logic::suppressPostSyncForQuietHours(
      false, false, false, true, false));
}

void test_silent_maintenance_flag_only_when_quiet_and_archive_due() {
  TEST_ASSERT_TRUE(app_logic::maintainSilentlyInQuietHours(true, true));
  TEST_ASSERT_FALSE(app_logic::maintainSilentlyInQuietHours(false, true));
  TEST_ASSERT_FALSE(app_logic::maintainSilentlyInQuietHours(true, false));
  TEST_ASSERT_FALSE(app_logic::maintainSilentlyInQuietHours(false, false));
}

void test_parse_unsigned_digits_accepts_and_rejects_correctly() {
  uint32_t value = 0;
  TEST_ASSERT_TRUE(xkcd_index::parseUnsignedDigits("1", 1, value, false));
  TEST_ASSERT_EQUAL_UINT32(1u, value);
  TEST_ASSERT_TRUE(
      xkcd_index::parseUnsignedDigits("100000", 6, value, false));
  TEST_ASSERT_EQUAL_UINT32(100000u, value);

  // 0 is only accepted when allowZero=true (used for the count-line
  // prefix, not for actual entries).
  TEST_ASSERT_FALSE(xkcd_index::parseUnsignedDigits("0", 1, value, false));
  TEST_ASSERT_TRUE(xkcd_index::parseUnsignedDigits("0", 1, value, true));
  TEST_ASSERT_EQUAL_UINT32(0u, value);

  // Non-digit, empty, null, over-length and >100000 rejected.
  TEST_ASSERT_FALSE(xkcd_index::parseUnsignedDigits(nullptr, 3, value, true));
  TEST_ASSERT_FALSE(xkcd_index::parseUnsignedDigits("12", 0, value, true));
  TEST_ASSERT_FALSE(xkcd_index::parseUnsignedDigits("1a", 2, value, true));
  TEST_ASSERT_FALSE(xkcd_index::parseUnsignedDigits("-1", 2, value, true));
  TEST_ASSERT_FALSE(
      xkcd_index::parseUnsignedDigits("12345678901", 11, value, true));
  TEST_ASSERT_FALSE(
      xkcd_index::parseUnsignedDigits("100001", 6, value, true));

  // The helper does not trim -- callers are expected to trim first.
  TEST_ASSERT_FALSE(xkcd_index::parseUnsignedDigits(" 1", 2, value, true));
}

void test_pack_4bpp_in_place_packs_two_pixels_per_byte() {
  // 4x2 image, values 0..7. Row 0: 1,2,3,4 -> 0x12, 0x34.
  //                       Row 1: 5,6,7,8 -> 0x56, 0x78.
  uint8_t buffer[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  xkcd_index::pack4bppInPlace(buffer, 4, 2);
  TEST_ASSERT_EQUAL_UINT8(0x12, buffer[0]);
  TEST_ASSERT_EQUAL_UINT8(0x34, buffer[1]);
  TEST_ASSERT_EQUAL_UINT8(0x56, buffer[2]);
  TEST_ASSERT_EQUAL_UINT8(0x78, buffer[3]);
}

void test_pack_4bpp_in_place_masks_high_nibble() {
  // Values > 0x0F must be masked to their low nibble (indices should
  // already be 0..15, but the helper defensively clamps).
  uint8_t buffer[2] = {0xAB, 0xCD};
  xkcd_index::pack4bppInPlace(buffer, 2, 1);
  TEST_ASSERT_EQUAL_UINT8(0xBD, buffer[0]);
}

void test_cache_schema_wrap_injects_tag_as_first_key() {
  const std::string in = R"({"num":42,"img":"https://example/x.png"})";
  const std::string out = xkcd_cache::wrapWithSchema(in);
  TEST_ASSERT_EQUAL_STRING(
      "{\"_schema\":\"xkcd-comic-v1\",\"num\":42,\"img\":\"https://example/x.png\"}",
      out.c_str());
}

void test_cache_schema_wrap_handles_empty_object() {
  const std::string out = xkcd_cache::wrapWithSchema("{}");
  TEST_ASSERT_EQUAL_STRING("{\"_schema\":\"xkcd-comic-v1\"}", out.c_str());
}

void test_cache_schema_wrap_preserves_leading_whitespace() {
  const std::string out = xkcd_cache::wrapWithSchema("  {\"n\":1}");
  TEST_ASSERT_EQUAL_STRING("  {\"_schema\":\"xkcd-comic-v1\",\"n\":1}",
                           out.c_str());
}

void test_cache_schema_wrap_passthrough_when_not_object() {
  // Non-object payloads (arrays, partial downloads, empty string) must be
  // returned unchanged so we never prepend a tag to something we don't
  // understand.
  TEST_ASSERT_EQUAL_STRING("", xkcd_cache::wrapWithSchema("").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "[1,2,3]", xkcd_cache::wrapWithSchema("[1,2,3]").c_str());
  TEST_ASSERT_EQUAL_STRING(
      "not json", xkcd_cache::wrapWithSchema("not json").c_str());
}

// Regression test for the JSONL manifest loader (see xkcd_index.cpp
// commit 871e10f). ArduinoJson v7 stores parsed string values in the
// JsonDocument's internal StringPool, NOT in the input buffer -- see
// ArduinoJson/Memory/StringBuilder.hpp, which memcpy's each character
// into a StringNode owned by the doc's ResourceManager. Reusing a
// single JsonDocument across JSONL lines and saving `.as<const char*>()`
// pointers would dangle every prior pointer as soon as `.clear()` fires
// on the next line, because clear() releases the pool.
//
// The manifest loader avoids this by memcpy'ing every string into a
// separate arena immediately after each parse. This test exercises
// that pattern end-to-end: parse several lines with one reused doc,
// snapshot strings into an arena, force many .clear() cycles, then
// verify the snapshots still read as originally parsed.
void test_reused_json_doc_dangles_pool_but_arena_copy_survives() {
  const char* kLines[] = {
      "{\"n\":1,\"t\":\"Barrel - Part 1\",\"a\":\"Don't we all.\","
      "\"e\":\".png\",\"u\":\"https://imgs.xkcd.com/comics/barrel_cropped_(1).jpg\"}",
      "{\"n\":42,\"t\":\"Geico\",\"a\":\"An alt with a \\\"quote\\\".\","
      "\"e\":\".png\",\"u\":\"https://imgs.xkcd.com/comics/geico.png\"}",
      "{\"n\":3276,\"t\":\"Latest\",\"a\":\"Alt \\u00e9 text.\","
      "\"e\":\".jpg\",\"u\":\"https://imgs.xkcd.com/comics/latest.jpg\"}",
  };
  const size_t kLineCount = sizeof(kLines) / sizeof(kLines[0]);
  const int kExpectedNumbers[kLineCount] = {1, 42, 3276};
  const char* kExpectedTitles[kLineCount] = {
      "Barrel - Part 1", "Geico", "Latest"};
  const char* kExpectedAlts[kLineCount] = {
      "Don't we all.", "An alt with a \"quote\".", "Alt \xc3\xa9 text."};
  const char* kExpectedExts[kLineCount] = {".png", ".png", ".jpg"};

  // Simulate the arena: one contiguous buffer, bump-allocated.
  std::vector<char> arena(4096, 0);
  size_t arenaPos = 0;
  auto arenaCopy = [&](const char* s) -> const char* {
    if (s == nullptr) return "";
    const size_t len = strlen(s) + 1;
    TEST_ASSERT_TRUE_MESSAGE(arenaPos + len <= arena.size(),
                             "test arena too small");
    char* dst = arena.data() + arenaPos;
    memcpy(dst, s, len);
    arenaPos += len;
    return dst;
  };

  struct StoredMeta {
    int number;
    const char* title;
    const char* alt;
    const char* extension;
    const char* url;
  };
  std::vector<StoredMeta> stored;

  // The critical bit: one JsonDocument reused across every line, with
  // .clear() between them -- exactly the pattern in xkcd_index.cpp.
  JsonDocument doc;
  for (size_t i = 0; i < kLineCount; ++i) {
    doc.clear();
    // Copy line into a mutable buffer so we exercise the same
    // deserializeJson(doc, char*) overload the production loader uses.
    std::vector<char> lineBuf(kLines[i], kLines[i] + strlen(kLines[i]) + 1);
    const DeserializationError err = deserializeJson(doc, lineBuf.data());
    TEST_ASSERT_TRUE_MESSAGE(!err, err.c_str());
    StoredMeta meta;
    meta.number = doc["n"].as<int>();
    meta.title = arenaCopy(doc["t"].as<const char*>());
    meta.alt = arenaCopy(doc["a"].as<const char*>());
    meta.extension = arenaCopy(doc["e"].as<const char*>());
    meta.url = arenaCopy(doc["u"].as<const char*>());
    stored.push_back(meta);
  }
  // Drop the doc's last-parse pool so any surviving direct-into-pool
  // pointers would be dangling. Then parse a bunch of throwaway blobs
  // so the released pool bytes are actively reused -- this way a
  // regression that skipped arenaCopy() would deterministically read
  // scrambled memory and fail the assertions below, rather than being
  // saved by "the freed memory happens to still contain the string".
  doc.clear();
  for (int i = 0; i < 128; ++i) {
    doc.clear();
    char junk[96];
    snprintf(junk, sizeof(junk),
             "{\"j\":\"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\",\"k\":%d}", i);
    (void)deserializeJson(doc, junk);
  }
  doc.clear();

  for (size_t i = 0; i < kLineCount; ++i) {
    TEST_ASSERT_EQUAL_INT(kExpectedNumbers[i], stored[i].number);
    TEST_ASSERT_EQUAL_STRING(kExpectedTitles[i], stored[i].title);
    TEST_ASSERT_EQUAL_STRING(kExpectedAlts[i], stored[i].alt);
    TEST_ASSERT_EQUAL_STRING(kExpectedExts[i], stored[i].extension);
  }

  // Belt-and-braces: no stored extension should be empty, since that
  // was the surface symptom of the original bug (SD.exists() missed
  // 100% of the time because the built path had no ".png"/".jpg" tail).
  for (size_t i = 0; i < kLineCount; ++i) {
    TEST_ASSERT_TRUE(stored[i].extension[0] != '\0');
  }
}

void test_display_text_preserves_utf8_and_normalizes() {
  // "Café — piñata" round-trips (é, em-dash, ñ are all multi-byte).
  // Note the "" boundaries: C++ hex escapes are greedy so \xB1a would
  // otherwise be interpreted as one 3-digit hex value.
  const std::string in =
      "  Caf\xC3\xA9""  \xE2\x80\x94""   pi\xC3\xB1""ata   ";
  const std::string out = text_render::pure::displayText(in);
  TEST_ASSERT_EQUAL_STRING(
      "Caf\xC3\xA9"" \xE2\x80\x94"" pi\xC3\xB1""ata", out.c_str());
}

void test_display_text_strips_control_bytes() {
  // C0 controls (excluding whitespace) and DEL are removed silently;
  // tab/newline/CR collapse to a single space between words.
  const std::string in =
      "line\x01\x02""one\ttwo\r\nthree\x7F""done";
  const std::string out = text_render::pure::displayText(in);
  TEST_ASSERT_EQUAL_STRING("lineone two threedone", out.c_str());
}

void test_display_text_html_unescape() {
  const std::string in =
      "Ren\xC3\xA9"" &amp; C&#39;t &quot;quoted&quot; &lt;tag&gt;";
  const std::string out = text_render::pure::displayText(in);
  TEST_ASSERT_EQUAL_STRING(
      "Ren\xC3\xA9"" & C't \"quoted\" <tag>", out.c_str());
}

void test_display_text_rejects_malformed_utf8() {
  // 0xC3 is a valid 2-byte lead but must be followed by a continuation
  // byte (0x80-0xBF). Here it is followed by 'x' (0x78), so the lead
  // byte must be dropped rather than emitted verbatim and 'x' must
  // survive. Similarly the isolated 0xFF is dropped.
  const std::string in = "bad\xC3""xnext\xFF""end";
  const std::string out = text_render::pure::displayText(in);
  TEST_ASSERT_EQUAL_STRING("badxnextend", out.c_str());
}

void test_display_text_drops_4byte_utf8_sequences() {
  // Seeed_GFX's decodeUTF8 doesn't decode 4-byte (BMP-out) codepoints.
  // xkcd #2912's alt is entirely U+1D400-U+1D7FF (math script letters),
  // encoded as 4-byte UTF-8. Passing them through would render as tofu,
  // so displayText must strip them. Surrounding ASCII/2/3-byte chars must
  // survive.
  // U+1D4D8 (MATH SCRIPT CAPITAL I) = F0 9D 93 98
  // U+1D4EE (MATH SCRIPT SMALL S)   = F0 9D 93 AE
  // U+00E9  (LATIN SMALL LETTER E WITH ACUTE) = C3 A9 -- must survive
  const std::string in =
      "hi \xF0\x9D\x93\x98\xF0\x9D\x93\xAE caf\xC3\xA9";
  const std::string out = text_render::pure::displayText(in);
  // The two 4-byte sequences vanish; leading/inner spaces around the
  // dropped range collapse to a single space (nice UX).
  TEST_ASSERT_EQUAL_STRING("hi caf\xC3\xA9", out.c_str());
}

// -- xkcd_manifest JSONL round-trip ------------------------------------------
// Model what persist() emits when addComic() inserts a new entry into
// an existing cache. The production writer streams one line per record;
// the tests reconstruct the same stream via the pure builders and check
// the shape of the manifest.

namespace {
std::vector<std::string> splitLines(const std::string& blob) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i < blob.size(); ++i) {
    if (blob[i] == '\n') {
      out.emplace_back(blob.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < blob.size()) out.emplace_back(blob.substr(start));
  return out;
}
}  // namespace

void test_manifest_header_omits_latest_when_zero_and_includes_skips() {
  const std::string line = xkcd_manifest::buildJsonlHeader(5, 0, {404, 1608});
  TEST_ASSERT_TRUE(line.size() > 0);
  TEST_ASSERT_EQUAL_CHAR('\n', line.back());
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_EQUAL_UINT32(5U, doc["v"].as<uint32_t>());
  TEST_ASSERT_TRUE(doc["l"].isNull());
  JsonArray skips = doc["s"].as<JsonArray>();
  TEST_ASSERT_EQUAL(2, static_cast<int>(skips.size()));
  TEST_ASSERT_EQUAL(404, skips[0].as<int>());
  TEST_ASSERT_EQUAL(1608, skips[1].as<int>());
}

void test_manifest_header_emits_latest_when_positive() {
  const std::string line = xkcd_manifest::buildJsonlHeader(5, 3276, {});
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_EQUAL(3276, doc["l"].as<int>());
  TEST_ASSERT_EQUAL(0, static_cast<int>(doc["s"].as<JsonArray>().size()));
}

void test_manifest_comic_preserves_all_fields_and_terminates_with_newline() {
  const std::string line = xkcd_manifest::buildJsonlComic(
      2493, "Consequences", "Two-by-fours are not that heavy.", ".png",
      "https://imgs.xkcd.com/comics/consequences.png");
  TEST_ASSERT_EQUAL_CHAR('\n', line.back());
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_EQUAL(2493, doc["n"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Consequences", doc["t"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Two-by-fours are not that heavy.",
                           doc["a"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(".png", doc["e"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("https://imgs.xkcd.com/comics/consequences.png",
                           doc["u"].as<const char*>());
}

void test_manifest_comic_serializes_null_field_pointers_as_empty_strings() {
  // StoredMeta fields are const char* and can legitimately point at a
  // shared empty-string constant. Passing raw nullptr should also work
  // so the persist path stays defensive against future churn.
  const std::string line =
      xkcd_manifest::buildJsonlComic(42, nullptr, nullptr, nullptr, nullptr);
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_EQUAL(42, doc["n"].as<int>());
  TEST_ASSERT_EQUAL_STRING("", doc["t"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("", doc["a"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("", doc["e"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("", doc["u"].as<const char*>());
}

void test_manifest_comic_escapes_special_json_characters() {
  // Titles occasionally contain quotes and backslashes ("Star Wars \"Ep IV\"").
  // The serializer must escape them so the resulting line is still valid JSON.
  const std::string line = xkcd_manifest::buildJsonlComic(
      1, "back\\slash \"quote\"", "line\nfeed", ".png", "http://x/y");
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_EQUAL_STRING("back\\slash \"quote\"",
                           doc["t"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("line\nfeed", doc["a"].as<const char*>());
}

void test_manifest_comic_emits_publication_date_when_provided() {
  const std::string line = xkcd_manifest::buildJsonlComic(
      2493, "Consequences", "alt", ".png",
      "https://imgs.xkcd.com/comics/consequences.png", 2021, 6, 25);
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_EQUAL(2021, doc["y"].as<int>());
  TEST_ASSERT_EQUAL(6, doc["m"].as<int>());
  TEST_ASSERT_EQUAL(25, doc["d"].as<int>());
}

void test_manifest_comic_omits_date_when_any_component_is_zero() {
  // year=0 alone is enough to suppress the whole triplet -- the
  // firmware treats "unknown" as an atomic property.
  const std::string line = xkcd_manifest::buildJsonlComic(
      1, "Barrel", "alt", ".jpg", "http://x/y", 0, 6, 25);
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(doc, line.c_str(), line.size() - 1).code());
  TEST_ASSERT_FALSE(doc["y"].is<int>());
  TEST_ASSERT_FALSE(doc["m"].is<int>());
  TEST_ASSERT_FALSE(doc["d"].is<int>());
}

void test_manifest_append_new_comic_produces_expected_jsonl_stream() {
  // Simulate what persist() writes when addComic() has just recorded a
  // freshly downloaded comic on top of a warmed-up cache. Concatenating
  // the header + each comic line reproduces the on-disk format that
  // load() will consume on the next boot.
  std::string blob;
  blob += xkcd_manifest::buildJsonlHeader(5, 4, {404});
  blob += xkcd_manifest::buildJsonlComic(1, "Barrel - Part 1", "Don't we all.",
                                         ".jpg", "https://x/1.jpg");
  blob += xkcd_manifest::buildJsonlComic(2, "Petit Trees", "'Tis a beautiful.",
                                         ".jpg", "https://x/2.jpg");
  blob += xkcd_manifest::buildJsonlComic(3, "Island", "Watch out.", ".jpg",
                                         "https://x/3.jpg");
  // The "new" line -- what addComic(4) + persist() appends on the wire.
  blob += xkcd_manifest::buildJsonlComic(4, "Landscape", "Mountains ahoy.",
                                         ".png", "https://x/4.png");

  const std::vector<std::string> lines = splitLines(blob);
  TEST_ASSERT_EQUAL(5, static_cast<int>(lines.size()));
  // Header must sit on line 0 and carry the new latest=4.
  JsonDocument header;
  TEST_ASSERT_EQUAL(DeserializationError::Ok,
                    deserializeJson(header, lines[0]).code());
  TEST_ASSERT_EQUAL(5, header["v"].as<int>());
  TEST_ASSERT_EQUAL(4, header["l"].as<int>());
  TEST_ASSERT_EQUAL(1, static_cast<int>(header["s"].as<JsonArray>().size()));
  // Comics 1..4 must appear in order, and the last one is the freshly
  // downloaded #4 with the .png extension.
  for (int i = 0; i < 4; ++i) {
    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok,
                      deserializeJson(doc, lines[i + 1]).code());
    TEST_ASSERT_EQUAL(i + 1, doc["n"].as<int>());
  }
  JsonDocument tail;
  deserializeJson(tail, lines[4]);
  TEST_ASSERT_EQUAL_STRING(".png", tail["e"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("Landscape", tail["t"].as<const char*>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_startup_beep_only_for_cold_boot_and_button_wake);
  RUN_TEST(test_display_text_preserves_utf8_and_normalizes);
  RUN_TEST(test_display_text_strips_control_bytes);
  RUN_TEST(test_display_text_html_unescape);
  RUN_TEST(test_display_text_rejects_malformed_utf8);
  RUN_TEST(test_display_text_drops_4byte_utf8_sequences);
  RUN_TEST(test_quiet_hours_boundaries);
  RUN_TEST(test_quiet_hours_can_wrap_midnight);
  RUN_TEST(test_refresh_due_handles_boundaries_and_clock_rollback);
  RUN_TEST(test_refresh_baseline_is_repaired_before_due_check);
  RUN_TEST(test_published_comic_count_excludes_missing_404);
  RUN_TEST(test_cache_only_threshold_controls_network);
  RUN_TEST(test_archive_maintenance_requires_timer_and_sd);
  RUN_TEST(test_live_recovery_only_when_sd_present_and_offline_and_empty);
  RUN_TEST(test_deadline_comparison_survives_millis_wrap);
  RUN_TEST(test_pre_sync_quiet_suppression_lets_maintenance_run);
  RUN_TEST(test_post_sync_quiet_suppression_matches_pre_sync_minus_ntp);
  RUN_TEST(test_silent_maintenance_flag_only_when_quiet_and_archive_due);
  RUN_TEST(test_parse_unsigned_digits_accepts_and_rejects_correctly);
  RUN_TEST(test_pack_4bpp_in_place_packs_two_pixels_per_byte);
  RUN_TEST(test_pack_4bpp_in_place_masks_high_nibble);
  RUN_TEST(test_cache_schema_wrap_injects_tag_as_first_key);
  RUN_TEST(test_cache_schema_wrap_handles_empty_object);
  RUN_TEST(test_cache_schema_wrap_preserves_leading_whitespace);
  RUN_TEST(test_cache_schema_wrap_passthrough_when_not_object);
  RUN_TEST(test_reused_json_doc_dangles_pool_but_arena_copy_survives);
  RUN_TEST(test_manifest_header_omits_latest_when_zero_and_includes_skips);
  RUN_TEST(test_manifest_header_emits_latest_when_positive);
  RUN_TEST(test_manifest_comic_preserves_all_fields_and_terminates_with_newline);
  RUN_TEST(test_manifest_comic_serializes_null_field_pointers_as_empty_strings);
  RUN_TEST(test_manifest_comic_escapes_special_json_characters);
  RUN_TEST(test_manifest_comic_emits_publication_date_when_provided);
  RUN_TEST(test_manifest_comic_omits_date_when_any_component_is_zero);
  RUN_TEST(test_manifest_append_new_comic_produces_expected_jsonl_stream);
  return UNITY_END();
}
