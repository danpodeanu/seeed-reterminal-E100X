#pragma once

#include <Arduino.h>

#include <string>

#include "miniz.h"

class EpubArchive {
 public:
  static constexpr int kMaximumSpineItems = 96;
  static constexpr size_t kMaximumChapterBytes = 512 * 1024;

  EpubArchive() = default;
  ~EpubArchive() { close(); }

  bool open(const String& sdPath);
  void close();
  bool loadChapter(int index, String& text);

  bool isOpen() const { return open_; }
  int chapterCount() const { return chapterCount_; }
  const String& title() const { return title_; }
  const String& path() const { return sdPath_; }
  const String& error() const { return error_; }

 private:
  struct ManifestItem {
    String id;
    String href;
  };

  mz_zip_archive archive_ = {};
  bool open_ = false;
  String sdPath_;
  String title_;
  String error_;
  String spine_[kMaximumSpineItems];
  int chapterCount_ = 0;

  bool extract(const String& archivePath, std::string& output,
               size_t maximumBytes);
  bool parsePackage(const std::string& package, const String& packagePath);
  void setError(const char* message);
};
