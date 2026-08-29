#ifdef ARDUINO
#include "config_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <vector>

#include "app_logger.h"
#include "config_json.h"
#include "config_storage.h"
#include "portal_ap_password.h"
#include "portal_identity.h"
#include "wifi_schema.h"

namespace config_portal {
namespace {

WebServer* g_server = nullptr;
DNSServer* g_dns = nullptr;
Config g_config;
String g_ssid;
String g_apPassword;  // empty when the AP is open
bool g_running = false;
bool g_rebootRequested = false;
storage::PrefsStorage g_wifiStorage;
storage::PrefsStorage g_appStorage;
uint32_t g_scanMs = 0;
String g_scanJson = "[]";

String jsonEscape(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (static_cast<uint8_t>(c) < 0x20) out += ' ';
    else out += c;
  }
  return out;
}

// Compact per-request access log inspired by nginx: client IP, HTTP
// method, request URI, and status code go to the serial log so an
// operator can see when the browser is talking to the portal.
void logAccess(int statusCode) {
  if (!g_server) return;
  const char* method;
  switch (g_server->method()) {
    case HTTP_GET:     method = "GET"; break;
    case HTTP_POST:    method = "POST"; break;
    case HTTP_PUT:     method = "PUT"; break;
    case HTTP_DELETE:  method = "DELETE"; break;
    case HTTP_OPTIONS: method = "OPTIONS"; break;
    case HTTP_HEAD:    method = "HEAD"; break;
    default:           method = "?"; break;
  }
  LOG.printf("[cfg-portal] %s - %s %s %d\n",
             g_server->client().remoteIP().toString().c_str(),
             method, g_server->uri().c_str(), statusCode);
}

// Root and captive-portal redirect. Sends the browser to /wifi during
// first-time setup (no SSID stored yet) and to the app's configured
// landing page once Wi-Fi is configured. Falls back to /settings when
// no landing path is set and the app registered an appSchema.
void redirectRoot() {
  const char* dest = "/wifi";
  bool hasWifi = false;
  if (g_config.wifiSchema) {
    if (g_wifiStorage.begin(g_config.wifiSchema->nvsNamespace, true)) {
      hasWifi = g_wifiStorage.getString("ssid", "").length() > 0;
      g_wifiStorage.end();
    }
  }
  if (hasWifi) {
    if (g_config.postConfigLandingPath && g_config.postConfigLandingPath[0]) {
      dest = g_config.postConfigLandingPath;
    } else if (g_config.appSchema) {
      dest = "/settings";
    }
  }
  g_server->sendHeader("Location", dest);
  g_server->send(302, "text/plain", "");
  logAccess(302);
}

void sendJson(int code, const String& body) {
  g_server->sendHeader("Cache-Control", "no-store");
  g_server->send(code, "application/json", body);
  logAccess(code);
}

void sendHtml(int code, const String& body) {
  g_server->send(code, "text/html; charset=utf-8", body);
  logAccess(code);
}

void handleValues(const Schema& schema, storage::Storage& store) {
  std::vector<std::pair<String, String>> values;
  storage::loadForGet(store, schema, values);
  sendJson(200, json::valuesToJson(values));
}

// Wi-Fi-specific values handler that layers the app-provided compile-
// time fallback (e.g. secrets.h) over an empty NVS so the form still
// shows the credentials the device is currently using. Secrets are
// redacted with the __saved__ sentinel so we never leak the real
// password over HTTP.
void handleWifiValues() {
  std::vector<std::pair<String, String>> values;
  storage::loadForGet(g_wifiStorage, *g_config.wifiSchema, values);
  if (g_config.wifiFallback) {
    for (auto& kv : values) {
      // If loadForGet already returned the __saved__ sentinel we leave
      // it alone (a real secret is stored in NVS).
      if (kv.second.length() > 0 && kv.second != "__saved__") continue;
      String fb = g_config.wifiFallback(kv.first.c_str());
      if (!fb.length()) continue;
      const Field* f = findField(*g_config.wifiSchema, kv.first.c_str());
      const bool secret = f && (f->type == FieldType::Secret ||
                                f->type == FieldType::Password);
      kv.second = secret ? String("__saved__") : fb;
    }
  }
  sendJson(200, json::valuesToJson(values));
}

void handleSave(const Schema& schema, storage::Storage& store, bool reboot,
                void (*onSuccess)() = nullptr) {
  String err;
  std::map<String, String> submitted;
  if (!json::parseSubmissionJson(schema, g_server->arg("plain"), submitted, &err)) {
    sendJson(400, json::errorJson(err.c_str()));
    return;
  }
  if (!storage::save(store, schema, submitted, &err)) {
    sendJson(400, json::errorJson(err.c_str()));
    return;
  }
  // Fire post-save hook AFTER the write is durable but BEFORE the
  // response is sent, so the very next request the browser makes (e.g.
  // /photo-panel.json after saving the orientation setting) already
  // sees the refreshed runtime cache.
  if (onSuccess) onSuccess();
  if (reboot) {
    g_rebootRequested = true;
    sendJson(200, "{\"ok\":true,\"reboot\":true}");
  } else {
    sendJson(200, "{\"ok\":true}");
  }
}

void handleWifiSave() {
  String err;
  std::map<String, String> submitted;
  if (!json::parseSubmissionJson(*g_config.wifiSchema,
                                 g_server->arg("plain"), submitted, &err)) {
    sendJson(400, json::errorJson(err.c_str()));
    return;
  }
  if (!storage::save(g_wifiStorage, *g_config.wifiSchema, submitted, &err)) {
    sendJson(400, json::errorJson(err.c_str()));
    return;
  }
  const auto password = submitted.find("password");
  if (g_config.onWifiPasswordSaved && password != submitted.end() &&
      password->second != kSecretSentinel &&
      !g_config.onWifiPasswordSaved(password->second.isEmpty(), err)) {
    sendJson(500, json::errorJson(err.c_str()));
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

void handleScan() {
  const uint32_t now = millis();
  if (g_scanJson.length() > 2 && now - g_scanMs < 10000U) {
    sendJson(200, g_scanJson);
    return;
  }
  // Cancel any half-finished async scan from a previous request, then
  // run a blocking scan so the browser gets real results in a single
  // round trip (the previous async design returned "[]" on the first
  // click and the frontend never re-polled, so the dropdown stayed
  // empty). A blocking scan takes ~2-3 s which is fine for a captive
  // portal UI.
  const int prior = WiFi.scanComplete();
  if (prior >= 0) WiFi.scanDelete();
  LOG.println("[cfg-portal] scanning for Wi-Fi networks...");
  const uint32_t scanStart = millis();
  const int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  LOG.printf("[cfg-portal] scan complete: %d networks in %u ms\n",
             n, static_cast<unsigned>(millis() - scanStart));
  String out = "[";
  bool first = true;
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      if (!first) out += ',';
      first = false;
      out += "{\"ssid\":\"";
      out += jsonEscape(ssid);
      out += "\",\"rssi\":";
      out += WiFi.RSSI(i);
      out += ",\"secure\":";
      out += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true";
      out += "}";
    }
  }
  out += "]";
  WiFi.scanDelete();
  g_scanJson = out;
  g_scanMs = now;
  sendJson(200, g_scanJson);
}

void handlePanel() {
  String out = "{\"ok\":true,\"app\":\"";
  out += jsonEscape(g_config.appName ? g_config.appName : "reTerminal");
  out += "\",\"hasWifi\":";
  out += g_config.wifiSchema ? "true" : "false";
  out += ",\"hasSettings\":";
  out += g_config.appSchema ? "true" : "false";
  out += "}";
  sendJson(200, out);
}

// Wipe a Preferences namespace. Returns true if we successfully opened
// it and issued a clear (or if it was already empty / non-existent -
// the goal is "no user config remains", not "namespace existed").
bool clearNamespace(const char* ns) {
  if (!ns || !ns[0]) return false;
  Preferences p;
  if (!p.begin(ns, /*readOnly=*/false)) {
    LOG.printf("[cfg-portal] reset: namespace \"%s\" absent, nothing to clear\n", ns);
    return true;
  }
  const bool ok = p.clear();
  p.end();
  LOG.printf("[cfg-portal] reset: cleared NVS namespace \"%s\" (%s)\n",
             ns, ok ? "ok" : "clear failed");
  return ok;
}

void handleReset() {
  LOG.println("[cfg-portal] reset requested via /reset.json");
  bool ok = true;
  if (g_config.wifiSchema && g_config.wifiSchema->nvsNamespace) {
    ok &= clearNamespace(g_config.wifiSchema->nvsNamespace);
  }
  if (g_config.appSchema && g_config.appSchema->nvsNamespace) {
    ok &= clearNamespace(g_config.appSchema->nvsNamespace);
  }
  if (!ok) {
    sendJson(500, json::errorJson("one or more namespaces failed to clear"));
    return;
  }
  g_rebootRequested = true;
  sendJson(200, "{\"ok\":true,\"reboot\":true}");
}

void handleNotFound() {
  g_server->send(404, "text/html; charset=utf-8",
                 "<!doctype html><title>Not found</title><p>Not found</p>");
  logAccess(404);
}

}  // namespace

