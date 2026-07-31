#include <unity.h>

void test_validate_bool_accepts_expected_tokens();
void test_validate_int_is_strict_and_checks_bounds();
void test_validate_float_is_strict();
void test_validate_enum_and_find_field();
void test_validate_field_checks_literal_pattern();
void test_load_for_get_defaults_stored_values_and_secret_redaction();
void test_save_skips_unchanged_and_updates_changed();
void test_save_secret_sentinel_preserves_and_empty_clears();
void test_save_rejects_invalid_input_and_sets_error();
void test_typed_getters_use_schema_defaults();
void test_json_round_trip_values();
void test_parse_submission_ignores_unknown_keys();
void test_parse_submission_handles_malformed_json();
void test_wifi_schema_fields_and_defaults();
void test_wifi_password_redacts_when_stored();

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_validate_bool_accepts_expected_tokens);
  RUN_TEST(test_validate_int_is_strict_and_checks_bounds);
  RUN_TEST(test_validate_float_is_strict);
  RUN_TEST(test_validate_enum_and_find_field);
  RUN_TEST(test_validate_field_checks_literal_pattern);
  RUN_TEST(test_load_for_get_defaults_stored_values_and_secret_redaction);
  RUN_TEST(test_save_skips_unchanged_and_updates_changed);
  RUN_TEST(test_save_secret_sentinel_preserves_and_empty_clears);
  RUN_TEST(test_save_rejects_invalid_input_and_sets_error);
  RUN_TEST(test_typed_getters_use_schema_defaults);
  RUN_TEST(test_json_round_trip_values);
  RUN_TEST(test_parse_submission_ignores_unknown_keys);
  RUN_TEST(test_parse_submission_handles_malformed_json);
  RUN_TEST(test_wifi_schema_fields_and_defaults);
  RUN_TEST(test_wifi_password_redacts_when_stored);
  return UNITY_END();
}
