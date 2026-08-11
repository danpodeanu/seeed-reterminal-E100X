#pragma once

#include <string>

#include "epub_text.h"

namespace epub_cover {

inline std::string nextTag(const std::string& xml, const char* name,
                           size_t& offset) {
  const size_t start = epub_text::findStartTag(xml, name, offset);
  if (start == std::string::npos) return {};
  const size_t end = xml.find('>', start + 1);
  if (end == std::string::npos) return {};
  offset = end + 1;
  return xml.substr(start + 1, end - start - 1);
}

inline bool tokenListContains(const std::string& tokens,
                              const char* requested) {
  size_t offset = 0;
  while (offset < tokens.length()) {
    while (offset < tokens.length() &&
           (tokens[offset] == ' ' || tokens[offset] == '\t' ||
            tokens[offset] == '\r' || tokens[offset] == '\n')) {
      ++offset;
    }
    const size_t start = offset;
    while (offset < tokens.length() && tokens[offset] != ' ' &&
           tokens[offset] != '\t' && tokens[offset] != '\r' &&
           tokens[offset] != '\n') {
      ++offset;
    }
    if (tokens.compare(start, offset - start, requested) == 0) return true;
  }
  return false;
}

inline bool supportedMediaType(const std::string& mediaType) {
  return mediaType == "image/jpeg" || mediaType == "image/png";
}

inline std::string declaredCoverId(const std::string& package) {
  size_t offset = 0;
  while (true) {
    const std::string tag = nextTag(package, "meta", offset);
    if (tag.empty()) return {};
    if (epub_text::attribute(tag.data(), tag.size(), "name") == "cover") {
      return epub_text::attribute(tag.data(), tag.size(), "content");
    }
  }
}

inline std::string findCoverPath(const std::string& package,
                                 const char* packagePath) {
  const std::string epub2CoverId = declaredCoverId(package);
  std::string epub2Candidate;
  size_t offset = 0;
  while (true) {
    const std::string tag = nextTag(package, "item", offset);
    if (tag.empty()) break;
    const std::string id =
        epub_text::attribute(tag.data(), tag.size(), "id");
    const std::string href =
        epub_text::attribute(tag.data(), tag.size(), "href");
    const std::string mediaType =
        epub_text::attribute(tag.data(), tag.size(), "media-type");
    if (href.empty() || !supportedMediaType(mediaType)) continue;

    const std::string resolved =
        epub_text::resolvePath(packagePath, href);
    const std::string properties =
        epub_text::attribute(tag.data(), tag.size(), "properties");
    if (tokenListContains(properties, "cover-image")) return resolved;
    if (!epub2CoverId.empty() && id == epub2CoverId) {
      epub2Candidate = resolved;
    }
  }
  return epub2Candidate;
}

}  // namespace epub_cover
