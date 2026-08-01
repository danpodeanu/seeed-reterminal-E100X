#pragma once

#include <Arduino.h>

// Runtime Wi-Fi credentials for photo-viewer. Reads NVS via the config-
// portal storage first; when NVS is empty falls back to the compile-time
// defaults baked into secrets.h. This lets a device work out of the box
// while still allowing the on-device portal (green button) to override
// the credentials.
namespace photo_wifi {

// Loads NVS credentials into cache. Call once early in setup().
void load();

// True when either NVS or the compile-time defaults resolved to a
// non-empty, non-placeholder SSID.
bool haveCredentials();

// True when NVS holds no SSID at all.
bool nvsEmpty();

const char* ssid();
const char* password();

}  // namespace photo_wifi
