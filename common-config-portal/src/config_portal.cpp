#ifdef ARDUINO
#include "config_portal.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <vector>

#include "app_logger.h"
#include "config_json.h"
#include "config_storage.h"
#include "wifi_schema.h"

namespace config_portal {
namespace {

WebServer* g_server = nullptr;
DNSServer* g_dns = nullptr;
Config g_config;
String g_ssid;
bool g_running = false;
bool g_rebootRequested = false;
storage::PrefsStorage g_wifiStorage;
storage::PrefsStorage g_appStorage;
uint32_t g_scanMs = 0;
String g_scanJson = "[]";

String macToHexSuffix(const uint8_t mac[6]) {
  // Use the last 2 bytes so the SSID varies per device -- the first 3
  // bytes are the vendor OUI (identical across all ESP32-S3 boards
  // from the same silicon batch).
  char buf[5];
  snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
  return String(buf);
}

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

void redirectWifi() {
  g_server->sendHeader("Location", "/wifi");
  g_server->send(302, "text/plain", "");
}

void sendJson(int code, const String& body) {
  g_server->sendHeader("Cache-Control", "no-store");
  g_server->send(code, "application/json", body);
}

void handleValues(const Schema& schema, storage::Storage& store) {
  std::vector<std::pair<String, String>> values;
  storage::loadForGet(store, schema, values);
  sendJson(200, json::valuesToJson(values));
}

void handleSave(const Schema& schema, storage::Storage& store, bool reboot) {
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
  if (reboot) {
    g_rebootRequested = true;
    sendJson(200, "{\"ok\":true,\"reboot\":true}");
  } else {
    sendJson(200, "{\"ok\":true}");
  }
}

void handleScan() {
  const uint32_t now = millis();
  if (g_scanJson.length() > 2 && now - g_scanMs < 10000U) {
    sendJson(200, g_scanJson);
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    sendJson(200, g_scanJson.length() ? g_scanJson : "[]");
    return;
  }
  if (n < 0) {
    WiFi.scanNetworks(true, true);
    sendJson(200, g_scanJson.length() ? g_scanJson : "[]");
    return;
  }
  String out = "[";
  bool first = true;
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

void handleNotFound() {
  g_server->send(404, "text/html; charset=utf-8",
                 "<!doctype html><title>Not found</title><p>Not found</p>");
}

}  // namespace

String buildSsid(const Config& cfg) {
  uint8_t mac[6] = {};
  // Use the station MAC so the SSID matches the label the user sees on
  // the device / in logs (esp_read_mac derives the SoftAP MAC by
  // OR'ing the locally-administered bit, which shifts the first byte).
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  String s = cfg.apSsidPrefix ? String(cfg.apSsidPrefix) : String();
  s += macToHexSuffix(mac);
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
  const char* pass = (cfg.apPassword && cfg.apPassword[0]) ? cfg.apPassword : nullptr;
  if (!WiFi.softAP(g_ssid.c_str(), pass, 1, 0, cfg.maxConnections)) {
    LOG.println("[cfg-portal] softAP failed");
    return false;
  }
  LOG.printf("[cfg-portal] AP up: ssid=\"%s\" ip=%s\n", g_ssid.c_str(), cfg.apIp.toString().c_str());

  if (g_server) { g_server->stop(); delete g_server; }
  g_server = new WebServer(cfg.httpPort);
  g_server->on("/", redirectWifi);
  g_server->on("/wifi", []() { g_server->send(200, "text/html; charset=utf-8", renderWifiPage(g_config, *g_config.wifiSchema, g_config.appSchema)); });
  g_server->on("/wifi.json", HTTP_GET, []() { handleValues(*g_config.wifiSchema, g_wifiStorage); });
  g_server->on("/wifi.json", HTTP_POST, []() { handleSave(*g_config.wifiSchema, g_wifiStorage, true); });
  g_server->on("/scan.json", handleScan);
  g_server->on("/settings", []() { if (!g_config.appSchema) handleNotFound(); else g_server->send(200, "text/html; charset=utf-8", renderSettingsPage(g_config, *g_config.appSchema, *g_config.wifiSchema)); });
  g_server->on("/settings.json", HTTP_GET, []() { if (!g_config.appSchema) handleNotFound(); else handleValues(*g_config.appSchema, g_appStorage); });
  g_server->on("/settings.json", HTTP_POST, []() { if (!g_config.appSchema) handleNotFound(); else handleSave(*g_config.appSchema, g_appStorage, false); });
  g_server->on("/reboot", HTTP_POST, []() { g_rebootRequested = true; sendJson(200, "{\"ok\":true}"); });
  g_server->on("/panel.json", handlePanel);
  g_server->on("/generate_204", redirectWifi);
  g_server->on("/hotspot-detect.html", redirectWifi);
  g_server->on("/connecttest.txt", redirectWifi);
  g_server->on("/redirect", redirectWifi);
  g_server->on("/ncsi.txt", redirectWifi);
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
IPAddress currentIp() { return g_running ? g_config.apIp : IPAddress(); }
uint16_t currentPort() { return g_config.httpPort; }
bool rebootRequested() { return g_rebootRequested; }

void end() {
  if (g_dns) { g_dns->stop(); delete g_dns; g_dns = nullptr; }
  if (g_server) { g_server->stop(); delete g_server; g_server = nullptr; }
  if (g_running) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); g_running = false; }
}

}  // namespace config_portal
#endif
