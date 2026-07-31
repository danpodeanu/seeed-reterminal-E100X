#include <unity.h>

#include <ArduinoJson.h>
#include <map>
#include <vector>

#include "config_json.h"
#include "test_fixtures.h"

using namespace config_portal;

void test_json_round_trip_values() {
  std::vector<std::pair<String, String>> vals{{"name", "xkcd custom"}, {"count", "7"}};
  String body = json::valuesToJson(vals);
  JsonDocument doc;
  TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, body.c_str()).code());
  TEST_ASSERT_TRUE(doc["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("7", doc["values"]["count"].as<const char*>());
}

void test_parse_submission_ignores_unknown_keys() {
  std::map<String, String> out;
  String err;
  TEST_ASSERT_TRUE(json::parseSubmissionJson(test_fixtures::kSchema,
                                             "{\"count\":7,\"unknown\":\"x\"}", out, &err));
  TEST_ASSERT_EQUAL(1, static_cast<int>(out.size()));
  TEST_ASSERT_EQUAL_STRING("7", out["count"].c_str());
}

void test_parse_submission_handles_malformed_json() {
  std::map<String, String> out;
  String err;
  TEST_ASSERT_FALSE(json::parseSubmissionJson(test_fixtures::kSchema, "{", out, &err));
  TEST_ASSERT_TRUE(err.length() > 0);
}
