#include <unity.h>

#include <map>
#include <vector>

#include "config_storage.h"
#include "fake_storage.h"
#include "test_fixtures.h"

using namespace config_portal;

void test_load_for_get_defaults_stored_values_and_secret_redaction() {
  FakeStorage s;
  std::vector<std::pair<String, String>> out;
  storage::loadForGet(s, test_fixtures::kSchema, out);
  TEST_ASSERT_EQUAL_STRING("true", out[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("xkcd", out[3].second.c_str());

  s.data_["name"] = "xkcd custom";
  s.data_["api_key"] = "secret";
  storage::loadForGet(s, test_fixtures::kSchema, out);
  TEST_ASSERT_EQUAL_STRING("xkcd custom", out[3].second.c_str());
  TEST_ASSERT_EQUAL_STRING(kSecretSentinel, out[5].second.c_str());

  s.data_["api_key"] = "";
  storage::loadForGet(s, test_fixtures::kSchema, out);
  TEST_ASSERT_EQUAL_STRING("", out[5].second.c_str());
}

void test_save_writes_every_submitted_field() {
  // Save persists every submitted non-secret field, even when the value
  // matches what NVS already holds -- so a user actively reviewing
  // and confirming a form pins the value in NVS and future firmware
  // default changes cannot silently override it.
  FakeStorage s;
  s.data_["count"] = "5";
  std::map<String, String> form{{"count", "5"}, {"mode", "hourly"}};
  String err;
  TEST_ASSERT_TRUE(storage::save(s, test_fixtures::kSchema, form, &err));
  TEST_ASSERT_EQUAL(2, s.putCount);
  TEST_ASSERT_EQUAL_STRING("5", s.data_["count"].c_str());
  TEST_ASSERT_EQUAL_STRING("hourly", s.data_["mode"].c_str());
}

void test_save_secret_sentinel_preserves_and_empty_clears() {
  FakeStorage s;
  s.data_["api_key"] = "old";
  String err;
  TEST_ASSERT_TRUE(storage::save(s, test_fixtures::kSchema,
                                 std::map<String, String>{{"api_key", kSecretSentinel}}, &err));
  TEST_ASSERT_EQUAL_STRING("old", s.data_["api_key"].c_str());

  TEST_ASSERT_TRUE(storage::save(s, test_fixtures::kSchema,
                                 std::map<String, String>{{"api_key", ""}}, &err));
  TEST_ASSERT_FALSE(s.has("api_key"));
  TEST_ASSERT_EQUAL(1, s.removeCount);
}

void test_save_rejects_invalid_input_and_sets_error() {
  FakeStorage s;
  String err;
  TEST_ASSERT_FALSE(storage::save(s, test_fixtures::kSchema,
                                  std::map<String, String>{{"count", "bad"}}, &err));
  TEST_ASSERT_TRUE(err.length() > 0);
}

void test_typed_getters_use_schema_defaults() {
  FakeStorage s;
  TEST_ASSERT_EQUAL_STRING("xkcd", storage::getString(s, test_fixtures::kSchema, "name").c_str());
  TEST_ASSERT_EQUAL_INT32(5, storage::getInt(s, test_fixtures::kSchema, "count"));
  TEST_ASSERT_TRUE(storage::getBool(s, test_fixtures::kSchema, "enabled"));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, storage::getFloat(s, test_fixtures::kSchema, "ratio"));
}
