#pragma once

#include "config_portal_string_shim.h"
#include "config_schema.h"

#include <map>
#include <stdint.h>
#include <utility>
#include <vector>

#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace config_portal {
namespace storage {

// Storage abstraction around Preferences/NVS. Callers can supply a fake
// implementation in host tests without pulling Arduino headers into the
// schema or storage algorithms.
class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool begin(const char* ns, bool readOnly) = 0;
  virtual void end() = 0;
  virtual bool has(const char* key) = 0;
  virtual String getString(const char* key, const char* defaultVal) = 0;
  virtual bool putString(const char* key, const char* value) = 0;
  virtual bool remove(const char* key) = 0;
  virtual bool clear() = 0;
};

#ifdef ARDUINO
class PrefsStorage : public Storage {
 public:
  bool begin(const char* ns, bool readOnly) override;
  void end() override;
  bool has(const char* key) override;
  String getString(const char* key, const char* defaultVal) override;
  bool putString(const char* key, const char* value) override;
  bool remove(const char* key) override;
  bool clear() override;

 private:
  Preferences prefs_;
};
#endif

void loadForGet(Storage& s, const Schema& schema,
                std::vector<std::pair<String, String>>& out);

bool save(Storage& s, const Schema& schema,
          const std::map<String, String>& submitted, String* err);

String  getString(Storage& s, const Schema& schema, const char* key);
int32_t getInt   (Storage& s, const Schema& schema, const char* key);
bool    getBool  (Storage& s, const Schema& schema, const char* key);
float   getFloat (Storage& s, const Schema& schema, const char* key);

}  // namespace storage
}  // namespace config_portal
