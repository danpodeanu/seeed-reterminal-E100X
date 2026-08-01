#pragma once

#include "config_schema.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <IPAddress.h>

class WebServer;

// Schema-driven Wi-Fi + application settings portal. Call begin() from a
// viewer's maintenance/configuration mode, then pump loop() until
// rebootRequested() is true (or until the app exits the mode another way).
namespace config_portal {

// Extra navigation tab. Rendered by the portal chrome after the built-in
// Wi-Fi / Settings entries and before Reset. Apps use this to expose extra
// pages (e.g. SD-card browser, photo uploader) served by handlers they
// install directly on webServer() after begin() returns.
struct NavTab {
  const char* label = nullptr;
  const char* href = nullptr;
  // Highlight this tab when appendChrome's `active` string matches. NULL
  // means "never highlight" (use for tabs whose page renders its own chrome).
  const char* activeKey = nullptr;
};

struct Config {
  const char* apSsidPrefix = "ReTerminal ";
  const char* apPassword = nullptr;
  // When true and apPassword is null/empty, the portal generates an
  // 8-character password on first boot, persists it in NVS, and reuses
  // it thereafter. Retrievable via currentApPassword() so the caller
  // can embed it in the on-screen splash + QR payload.
  bool useAutoApPassword = false;
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

  // Optional array of extra navigation tabs the portal chrome renders
  // between Settings and Reset. The pages behind these tabs must be
  // registered on webServer() (returned by webServer() after begin())
  // by the app itself.
  const NavTab* extraTabs = nullptr;
  size_t extraTabCount = 0;
};

String buildSsid(const Config& cfg = Config{});
String wifiQrPayload(const String& ssid, const char* password = nullptr);
String urlQrPayload(const IPAddress& ip, uint16_t port = 80, const char* path = nullptr);

bool begin(const Config& cfg);
void loop();

// Access the running WebServer. Valid after begin() succeeds, invalidated
// by end(). Apps use this to install extra route handlers (paired with
// Config::extraTabs) without spinning up a second HTTP listener.
WebServer* webServer();

// Render a nav-strip HTML fragment identical to the one the portal chrome
// emits inside <nav>...</nav>. Handy for extra pages that share the portal
// server but render their own layout - pass the same Config that was used
// with begin() and the activeKey to highlight (or nullptr).
String renderNavStripHtml(const Config& cfg, const char* activeKey);

const String& currentSsid();
const String& currentApPassword();  // empty when the AP is open
IPAddress currentIp();
uint16_t currentPort();
bool rebootRequested();
void end();

String renderWifiPage(const Config& cfg, const Schema& wifi, const Schema* appSchema);
String renderSettingsPage(const Config& cfg, const Schema& appSchema, const Schema& wifi);
String renderResetPage(const Config& cfg, bool hasSettings);

}  // namespace config_portal
#endif
