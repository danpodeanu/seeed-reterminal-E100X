#include <unity.h>

#include "config_schema.h"
#include "test_fixtures.h"
#include "timezone_list.h"

using namespace config_portal;

void test_validate_bool_accepts_expected_tokens() {
  TEST_ASSERT_TRUE(validateBool("true"));
  TEST_ASSERT_TRUE(validateBool("FALSE"));
  TEST_ASSERT_TRUE(validateBool("1"));
  TEST_ASSERT_TRUE(validateBool("off"));
  TEST_ASSERT_TRUE(validateBool("Yes"));
  TEST_ASSERT_FALSE(validateBool("maybe"));
}

void test_validate_int_is_strict_and_checks_bounds() {
  const Field* f = findField(test_fixtures::kSchema, "count");
  int32_t out = 0;
  TEST_ASSERT_TRUE(validateInt(test_fixtures::kSchema, *f, "7", &out));
  TEST_ASSERT_EQUAL_INT32(7, out);
  TEST_ASSERT_FALSE(validateInt(test_fixtures::kSchema, *f, "", &out));
  TEST_ASSERT_FALSE(validateInt(test_fixtures::kSchema, *f, "7x", &out));
  TEST_ASSERT_FALSE(validateInt(test_fixtures::kSchema, *f, "0", &out));
  TEST_ASSERT_FALSE(validateInt(test_fixtures::kSchema, *f, "11", &out));
}

void test_validate_float_is_strict() {
  const Field* f = findField(test_fixtures::kSchema, "ratio");
  float out = 0.0f;
  TEST_ASSERT_TRUE(validateFloat(*f, "2.25", &out));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.25f, out);
  TEST_ASSERT_FALSE(validateFloat(*f, "", &out));
  TEST_ASSERT_FALSE(validateFloat(*f, "2.5ms", &out));
}

void test_validate_enum_and_find_field() {
  const Field* f = findField(test_fixtures::kSchema, "mode");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(validateEnum(*f, "hourly"));
  TEST_ASSERT_FALSE(validateEnum(*f, "weekly"));
  TEST_ASSERT_NULL(findField(test_fixtures::kSchema, "missing"));
}

void test_validate_field_checks_literal_pattern() {
  String err;
  const Field* f = findField(test_fixtures::kSchema, "name");
  TEST_ASSERT_TRUE(validateField(test_fixtures::kSchema, *f, "xkcd", &err));
  TEST_ASSERT_FALSE(validateField(test_fixtures::kSchema, *f, "weather", &err));
  TEST_ASSERT_TRUE(err.length() > 0);
}

void test_timezone_field_accepts_presets_and_custom_and_rejects_empty() {
  // A curated preset must resolve to its friendly label.
  TEST_ASSERT_NOT_NULL(
      timezoneLabelFor("GMT0BST,M3.5.0/1,M10.5.0/2"));
  TEST_ASSERT_TRUE(timezoneIsPreset("CST-8"));
  TEST_ASSERT_FALSE(timezoneIsPreset("Made/Up_Zone"));
  TEST_ASSERT_NULL(timezoneLabelFor(nullptr));
  TEST_ASSERT_NULL(timezoneLabelFor(""));

  // FieldType::Timezone validates like a bounded String: any non-empty
  // value within [minVal, maxVal]. The portal deliberately does not
  // second-guess tzset() on the exact grammar.
  const Field tzField = {
      "tz", "Timezone", nullptr, FieldType::Timezone,
      "GMT0BST,M3.5.0/1,M10.5.0/2", nullptr, 0, 32, nullptr};
  const Field* fields = &tzField;
  const Section section = {"tz", fields, 1};
  const Schema schema = {"test", &section, 1};
  String err;
  TEST_ASSERT_TRUE(validateField(schema, tzField, "CST-8", &err));
  TEST_ASSERT_TRUE(validateField(schema, tzField,
                                 "GMT0BST,M3.5.0/1,M10.5.0/2", &err));
  // Custom (non-preset) POSIX strings must still validate.
  TEST_ASSERT_TRUE(validateField(schema, tzField, "EAT-3", &err));
  // Empty is rejected (empty timezone would mean "no clue what to set").
  TEST_ASSERT_FALSE(validateField(schema, tzField, "", &err));
  // Overlong strings are rejected against maxVal.
  String longStr;
  for (int i = 0; i < 40; ++i) longStr += "X";
  TEST_ASSERT_FALSE(validateField(schema, tzField, longStr.c_str(), &err));
}
