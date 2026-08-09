#pragma once

#include <Arduino.h>

class SdReadonlyBrowser {
 public:
  static constexpr int kMaximumEntries = 96;

  struct Entry {
    String name;
    String path;
    uint64_t size;
    bool directory;
    bool epub;
  };

  bool open(const String& path);
  const String& path() const { return path_; }
  int count() const { return count_; }
  bool truncated() const { return truncated_; }
  const Entry* entry(int index) const {
    return index >= 0 && index < count_ ? &entries_[index] : nullptr;
  }
  static String parentPath(const String& path);

 private:
  String path_ = "/";
  Entry entries_[kMaximumEntries] = {};
  int count_ = 0;
  bool truncated_ = false;

  static bool isEpub(const String& name);
  static bool precedes(const Entry& left, const Entry& right);
};