String buildSsid(const Config& cfg) {
  uint8_t mac[6] = {};
  // Use the station MAC so the SSID matches the label the user sees on
  // the device / in logs (esp_read_mac derives the SoftAP MAC by
  // OR'ing the locally-administered bit, which shifts the first byte).
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  String s = cfg.apSsidPrefix ? String(cfg.apSsidPrefix) : String();
  char suffix[5];
  portal_identity::formatSsidSuffix(mac, suffix);
  s += suffix;
  return s;
}

String wifiQrPayload(const String& ssid, const char* password) {
  auto esc = [](const String& in) {
    String out;
    out.reserve(in.length() + 4);
    for (size_t i = 0; i < in.length(); ++i) {
      const char c = in[i];
      if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
      out += c;
    }
    return out;
  };
  String payload = "WIFI:S:";
  payload += esc(ssid);
  if (password && password[0]) {
    payload += ";T:WPA;P:";
    payload += esc(String(password));
    payload += ";;";
  } else {
    payload += ";T:nopass;;";
  }
  return payload;
}

String urlQrPayload(const IPAddress& ip, uint16_t port, const char* path) {
  String url = "http://";
  url += ip.toString();
  if (port != 80) { url += ':'; url += String(port); }
  if (path && path[0]) { if (path[0] != '/') url += '/'; url += path; }
  else url += '/';
  return url;
}

