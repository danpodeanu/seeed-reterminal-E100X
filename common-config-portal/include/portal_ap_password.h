#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "config_portal_string_shim.h"
#endif

// Persistent-per-device SoftAP password helper for the config portal.
//
// The portal used to boot with an OPEN AP (no password) because there
// was no way to give every device a unique key without compile-time
// per-device configuration. This module solves that by generating an
// 8-character password on first boot, stashing it in NVS under a
// dedicated namespace, and returning the same value on every
// subsequent boot. The password is intentionally drawn from an
// easy-to-type alphabet (lowercase + digits, no 0/1/i/l/o confusables)
// so the user can key it in on a phone if they don't want to scan the
// QR splash.
namespace config_portal {

// Non-Arduino code paths (unit tests) can inject a deterministic RNG.
using RngFn = uint32_t (*)();

// Generate `len` characters from the easy-to-type alphabet using
// `rng()` for entropy. `len` must be >= 1 and <= 63 (max WPA2 PSK is
// 63 chars). Returns an empty String on bad input. Pure - no I/O, no
// globals - so it can be exercised on the host.
String randomEasyPassword(size_t len, RngFn rng);

// The alphabet used by randomEasyPassword. Exposed for tests.
extern const char kEasyPasswordAlphabet[];  // null-terminated
size_t easyPasswordAlphabetSize();

#ifdef ARDUINO
// Return the persistent SoftAP password for this device. Reads from
// NVS (namespace "portal_ap", key "pass"); if missing or shorter than
// `len`, generates a fresh one using esp_random() and stores it.
// Subsequent calls return the same string as long as NVS survives.
String ensureApPassword(size_t len = 8);

// For manual recovery: wipe the stored password so the next call to
// ensureApPassword() generates a new one. Not wired to any UI yet.
void resetApPassword();
#endif

}  // namespace config_portal
