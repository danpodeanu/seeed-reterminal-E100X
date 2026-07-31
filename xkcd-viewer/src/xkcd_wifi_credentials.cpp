#include "xkcd_wifi_credentials.h"

#include "config_storage.h"
#include "secrets.h"
#include "wifi_schema.h"

namespace xkcd_wifi {
namespace {

String g_ssid;
String g_password;
bool   g_nvsEmpty = true;
bool   g_loaded = false;

// secrets.h.example placeholder. When someone flashes with an untouched
// example (or reset the portal and reflashed with the example still in
// place), we want the device to launch the config portal immediately
// instead of wasting WIFI_TIMEOUT_MS attempting to associate with an AP
// literally named "YOUR_WIFI_NAME". wifi_sta::connectStation also
// short-circuits this string, but the portal-launch gate in main.cpp is
// haveCredentials(), so the check has to live here too.
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
  // NVS empty (or unavailable) -- fall back to secrets.h defaults.
  g_ssid = WIFI_SSID;
  g_password = WIFI_PASSWORD;
  // secrets.h.example ships with "YOUR_WIFI_NAME" / "YOUR_WIFI_PASSWORD"
  // sentinels. Treat them as "unconfigured" so callers see empty
  // strings: the boot flow launches the config portal (see
  // haveCredentials() below) and the /wifi form pre-fills to blank
  // instead of asking the user to delete the placeholder text.
  if (isPlaceholder(g_ssid)) {
    g_ssid = "";
    g_password = "";
  }
  g_loaded = true;
}

bool haveCredentials() {
  // g_ssid is already scrubbed of the placeholder in load(); the extra
  // isPlaceholder check here is defensive belt-and-braces in case
  // something ever assigns g_ssid directly.
  return g_loaded && g_ssid.length() > 0 && !isPlaceholder(g_ssid);
}
bool nvsEmpty()        { return g_nvsEmpty; }
const char* ssid()     { return g_ssid.c_str(); }
const char* password() { return g_password.c_str(); }

}  // namespace xkcd_wifi
