#include "portal_ap_password.h"

namespace config_portal {

// Lowercase letters + digits with the classic confusables removed
// (0/O, 1/l/I). 31 characters. A slight non-uniform bias from
// `rng() % 31` is negligible for password generation and much simpler
// than rejection sampling.
const char kEasyPasswordAlphabet[] =
    "abcdefghjkmnpqrstuvwxyz23456789";

size_t easyPasswordAlphabetSize() {
  return sizeof(kEasyPasswordAlphabet) - 1;
}

String randomEasyPassword(size_t len, RngFn rng) {
  if (!rng || len == 0 || len > 63) return String();
  const size_t n = easyPasswordAlphabetSize();
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    const uint32_t r = rng();
    out += kEasyPasswordAlphabet[r % n];
  }
  return out;
}

}  // namespace config_portal

#ifdef ARDUINO
#include <Preferences.h>
#include <esp_system.h>

#include "app_logger.h"

namespace config_portal {
namespace {

constexpr const char* kNamespace = "portal_ap";
constexpr const char* kPassKey   = "pass";

uint32_t hardwareRng() { return esp_random(); }

bool isFromAlphabet(const String& s) {
  for (size_t i = 0; i < s.length(); ++i) {
    bool ok = false;
    for (size_t j = 0; j < easyPasswordAlphabetSize(); ++j) {
      if (s[i] == kEasyPasswordAlphabet[j]) { ok = true; break; }
    }
    if (!ok) return false;
  }
  return true;
}

}  // namespace

String ensureApPassword(size_t len) {
  if (len < 8) len = 8;   // WPA2-PSK minimum
  if (len > 63) len = 63;

  Preferences prefs;
  String pass;

  if (prefs.begin(kNamespace, /*readOnly=*/true)) {
    pass = prefs.getString(kPassKey, "");
    prefs.end();
  }

  if (pass.length() >= len && isFromAlphabet(pass)) {
    return pass;
  }

  // Fresh device or invalid/short stored value -- generate and persist.
  pass = randomEasyPassword(len, hardwareRng);
  if (!pass.length()) return pass;

  if (prefs.begin(kNamespace, /*readOnly=*/false)) {
    prefs.putString(kPassKey, pass);
    prefs.end();
    LOG.printf("[cfg-portal] generated new SoftAP password (%u chars)\n",
               static_cast<unsigned>(pass.length()));
  } else {
    LOG.println("[cfg-portal] warning: could not persist SoftAP password");
  }
  return pass;
}

void resetApPassword() {
  Preferences prefs;
  if (prefs.begin(kNamespace, /*readOnly=*/false)) {
    prefs.remove(kPassKey);
    prefs.end();
  }
}

}  // namespace config_portal
#endif
