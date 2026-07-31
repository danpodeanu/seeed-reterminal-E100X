#pragma once

#include "config_storage.h"

#include <string>
#include <unordered_map>

class FakeStorage : public config_portal::storage::Storage {
 public:
  bool begin(const char* ns, bool readOnly) override {
    ns_ = ns ? ns : "";
    readOnly_ = readOnly;
    begun_ = true;
    return beginOk_;
  }
  void end() override { begun_ = false; }
  bool has(const char* key) override { return data_.find(key ? key : "") != data_.end(); }
  String getString(const char* key, const char* defaultVal) override {
    auto it = data_.find(key ? key : "");
    return it == data_.end() ? String(defaultVal ? defaultVal : "") : it->second;
  }
  bool putString(const char* key, const char* value) override {
    if (readOnly_) return false;
    ++putCount;
    data_[key ? key : ""] = value ? value : "";
    return true;
  }
  bool remove(const char* key) override {
    if (readOnly_) return false;
    ++removeCount;
    data_.erase(key ? key : "");
    return true;
  }
  bool clear() override {
    if (readOnly_) return false;
    data_.clear();
    return true;
  }

  std::unordered_map<std::string, std::string> data_;
  std::string ns_;
  bool readOnly_ = false;
  bool begun_ = false;
  bool beginOk_ = true;
  int putCount = 0;
  int removeCount = 0;
};
