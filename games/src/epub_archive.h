#pragma once

#include <Arduino.h>
#include <FS.h>

#include <string>

#include "miniz.h"

class EpubChapterText {
 public:
  EpubChapterText() = default;
  ~EpubChapterText();
  EpubChapterText(const EpubChapterText&) = delete;
  EpubChapterText& operator=(const EpubChapterText&) = delete;
  EpubChapterText(EpubChapterText&& other) noexcept;
  EpubChapterText& operator=(EpubChapterText&& other) noexcept;

  const char* c_str() const { return data_ == nullptr ? "" : data_; }
  size_t length() const { return length_; }
  bool empty() const { return length_ == 0; }
  void clear();

 private:
  friend class EpubArchive;

  char* data_ = nullptr;
  size_t length_ = 0;

  void adopt(char* data, size_t length);
};

class EpubCoverData {
 public:
  EpubCoverData() = default;
  ~EpubCoverData();
  EpubCoverData(const EpubCoverData&) = delete;
  EpubCoverData& operator=(const EpubCoverData&) = delete;

  const uint8_t* data() const { return data_; }
  size_t length() const { return length_; }
  const String& nameHint() const { return nameHint_; }
  bool empty() const { return length_ == 0; }
  void clear();

 private:
  friend class EpubArchive;

  uint8_t* data_ = nullptr;
  size_t length_ = 0;
  String nameHint_;

  void adopt(uint8_t* data, size_t length, const String& nameHint);
};

class EpubArchive {
 public:
  static constexpr int kMaximumSpineItems = 96;
  static constexpr size_t kMaximumChapterBytes = 512 * 1024;
  static constexpr size_t kMaximumCoverBytes = 2 * 1024 * 1024;

  EpubArchive() = default;
  ~EpubArchive() { close(); }

  bool open(const String& sdPath);
  void close();
  bool loadChapter(int index, EpubChapterText& text);
  bool loadCover(EpubCoverData& cover);

  bool isOpen() const { return open_; }
  int chapterCount() const { return chapterCount_; }
  bool hasCover() const { return !coverPath_.isEmpty(); }
  const String& title() const { return title_; }
  const String& path() const { return sdPath_; }
  const String& error() const { return error_; }

 private:
  struct ManifestItem {
    String id;
    String href;
  };

  mz_zip_archive archive_ = {};
  File archiveFile_;
  bool open_ = false;
  String sdPath_;
  String title_;
  String coverPath_;
  String error_;
  String spine_[kMaximumSpineItems];
  int chapterCount_ = 0;

  bool extract(const String& archivePath, std::string& output,
               size_t maximumBytes);
  bool extractBuffer(const String& archivePath, char*& output, size_t& length,
                     size_t maximumBytes);
  bool parsePackage(const std::string& package, const String& packagePath);
  void setError(const char* message);
};
