#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include "app_logger.h"
#else
#include "config_portal_string_shim.h"
#endif

namespace portal_ap_password {

using RngFn = uint32_t (*)();

inline constexpr char kEasyPasswordAlphabet[] =
    "abcdefghjkmnpqrstuvwxyz23456789";

inline size_t easyPasswordAlphabetSize() {
  return sizeof(kEasyPasswordAlphabet) - 1;
}

inline String randomEasyPassword(size_t len, RngFn rng) {
  if (!rng || len == 0 || len > 63) return String();
  const size_t alphabetSize = easyPasswordAlphabetSize();
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    out += kEasyPasswordAlphabet[rng() % alphabetSize];
  }
  return out;
}

#ifdef ARDUINO
namespace detail {

inline constexpr char kNamespace[] = "portal_ap";
inline constexpr char kPassKey[] = "pass";

inline uint32_t hardwareRng() { return esp_random(); }

inline bool isFromAlphabet(const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    bool found = false;
    for (size_t j = 0; j < easyPasswordAlphabetSize(); ++j) {
      if (value[i] == kEasyPasswordAlphabet[j]) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

}  // namespace detail

inline String ensureApPassword(size_t len = 8) {
  if (len < 8) len = 8;
  if (len > 63) len = 63;

  Preferences prefs;
  String password;
  if (prefs.begin(detail::kNamespace, /*readOnly=*/true)) {
    password = prefs.getString(detail::kPassKey, "");
    prefs.end();
  }

  if (password.length() >= len && detail::isFromAlphabet(password)) {
    return password;
  }

  password = randomEasyPassword(len, detail::hardwareRng);
  if (!password.length()) return password;

  if (prefs.begin(detail::kNamespace, /*readOnly=*/false)) {
    prefs.putString(detail::kPassKey, password);
    prefs.end();
    LOG.printf("[portal] generated new SoftAP password (%u chars)\n",
               static_cast<unsigned>(password.length()));
  } else {
    LOG.println("[portal] warning: could not persist SoftAP password");
  }
  return password;
}

inline void resetApPassword() {
  Preferences prefs;
  if (prefs.begin(detail::kNamespace, /*readOnly=*/false)) {
    prefs.remove(detail::kPassKey);
    prefs.end();
  }
}
#endif

}  // namespace portal_ap_password
