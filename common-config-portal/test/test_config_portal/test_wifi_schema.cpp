#include <unity.h>

#include <vector>

#include "config_storage.h"
#include "fake_storage.h"
#include "wifi_schema.h"

using namespace config_portal;

void test_wifi_schema_fields_and_defaults() {
  TEST_ASSERT_EQUAL_STRING("wifi", kWifiSchema.nvsNamespace);
  const Field* ssid = findField(kWifiSchema, "ssid");
  const Field* password = findField(kWifiSchema, "password");
  TEST_ASSERT_NOT_NULL(ssid);
  TEST_ASSERT_NOT_NULL(password);
  TEST_ASSERT_EQUAL(static_cast<int>(FieldType::String), static_cast<int>(ssid->type));
  TEST_ASSERT_EQUAL(static_cast<int>(FieldType::Password), static_cast<int>(password->type));
  TEST_ASSERT_EQUAL_STRING("", ssid->defaultVal);
  TEST_ASSERT_EQUAL_STRING("", password->defaultVal);
}

void test_wifi_password_redacts_when_stored() {
  FakeStorage s;
  s.data_["password"] = "hunter2";
  std::vector<std::pair<String, String>> out;
  storage::loadForGet(s, kWifiSchema, out);
  TEST_ASSERT_EQUAL_STRING(kSecretSentinel, out[1].second.c_str());
}
