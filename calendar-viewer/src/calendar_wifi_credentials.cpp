#include "calendar_wifi_credentials.h"

#include <Arduino.h>

#include "config_storage.h"
#include "secrets.h"
#include "wifi_schema.h"

namespace calendar_wifi {
namespace {

String g_ssid;
String g_password;
bool g_nvsEmpty = true;
bool g_loaded = false;
constexpr const char* kPasswordEmptyKey = "pass_empty";

bool isPlaceholder(const String& value) {
  return value == "YOUR_WIFI_NAME";
}

}  // namespace

void load() {
  config_portal::storage::PrefsStorage prefs;
  if (prefs.begin(config_portal::kWifiSchema.nvsNamespace, true)) {
    const String storedSsid = prefs.getString("ssid", "");
    const bool haveStoredPassword = prefs.has("password");
    const String storedPassword = prefs.getString("password", "");
    const bool passwordExplicitlyEmpty =
        prefs.getString(kPasswordEmptyKey, "") == "1";
    prefs.end();
    g_nvsEmpty = storedSsid.isEmpty();
    if (!g_nvsEmpty) {
      g_ssid = storedSsid;
      const bool useCompileTimePassword =
          !haveStoredPassword && !passwordExplicitlyEmpty &&
          storedSsid == WIFI_SSID;
      g_password =
          useCompileTimePassword ? String(WIFI_PASSWORD) : storedPassword;
      g_loaded = true;
      return;
    }
  }
  g_ssid = WIFI_SSID;
  g_password = WIFI_PASSWORD;
  if (isPlaceholder(g_ssid)) {
    g_ssid = "";
    g_password = "";
  }
  g_loaded = true;
}

bool recordPasswordOverride(bool passwordIsEmpty, String& failureReason) {
  failureReason = "";
  config_portal::storage::PrefsStorage prefs;
  if (!prefs.begin(config_portal::kWifiSchema.nvsNamespace, false)) {
    failureReason = "Could not open Wi-Fi credential storage";
    return false;
  }
  bool saved = true;
  if (passwordIsEmpty) {
    saved = prefs.putString(kPasswordEmptyKey, "1");
  } else if (prefs.has(kPasswordEmptyKey)) {
    saved = prefs.remove(kPasswordEmptyKey);
  }
  prefs.end();
  if (!saved) {
    failureReason = "Could not save the Wi-Fi password override";
    return false;
  }
  return true;
}

bool haveCredentials() {
  return g_loaded && !g_ssid.isEmpty() && !isPlaceholder(g_ssid);
}
bool nvsEmpty() { return g_nvsEmpty; }
const char* ssid() { return g_ssid.c_str(); }
const char* password() { return g_password.c_str(); }

}  // namespace calendar_wifi
