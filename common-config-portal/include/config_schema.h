#pragma once

#include "config_portal_string_shim.h"

#include <stddef.h>
#include <stdint.h>

// Small, Arduino-free schema model used by the configuration portal and
// by host-side tests. Field keys double as NVS keys, so keep them short
// (NVS allows at most 15 characters).
namespace config_portal {

constexpr const char* kSecretSentinel = "__saved__";

enum class FieldType { Bool, Int, Float, String, Enum, Secret, Password };

// Pattern support is intentionally tiny: when `pattern` is non-null and
// non-empty the submitted value must contain it as a literal substring.
// This keeps validation cheap on-device and avoids pulling in <regex>.
struct Field {
  const char* key;
  const char* label;
  const char* helpText;
  FieldType type;
  const char* defaultVal;
  const char* const* enumValues;
  int32_t minVal;
  int32_t maxVal;
  const char* pattern;
};

struct Section {
  const char* title;
  const Field* fields;
  size_t fieldCount;
};

struct Schema {
  const char* nvsNamespace;
  const Section* sections;
  size_t sectionCount;
};

const Field* findField(const Schema& s, const char* key);
bool validateBool(const char* s);
bool validateInt(const Schema& s, const Field& f, const char* val, int32_t* out);
bool validateFloat(const Field& f, const char* val, float* out);
bool validateEnum(const Field& f, const char* val);
bool validateField(const Schema& s, const Field& f, const char* val, String* err);

}  // namespace config_portal