bool begin(const Config& cfg) {
  if (!cfg.wifiSchema) {
    LOG.println("[cfg-portal] wifiSchema is required");
    return false;
  }
  g_config = cfg;
  if (!g_config.wifiSchema) g_config.wifiSchema = &kWifiSchema;
  g_rebootRequested = false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(cfg.apIp, cfg.apGateway, cfg.apNetmask)) {
    LOG.println("[cfg-portal] softAPConfig failed");
    return false;
  }
  g_ssid = buildSsid(cfg);
  g_apPassword = String();
  if (cfg.apPassword && cfg.apPassword[0]) {
    g_apPassword = cfg.apPassword;
  } else if (cfg.useAutoApPassword) {
    g_apPassword = portal_ap_password::ensureApPassword(8);
  }
  const char* pass = g_apPassword.length() ? g_apPassword.c_str() : nullptr;
  if (!WiFi.softAP(g_ssid.c_str(), pass, 1, 0, cfg.maxConnections)) {
    LOG.println("[cfg-portal] softAP failed");
    return false;
  }
  if (pass) {
    LOG.printf("[cfg-portal] AP up: ssid=\"%s\" pass=\"%s\" ip=%s (WPA2-PSK)\n",
               g_ssid.c_str(), g_apPassword.c_str(),
               cfg.apIp.toString().c_str());
  } else {
    LOG.printf("[cfg-portal] AP up: ssid=\"%s\" ip=%s (open)\n",
               g_ssid.c_str(), cfg.apIp.toString().c_str());
  }

  // Log AP client connect / disconnect / IP assignment so an operator
  // can see which device is talking to the portal.
  WiFi.onEvent([](WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
    const auto& sta = info.wifi_ap_staconnected;
    LOG.printf("[cfg-portal] AP client connected: "
               "%02X:%02X:%02X:%02X:%02X:%02X aid=%u\n",
               sta.mac[0], sta.mac[1], sta.mac[2],
               sta.mac[3], sta.mac[4], sta.mac[5], sta.aid);
  }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent([](WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
    const auto& sta = info.wifi_ap_stadisconnected;
    LOG.printf("[cfg-portal] AP client disconnected: "
               "%02X:%02X:%02X:%02X:%02X:%02X aid=%u\n",
               sta.mac[0], sta.mac[1], sta.mac[2],
               sta.mac[3], sta.mac[4], sta.mac[5], sta.aid);
  }, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
  WiFi.onEvent([](WiFiEvent_t /*event*/, WiFiEventInfo_t info) {
    const uint32_t ip = info.wifi_ap_staipassigned.ip.addr;
    LOG.printf("[cfg-portal] AP client got IP: %u.%u.%u.%u\n",
               (unsigned)((ip >> 0)  & 0xFF),
               (unsigned)((ip >> 8)  & 0xFF),
               (unsigned)((ip >> 16) & 0xFF),
               (unsigned)((ip >> 24) & 0xFF));
  }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

  if (g_server) { g_server->stop(); delete g_server; }
  g_server = new WebServer(cfg.httpPort);
  g_server->on("/", redirectRoot);
  g_server->on("/wifi", []() { sendHtml(200, renderWifiPage(g_config, *g_config.wifiSchema, g_config.appSchema)); });
  g_server->on("/wifi.json", HTTP_GET, handleWifiValues);
  g_server->on("/wifi.json", HTTP_POST, handleWifiSave);
  g_server->on("/scan.json", handleScan);
  g_server->on("/settings", []() { if (!g_config.appSchema) handleNotFound(); else sendHtml(200, renderSettingsPage(g_config, *g_config.appSchema, *g_config.wifiSchema)); });
  g_server->on("/settings.json", HTTP_GET, []() { if (!g_config.appSchema) handleNotFound(); else handleValues(*g_config.appSchema, g_appStorage); });
  g_server->on("/settings.json", HTTP_POST, []() {
    if (!g_config.appSchema) { handleNotFound(); return; }
    handleSave(*g_config.appSchema, g_appStorage, false, g_config.onAppSaved);
  });
  g_server->on("/reboot", HTTP_POST, []() { g_rebootRequested = true; sendJson(200, "{\"ok\":true}"); });
  g_server->on("/reset", []() { sendHtml(200, renderResetPage(g_config, g_config.appSchema != nullptr)); });
  g_server->on("/reset.json", HTTP_POST, handleReset);
  g_server->on("/format-sd.json", HTTP_POST, []() {
    if (!g_config.sdFormat) { handleNotFound(); return; }
    LOG.println("[cfg-portal] SD format requested via /format-sd.json");
    String err;
    const bool ok = g_config.sdFormat(err);
    if (!ok) {
      LOG.printf("[cfg-portal] SD format failed: %s\n",
                 err.length() ? err.c_str() : "unknown");
      if (!err.length()) err = F("SD format failed");
      sendJson(500, json::errorJson(err.c_str()));
      return;
    }
    LOG.println("[cfg-portal] SD format: ok");
    sendJson(200, "{\"ok\":true}");
  });
  g_server->on("/panel.json", handlePanel);
  g_server->on("/generate_204", redirectRoot);
  g_server->on("/hotspot-detect.html", redirectRoot);
  g_server->on("/connecttest.txt", redirectRoot);
  g_server->on("/redirect", redirectRoot);
  g_server->on("/ncsi.txt", redirectRoot);
  g_server->onNotFound(handleNotFound);
  g_server->begin();

  if (g_dns) { g_dns->stop(); delete g_dns; }
  g_dns = new DNSServer();
  g_dns->setErrorReplyCode(DNSReplyCode::NoError);
  if (!g_dns->start(53, "*", cfg.apIp)) {
    LOG.println("[cfg-portal] DNS server failed to start");
    delete g_dns;
    g_dns = nullptr;
  }
  g_running = true;
  return true;
}

void loop() {
  if (g_running && g_server) g_server->handleClient();
  if (g_running && g_dns) g_dns->processNextRequest();
}

const String& currentSsid() { return g_ssid; }
const String& currentApPassword() { return g_apPassword; }
IPAddress currentIp() { return g_running ? g_config.apIp : IPAddress(); }
uint16_t currentPort() { return g_config.httpPort; }
bool rebootRequested() { return g_rebootRequested; }

WebServer* webServer() { return g_server; }

void end() {
  if (g_dns) { g_dns->stop(); delete g_dns; g_dns = nullptr; }
  if (g_server) { g_server->stop(); delete g_server; g_server = nullptr; }
  if (g_running) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); g_running = false; }
}

}  // namespace config_portal
#endif
