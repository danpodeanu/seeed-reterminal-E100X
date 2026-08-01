#include "config_schema.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace config_portal {
namespace {

bool isEmpty(const char* s) { return !s || !s[0]; }

bool equalsIgnoreCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower(static_cast<unsigned char>(*a)) !=
        tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

void setError(String* err, const Field& f, const char* msg) {
  if (!err) return;
  err->clear();
  *err += f.key ? f.key : "field";
  *err += ": ";
  *err += msg;
}

bool keyLengthOk(const char* key) {
  return key && key[0] && strlen(key) <= 15;
}

}  // namespace

const Field* findField(const Schema& s, const char* key) {
  if (!key) return nullptr;
  for (size_t si = 0; si < s.sectionCount; ++si) {
    const Section& section = s.sections[si];
    for (size_t fi = 0; fi < section.fieldCount; ++fi) {
      const Field& f = section.fields[fi];
      if (f.key && strcmp(f.key, key) == 0) return &f;
    }
  }
  return nullptr;
}

bool validateBool(const char* s) {
  return equalsIgnoreCase(s, "true") || equalsIgnoreCase(s, "false") ||
         equalsIgnoreCase(s, "1") || equalsIgnoreCase(s, "0") ||
         equalsIgnoreCase(s, "on") || equalsIgnoreCase(s, "off") ||
         equalsIgnoreCase(s, "yes") || equalsIgnoreCase(s, "no");
}

bool validateInt(const Schema& /*s*/, const Field& f, const char* val,
                 int32_t* out) {
  if (isEmpty(val)) return false;
  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(val, &end, 10);
  if (errno != 0 || end == val || !end || *end != '\0') return false;
  if (parsed < INT32_MIN || parsed > INT32_MAX) return false;
  const int32_t v = static_cast<int32_t>(parsed);
  if ((f.minVal != 0 || f.maxVal != 0) &&
      (v < f.minVal || v > f.maxVal)) {
    return false;
  }
  if (out) *out = v;
  return true;
}

bool validateFloat(const Field& /*f*/, const char* val, float* out) {
  if (isEmpty(val)) return false;
  errno = 0;
  char* end = nullptr;
  const float parsed = strtof(val, &end);
  if (errno != 0 || end == val || !end || *end != '\0') return false;
  if (out) *out = parsed;
  return true;
}

bool validateEnum(const Field& f, const char* val) {
  if (isEmpty(val) || !f.enumValues) return false;
  for (const char* const* p = f.enumValues; *p; ++p) {
    if (strcmp(*p, val) == 0) return true;
  }
  return false;
}

bool validateField(const Schema& s, const Field& f, const char* val, String* err) {
  if (!keyLengthOk(f.key)) {
    setError(err, f, "key must be 1-15 characters");
    return false;
  }
  if (!s.nvsNamespace || strlen(s.nvsNamespace) > 15) {
    setError(err, f, "namespace must be 1-15 characters");
    return false;
  }

  const char* safeVal = val ? val : "";
  if (f.pattern && f.pattern[0] && !strstr(safeVal, f.pattern)) {
    setError(err, f, "does not match required pattern");
    return false;
  }

  bool ok = false;
  switch (f.type) {
    case FieldType::Bool:
      ok = validateBool(safeVal);
      break;
    case FieldType::Int: {
      int32_t ignored = 0;
      ok = validateInt(s, f, safeVal, &ignored);
      break;
    }
    case FieldType::Float: {
      float ignored = 0.0f;
      ok = validateFloat(f, safeVal, &ignored);
      break;
    }
    case FieldType::Enum:
      ok = validateEnum(f, safeVal);
      break;
    case FieldType::String:
      ok = true;
      if ((f.minVal != 0 || f.maxVal != 0) &&
          (strlen(safeVal) < static_cast<size_t>(f.minVal) ||
           strlen(safeVal) > static_cast<size_t>(f.maxVal))) {
        ok = false;
      }
      break;
    case FieldType::Timezone:
      // Same length policy as String. We deliberately do not validate
      // the POSIX-TZ grammar: the platform's tzset() is the source of
      // truth (it silently falls back to UTC on garbage), and users
      // reaching the "Custom (POSIX)" branch have opted into that.
      ok = safeVal[0] != '\0';
      if (ok && (f.minVal != 0 || f.maxVal != 0) &&
          (strlen(safeVal) < static_cast<size_t>(f.minVal) ||
           strlen(safeVal) > static_cast<size_t>(f.maxVal))) {
        ok = false;
      }
      break;
    case FieldType::Secret:
    case FieldType::Password:
      ok = safeVal[0] != '\0' || strcmp(safeVal, kSecretSentinel) == 0;
      // Empty secrets are accepted by storage::save as a clear operation.
      ok = true;
      if ((f.minVal != 0 || f.maxVal != 0) &&
          (strlen(safeVal) < static_cast<size_t>(f.minVal) ||
           strlen(safeVal) > static_cast<size_t>(f.maxVal))) {
        ok = false;
      }
      break;
  }
  if (!ok) setError(err, f, "invalid value");
  return ok;
}

}  // namespace config_portal
