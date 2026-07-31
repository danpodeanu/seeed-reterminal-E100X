#pragma once

#include <Arduino.h>

// Runtime Wi-Fi credentials. Reads NVS via the config-portal storage
// first; when NVS is empty falls back to the compile-time defaults
// baked into secrets.h. This lets a device work out of the box while
// still allowing the on-device portal to override credentials.
namespace weather_wifi {

// Loads NVS credentials into cache. Call once early in setup(), after
// the config portal has had a chance to run.
void load();

// True when either NVS or the compile-time defaults resolved to a
// non-empty SSID.
bool haveCredentials();

// True when NVS holds no SSID at all -- used as one of the triggers
// for launching the config portal automatically.
bool nvsEmpty();

const char* ssid();
const char* password();

}  // namespace weather_wifi
