#include "epub_archive.h"

#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "app_logger.h"
#include "epub_cover.h"
#include "epub_text.h"
#include "sd_card.h"

namespace {

constexpr size_t kMaximumContainerBytes = 64 * 1024;
constexpr size_t kMaximumPackageBytes = 256 * 1024;
constexpr size_t kArchiveReadChunkBytes = 4096;
uint8_t archiveReadBuffer[kArchiveReadChunkBytes];

size_t allocationBytes(size_t items, size_t size) {
  if (items != 0 && size > std::numeric_limits<size_t>::max() / items) {
    return 0;
  }
  return items * size;
}

void* epubAllocate(void*, size_t items, size_t size) {
  const size_t bytes = allocationBytes(items, size);
  if (bytes == 0) return nullptr;
  void* memory =
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) memory = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  return memory;
}

void epubFree(void*, void* address) { heap_caps_free(address); }

void* epubReallocate(void*, void* address, size_t items, size_t size) {
  const size_t bytes = allocationBytes(items, size);
  if (bytes == 0) {
    heap_caps_free(address);
    return nullptr;
  }
  void* memory = heap_caps_realloc(
      address, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory == nullptr) {
    memory = heap_caps_realloc(address, bytes, MALLOC_CAP_8BIT);
  }
  return memory;
}

size_t readArchiveFile(void* opaque, mz_uint64 offset, void* buffer,
                       size_t bytes) {
  File* file = static_cast<File*>(opaque);
  if (file == nullptr || !*file ||
      offset > std::numeric_limits<uint32_t>::max()) {
    return 0;
  }
  if (file->position() != static_cast<size_t>(offset) &&
      !file->seek(static_cast<uint32_t>(offset), fs::SeekSet)) {
    return 0;
  }
  uint8_t* destination = static_cast<uint8_t*>(buffer);
  size_t totalRead = 0;
  while (totalRead < bytes) {
    const size_t requested =
        std::min(kArchiveReadChunkBytes, bytes - totalRead);
    const size_t received = file->read(archiveReadBuffer, requested);
    if (received == 0) break;
    memcpy(destination + totalRead, archiveReadBuffer, received);
    totalRead += received;
    if (received < requested) break;
  }
  return totalRead;
}

String archiveBaseName(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

File openArchiveByTraversal(const String& path) {
  if (path.isEmpty() || path == "/") return {};
  File current = sd_card::openForRead("/");
  if (!current || !current.isDirectory()) {
    if (current) current.close();
    return {};
  }

  size_t componentStart = path[0] == '/' ? 1 : 0;
  while (componentStart < path.length()) {
    int slash = path.indexOf('/', componentStart);
    if (slash < 0) slash = path.length();
    const String component = path.substring(componentStart, slash);
    if (component.isEmpty()) {
      componentStart = static_cast<size_t>(slash) + 1;
      continue;
    }

    File match;
    File candidate = current.openNextFile();
    while (candidate) {
      if (archiveBaseName(candidate.name()) == component) {
        match = candidate;
        break;
      }
      candidate.close();
      candidate = current.openNextFile();
    }
    current.close();
    if (!match) return {};

    const bool finalComponent =
        static_cast<size_t>(slash) >= path.length();
    if (finalComponent) {
      if (match.isDirectory()) {
        match.close();
        return {};
      }
      return match;
    }
    if (!match.isDirectory()) {
      match.close();
      return {};
    }
    current = match;
    componentStart = static_cast<size_t>(slash) + 1;
  }
  current.close();
  return {};
}

std::string nextTag(const std::string& xml, const char* name, size_t& offset) {
  const size_t start = epub_text::findStartTag(xml, name, offset);
  if (start == std::string::npos) return {};
  const size_t end = xml.find('>', start + 1);
  if (end == std::string::npos) return {};
  offset = end + 1;
  return xml.substr(start + 1, end - start - 1);
}

}  // namespace

EpubChapterText::~EpubChapterText() { clear(); }

EpubChapterText::EpubChapterText(EpubChapterText&& other) noexcept
    : data_(other.data_), length_(other.length_) {
  other.data_ = nullptr;
  other.length_ = 0;
}

