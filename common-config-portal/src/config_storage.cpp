#include "config_storage.h"

#include <stdlib.h>
#include <string.h>

namespace config_portal {
namespace storage {
namespace {

bool isSecret(const Field& f) {
  return f.type == FieldType::Secret || f.type == FieldType::Password;
}

const char* defVal(const Field& f) { return f.defaultVal ? f.defaultVal : ""; }

bool equalsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

bool boolValue(const String& s) {
  const char* v = s.c_str();
  return equalsIgnoreCase(v, "true") || strcmp(v, "1") == 0 ||
         equalsIgnoreCase(v, "on") || equalsIgnoreCase(v, "yes");
}

String readField(Storage& s, const Field& f) {
  return s.getString(f.key, defVal(f));
}

}  // namespace

#ifdef ARDUINO
bool PrefsStorage::begin(const char* ns, bool readOnly) {
  return prefs_.begin(ns, readOnly);
}
void PrefsStorage::end() { prefs_.end(); }
bool PrefsStorage::has(const char* key) { return prefs_.isKey(key); }
String PrefsStorage::getString(const char* key, const char* defaultVal) {
  return prefs_.getString(key, defaultVal ? defaultVal : "");
}
bool PrefsStorage::putString(const char* key, const char* value) {
  return prefs_.putString(key, value ? value : "") > 0;
}
bool PrefsStorage::remove(const char* key) { return prefs_.remove(key); }
bool PrefsStorage::clear() { return prefs_.clear(); }
#endif

void loadForGet(Storage& s, const Schema& schema,
                std::vector<std::pair<String, String>>& out) {
  out.clear();
  const bool nvsOpen = s.begin(schema.nvsNamespace, true);
  for (size_t si = 0; si < schema.sectionCount; ++si) {
    const Section& section = schema.sections[si];
    for (size_t fi = 0; fi < section.fieldCount; ++fi) {
      const Field& f = section.fields[fi];
      // If NVS is unavailable (e.g. the namespace hasn't been created
      // yet on a fresh device), fall back to the schema defaults so the
      // browser can still pre-populate the form.
      String value = nvsOpen ? readField(s, f) : String(defVal(f));
      if (isSecret(f) && value.length() > 0) value = kSecretSentinel;
      out.push_back(std::make_pair(String(f.key), value));
    }
  }
  if (nvsOpen) s.end();
}

bool save(Storage& s, const Schema& schema,
          const std::map<String, String>& submitted, String* err) {
  for (const auto& item : submitted) {
    const Field* f = findField(schema, item.first.c_str());
    if (!f) continue;
    if (isSecret(*f) && item.second == kSecretSentinel) continue;
    if (!validateField(schema, *f, item.second.c_str(), err)) return false;
  }

  if (!s.begin(schema.nvsNamespace, false)) {
    if (err) *err = "storage open failed";
    return false;
  }
  bool ok = true;
  // Save persists every submitted field to NVS, even when the value
  // matches what NVS already holds (or what the current schema default
  // resolves to). This keeps user intent stable across firmware
  // upgrades that change default values -- a field the user actively
  // reviewed and confirmed lands in NVS explicitly, so a future
  // firmware whose defaults shift can never silently override it.
  //
  // Secret fields keep their existing three-way handling: sentinel ->
  // leave NVS untouched; empty -> erase; anything else -> write.
  for (const auto& item : submitted) {
    const Field* f = findField(schema, item.first.c_str());
    if (!f) continue;
    if (isSecret(*f)) {
      if (item.second == kSecretSentinel) continue;
      if (item.second.length() == 0) {
        if (s.has(f->key) && !s.remove(f->key)) ok = false;
        continue;
      }
    }
    if (!s.putString(f->key, item.second.c_str())) ok = false;
  }
  s.end();
  if (!ok && err) *err = "storage write failed";
  return ok;
}

String getString(Storage& s, const Schema& schema, const char* key) {
  const Field* f = findField(schema, key);
  if (!f) return String();
  if (!s.begin(schema.nvsNamespace, true)) return String(defVal(*f));
  String value = readField(s, *f);
  s.end();
  return value;
}

int32_t getInt(Storage& s, const Schema& schema, const char* key) {
  const Field* f = findField(schema, key);
  if (!f) return 0;
  const String value = getString(s, schema, key);
  int32_t out = 0;
  return validateInt(schema, *f, value.c_str(), &out) ? out : atoi(defVal(*f));
}

bool getBool(Storage& s, const Schema& schema, const char* key) {
  const Field* f = findField(schema, key);
  if (!f) return false;
  const String value = getString(s, schema, key);
  if (validateBool(value.c_str())) return boolValue(value);
  return boolValue(String(defVal(*f)));
}

float getFloat(Storage& s, const Schema& schema, const char* key) {
  const Field* f = findField(schema, key);
  if (!f) return 0.0f;
  const String value = getString(s, schema, key);
  float out = 0.0f;
  return validateFloat(*f, value.c_str(), &out) ? out : static_cast<float>(atof(defVal(*f)));
}

}  // namespace storage
}  // namespace config_portal
