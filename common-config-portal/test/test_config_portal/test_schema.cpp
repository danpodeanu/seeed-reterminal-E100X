#include <unity.h>

#include "config_schema.h"
#include "test_fixtures.h"

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