EpubChapterText& EpubChapterText::operator=(
    EpubChapterText&& other) noexcept {
  if (this == &other) return *this;
  clear();
  data_ = other.data_;
  length_ = other.length_;
  other.data_ = nullptr;
  other.length_ = 0;
  return *this;
}

void EpubChapterText::clear() {
  heap_caps_free(data_);
  data_ = nullptr;
  length_ = 0;
}

void EpubChapterText::adopt(char* data, size_t length) {
  clear();
  data_ = data;
  length_ = length;
}

EpubCoverData::~EpubCoverData() { clear(); }

void EpubCoverData::clear() {
  heap_caps_free(data_);
  data_ = nullptr;
  length_ = 0;
  nameHint_ = "";
}

void EpubCoverData::adopt(uint8_t* data, size_t length,
                          const String& nameHint) {
  clear();
  data_ = data;
  length_ = length;
  nameHint_ = nameHint;
}

void EpubArchive::setError(const char* message) { error_ = message; }

void EpubArchive::close() {
  if (open_) mz_zip_reader_end(&archive_);
  if (archiveFile_) archiveFile_.close();
  archive_ = {};
  open_ = false;
  sdPath_ = "";
  title_ = "";
  coverPath_ = "";
  error_ = "";
  chapterCount_ = 0;
  for (String& item : spine_) item = "";
}

bool EpubArchive::extract(const String& archivePath, std::string& output,
                          size_t maximumBytes) {
  char* buffer = nullptr;
  size_t length = 0;
  if (!extractBuffer(archivePath, buffer, length, maximumBytes)) return false;
  output.assign(buffer, length);
  heap_caps_free(buffer);
  return true;
}

bool EpubArchive::extractBuffer(const String& archivePath, char*& output,
                                size_t& length, size_t maximumBytes) {
  output = nullptr;
  length = 0;
  const int fileIndex =
      mz_zip_reader_locate_file(&archive_, archivePath.c_str(), nullptr, 0);
  if (fileIndex < 0) {
    setError("EPUB entry not found");
    return false;
  }
  mz_zip_archive_file_stat stat = {};
  if (!mz_zip_reader_file_stat(&archive_, fileIndex, &stat) ||
      stat.m_uncomp_size == 0 || stat.m_uncomp_size > maximumBytes) {
    setError("EPUB entry is empty or too large");
    return false;
  }
  length = static_cast<size_t>(stat.m_uncomp_size);
  char* buffer = static_cast<char*>(
      heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    buffer = static_cast<char*>(
        heap_caps_malloc(length + 1, MALLOC_CAP_8BIT));
  }
  if (buffer == nullptr) {
    setError("Not enough memory for EPUB entry");
    return false;
  }
  const bool extracted =
      mz_zip_reader_extract_to_mem(&archive_, fileIndex, buffer, length, 0);
  if (!extracted) {
    heap_caps_free(buffer);
    setError("Could not decompress EPUB entry");
    return false;
  }
  buffer[length] = '\0';
  output = buffer;
  return true;
}

bool EpubArchive::parsePackage(const std::string& package,
                               const String& packagePath) {
  const std::string parsedTitle = epub_text::elementText(package, "dc:title");
  title_ = parsedTitle.empty() ? "Untitled EPUB" : parsedTitle.c_str();
  coverPath_ =
      epub_cover::findCoverPath(package, packagePath.c_str()).c_str();

  ManifestItem manifest[kMaximumSpineItems] = {};
  int manifestCount = 0;
  size_t offset = 0;
  while (true) {
    std::string tag = nextTag(package, "item", offset);
    if (tag.empty()) break;
    if (tag.size() > 4 && tag[4] == 'r') continue;
    const std::string id =
        epub_text::attribute(tag.data(), tag.size(), "id");
    const std::string href =
        epub_text::attribute(tag.data(), tag.size(), "href");
    const std::string mediaType =
        epub_text::attribute(tag.data(), tag.size(), "media-type");
    if (id.empty() || href.empty() ||
        (mediaType != "application/xhtml+xml" &&
         mediaType != "text/html") ||
        manifestCount >= kMaximumSpineItems) {
      continue;
    }
    manifest[manifestCount].id = id.c_str();
    manifest[manifestCount].href =
        epub_text::resolvePath(packagePath.c_str(), href).c_str();
    ++manifestCount;
  }

  offset = 0;
  chapterCount_ = 0;
  while (chapterCount_ < kMaximumSpineItems) {
    const std::string tag = nextTag(package, "itemref", offset);
    if (tag.empty()) break;
    const std::string idref =
        epub_text::attribute(tag.data(), tag.size(), "idref");
    const std::string linear =
        epub_text::attribute(tag.data(), tag.size(), "linear");
    if (idref.empty() || linear == "no") continue;
    for (int index = 0; index < manifestCount; ++index) {
      if (manifest[index].id == idref.c_str()) {
        spine_[chapterCount_++] = manifest[index].href;
        break;
      }
    }
  }
  if (chapterCount_ == 0) {
    setError("EPUB reading order is empty");
    return false;
  }
  return true;
}

