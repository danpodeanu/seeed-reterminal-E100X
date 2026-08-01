#include "photo_wifi_credentials.h"

#include "config_storage.h"
#include "secrets.h"
#include "wifi_schema.h"

namespace photo_wifi {
namespace {

String g_ssid;
String g_password;
bool   g_nvsEmpty = true;
bool   g_loaded = false;

bool isPlaceholder(const String& ssid) {
  return ssid == "YOUR_WIFI_NAME";
}

}  // namespace

void load() {
  using config_portal::storage::PrefsStorage;
  PrefsStorage prefs;
  if (prefs.begin(config_portal::kWifiSchema.nvsNamespace, /*readOnly=*/true)) {
    const String nvsSsid = prefs.getString("ssid", "");
    const String nvsPassword = prefs.getString("password", "");
    prefs.end();
    g_nvsEmpty = nvsSsid.length() == 0;
    if (!g_nvsEmpty) {
      g_ssid = nvsSsid;
      g_password = nvsPassword;
      g_loaded = true;
      return;
    }
  }
  // NVS empty (or unavailable) - fall back to secrets.h defaults.
  g_ssid = WIFI_SSID;
  g_password = WIFI_PASSWORD;
  if (isPlaceholder(g_ssid)) {
    g_ssid = "";
    g_password = "";
  }
  g_loaded = true;
}

bool haveCredentials() {
  return g_loaded && g_ssid.length() > 0 && !isPlaceholder(g_ssid);
}
bool nvsEmpty()        { return g_nvsEmpty; }
const char* ssid()     { return g_ssid.c_str(); }
const char* password() { return g_password.c_str(); }

}  // namespace photo_wifi
