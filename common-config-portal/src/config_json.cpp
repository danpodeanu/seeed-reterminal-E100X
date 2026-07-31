#include "config_json.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <vector>

namespace config_portal {
namespace json {
namespace {

String serializeDoc(const JsonDocument& doc) {
  const size_t n = measureJson(doc);
  std::vector<char> buf(n + 1);
  serializeJson(doc, buf.data(), buf.size());
  return String(buf.data());
}

}  // namespace

String valuesToJson(const std::vector<std::pair<String, String>>& values) {
  JsonDocument doc;
  doc["ok"] = true;
  JsonObject data = doc["values"].to<JsonObject>();
  for (const auto& kv : values) data[kv.first.c_str()] = kv.second.c_str();
  return serializeDoc(doc);
}

bool parseSubmissionJson(const Schema& schema, const String& body,
                         std::map<String, String>& out, String* err) {
  out.clear();
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body.c_str());
  if (e) {
    if (err) *err = "malformed JSON";
    return false;
  }
  if (!doc.is<JsonObject>()) {
    if (err) *err = "JSON body must be an object";
    return false;
  }
  JsonObject obj = doc.as<JsonObject>();
  for (JsonPair kv : obj) {
    const char* key = kv.key().c_str();
    if (!findField(schema, key)) continue;
    JsonVariant v = kv.value();
    if (v.is<const char*>()) {
      out[String(key)] = String(v.as<const char*>());
    } else if (v.is<bool>()) {
      out[String(key)] = String(v.as<bool>() ? "true" : "false");
    } else if (v.is<int>()) {
      char buf[24];
      snprintf(buf, sizeof(buf), "%d", v.as<int>());
      out[String(key)] = String(buf);
    } else if (v.is<long>()) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%ld", v.as<long>());
      out[String(key)] = String(buf);
    } else if (v.is<double>()) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.8g", v.as<double>());
      out[String(key)] = String(buf);
    } else if (v.isNull()) {
      out[String(key)] = String();
    } else {
      if (err) {
        *err = key;
        *err += ": unsupported JSON value";
      }
      return false;
    }
  }
  return true;
}

String errorJson(const char* message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message ? message : "error";
  return serializeDoc(doc);
}

}  // namespace json
}  // namespace config_portal
