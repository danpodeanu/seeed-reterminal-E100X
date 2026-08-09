#include "epub_archive.h"

#include <esp_heap_caps.h>

#include <string>

#include "epub_text.h"

namespace {

constexpr size_t kMaximumContainerBytes = 64 * 1024;
constexpr size_t kMaximumPackageBytes = 256 * 1024;

String vfsPath(const String& sdPath) {
  return sdPath.startsWith("/") ? String("/sd") + sdPath
                                : String("/sd/") + sdPath;
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

void EpubArchive::setError(const char* message) { error_ = message; }

void EpubArchive::close() {
  if (open_) mz_zip_reader_end(&archive_);
  archive_ = {};
  open_ = false;
  sdPath_ = "";
  title_ = "";
  error_ = "";
  chapterCount_ = 0;
  for (String& item : spine_) item = "";
}

bool EpubArchive::extract(const String& archivePath, std::string& output,
                          size_t maximumBytes) {
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
  const size_t length = static_cast<size_t>(stat.m_uncomp_size);
  char* buffer = static_cast<char*>(
      heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) buffer = static_cast<char*>(malloc(length + 1));
  if (buffer == nullptr) {
    setError("Not enough memory for EPUB chapter");
    return false;
  }
  const bool extracted =
      mz_zip_reader_extract_to_mem(&archive_, fileIndex, buffer, length, 0);
  if (!extracted) {
    free(buffer);
    setError("Could not decompress EPUB entry");
    return false;
  }
  buffer[length] = '\0';
  output.assign(buffer, length);
  free(buffer);
  return true;
}

bool EpubArchive::parsePackage(const std::string& package,
                               const String& packagePath) {
  const std::string parsedTitle = epub_text::elementText(package, "dc:title");
  title_ = parsedTitle.empty() ? "Untitled EPUB" : parsedTitle.c_str();

  ManifestItem manifest[kMaximumSpineItems] = {};
  int manifestCount = 0;
  size_t offset = 0;
  while (manifestCount < kMaximumSpineItems) {
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
         mediaType != "text/html")) {
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

bool EpubArchive::open(const String& sdPath) {
  close();
  const String hostPath = vfsPath(sdPath);
  if (!mz_zip_reader_init_file(&archive_, hostPath.c_str(), 0)) {
    setError("Could not open EPUB archive");
    return false;
  }
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

bool EpubArchive::loadChapter(int index, String& text) {
  text = "";
  if (!open_ || index < 0 || index >= chapterCount_) {
    setError("EPUB chapter is out of range");
    return false;
  }
  std::string html;
  if (!extract(spine_[index], html, kMaximumChapterBytes)) return false;
  const std::string plain =
      epub_text::htmlToPlainText(html.data(), html.size(), kMaximumChapterBytes);
  if (plain.empty()) {
    setError("EPUB chapter contains no readable text");
    return false;
  }
  text = String(plain.c_str());
  return true;
}