bool EpubArchive::loadCover(EpubCoverData& cover) {
  cover.clear();
  if (!open_ || coverPath_.isEmpty()) {
    setError("EPUB cover is not available");
    return false;
  }
  char* data = nullptr;
  size_t length = 0;
  if (!extractBuffer(coverPath_, data, length, kMaximumCoverBytes)) {
    return false;
  }
  cover.adopt(reinterpret_cast<uint8_t*>(data), length, coverPath_);
  return true;
}

bool EpubArchive::open(const String& sdPath) {
  close();
  LOG.printf("[games] resolving EPUB path (%u bytes)\n",
             static_cast<unsigned>(sdPath.length()));
  LOG.flush();
  archiveFile_ = openArchiveByTraversal(sdPath);
  if (!archiveFile_) {
    setError("Could not open EPUB file");
    return false;
  }
  LOG.printf("[games] EPUB file opened: %llu bytes\n",
             static_cast<unsigned long long>(archiveFile_.size()));
  LOG.flush();
  archive_.m_pAlloc = epubAllocate;
  archive_.m_pFree = epubFree;
  archive_.m_pRealloc = epubReallocate;
  archive_.m_pRead = readArchiveFile;
  archive_.m_pIO_opaque = &archiveFile_;
  if (!mz_zip_reader_init(
          &archive_, archiveFile_.size(),
          MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
    archiveFile_.close();
    setError("Could not open EPUB archive");
    return false;
  }
  LOG.printf("[games] EPUB ZIP index ready: %u entries\n",
             mz_zip_reader_get_num_files(&archive_));
  LOG.flush();
  open_ = true;
  sdPath_ = sdPath;

  std::string container;
  if (!extract("META-INF/container.xml", container,
               kMaximumContainerBytes)) {
    close();
    setError("EPUB container.xml is missing");
    return false;
  }
  const size_t rootfile = epub_text::findStartTag(container, "rootfile");
  if (rootfile == std::string::npos) {
    close();
    setError("EPUB package location is missing");
    return false;
  }
  const size_t rootfileEnd = container.find('>', rootfile);
  if (rootfileEnd == std::string::npos) {
    close();
    setError("EPUB container.xml is invalid");
    return false;
  }
  const std::string packagePath = epub_text::attribute(
      container.data() + rootfile + 1, rootfileEnd - rootfile - 1,
      "full-path");
  if (packagePath.empty()) {
    close();
    setError("EPUB package path is empty");
    return false;
  }

  std::string package;
  if (!extract(packagePath.c_str(), package, kMaximumPackageBytes) ||
      !parsePackage(package, packagePath.c_str())) {
    const String savedError = error_;
    close();
    error_ = savedError;
    return false;
  }
  return true;
}

bool EpubArchive::loadChapter(int index, EpubChapterText& text) {
  text.clear();
  if (!open_ || index < 0 || index >= chapterCount_) {
    setError("EPUB chapter is out of range");
    return false;
  }
  char* html = nullptr;
  size_t htmlLength = 0;
  if (!extractBuffer(spine_[index], html, htmlLength,
                     kMaximumChapterBytes)) {
    return false;
  }
  size_t plainLength = epub_text::htmlToPlainTextInPlace(
      html, htmlLength, kMaximumChapterBytes, true);
  plainLength = epub_text::normalizeTypographyInPlace(html, plainLength);
  if (plainLength == 0) {
    heap_caps_free(html);
    setError("EPUB chapter contains no readable text");
    return false;
  }
  text.adopt(html, plainLength);
  return true;
}
