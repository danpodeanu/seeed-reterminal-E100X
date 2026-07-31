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
