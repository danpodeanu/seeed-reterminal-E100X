#pragma once

#include "config_schema.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <IPAddress.h>

// Schema-driven Wi-Fi + application settings portal. Call begin() from a
// viewer's maintenance/configuration mode, then pump loop() until
// rebootRequested() is true (or until the app exits the mode another way).
namespace config_portal {

struct Config {
  const char* apSsidPrefix = "ReTerminal ";
  const char* apPassword = nullptr;
  IPAddress apIp{192, 168, 1, 1};
  IPAddress apGateway{192, 168, 1, 1};
  IPAddress apNetmask{255, 255, 255, 0};
  uint16_t httpPort = 80;
  uint8_t maxConnections = 4;

  const Schema* wifiSchema = nullptr;
  const Schema* appSchema = nullptr;
  const char* appName = "reTerminal";
  const char* helpUrl = "https://github.com/danpodeanu/seeed-reterminal-E100X#configuration";
  const char* firmwareVersion = nullptr;

  // Optional compile-time-defaults provider consulted when NVS has no
  // value for a Wi-Fi field yet. Lets apps like xkcd-viewer pre-populate
  // the form with the credentials baked into secrets.h so the user sees
  // what's currently in use instead of a blank field. Return an empty
  // String when there's no fallback for the requested key. Only used
  // for /wifi.json GET; secrets are still redacted with the __saved__
  // sentinel before being sent to the browser.
  using WifiFallbackFn = String (*)(const char* key);
  WifiFallbackFn wifiFallback = nullptr;
};

String buildSsid(const Config& cfg = Config{});
String wifiQrPayload(const String& ssid, const char* password = nullptr);
String urlQrPayload(const IPAddress& ip, uint16_t port = 80, const char* path = nullptr);

bool begin(const Config& cfg);
void loop();

const String& currentSsid();
IPAddress currentIp();
uint16_t currentPort();
bool rebootRequested();
void end();

String renderWifiPage(const Config& cfg, const Schema& wifi, const Schema* appSchema);
String renderSettingsPage(const Config& cfg, const Schema& appSchema, const Schema& wifi);

}  // namespace config_portal
#endif
