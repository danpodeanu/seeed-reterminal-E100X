#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace epub_text {

inline bool startsWithIgnoreCase(const char* value, size_t length,
                                 const char* expected) {
  size_t index = 0;
  while (expected[index] != '\0') {
    if (index >= length ||
        tolower(static_cast<unsigned char>(value[index])) !=
            tolower(static_cast<unsigned char>(expected[index]))) {
      return false;
    }
    ++index;
  }
  return true;
}

inline void appendUtf8(std::string& output, uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0x10FFFF) {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

inline bool decodeEntity(const char* value, size_t length,
                         std::string& output) {
  if (length == 3 && startsWithIgnoreCase(value, length, "amp")) {
    output.push_back('&');
  } else if (length == 2 && startsWithIgnoreCase(value, length, "lt")) {
    output.push_back('<');
  } else if (length == 2 && startsWithIgnoreCase(value, length, "gt")) {
    output.push_back('>');
  } else if (length == 4 && startsWithIgnoreCase(value, length, "quot")) {
    output.push_back('"');
  } else if (length == 4 && startsWithIgnoreCase(value, length, "apos")) {
    output.push_back('\'');
  } else if (length == 4 && startsWithIgnoreCase(value, length, "nbsp")) {
    output.push_back(' ');
  } else if (length > 1 && value[0] == '#') {
    uint32_t codepoint = 0;
    size_t index = 1;
    int base = 10;
    if (index < length && (value[index] == 'x' || value[index] == 'X')) {
      base = 16;
      ++index;
    }
    if (index == length) return false;
    for (; index < length; ++index) {
      const char character = value[index];
      int digit = -1;
      if (character >= '0' && character <= '9') {
        digit = character - '0';
      } else if (base == 16 && character >= 'a' && character <= 'f') {
        digit = character - 'a' + 10;
      } else if (base == 16 && character >= 'A' && character <= 'F') {
        digit = character - 'A' + 10;
      }
      if (digit < 0 || digit >= base) return false;
      codepoint = codepoint * base + static_cast<uint32_t>(digit);
      if (codepoint > 0x10FFFF) return false;
    }
    appendUtf8(output, codepoint);
  } else {
    return false;
  }
  return true;
}

inline bool blockTag(const char* tag, size_t length) {
  static constexpr const char* kTags[] = {
      "br",  "p",   "/p",  "div", "/div", "li", "/li", "h1", "/h1",
      "h2",  "/h2", "h3",  "/h3", "h4",   "/h4", "h5", "/h5", "h6",
      "/h6", "tr",  "/tr", "hr",  "blockquote", "/blockquote",
  };
  while (length > 0 && isspace(static_cast<unsigned char>(*tag))) {
    ++tag;
    --length;
  }
  for (const char* expected : kTags) {
    const size_t expectedLength = std::char_traits<char>::length(expected);
    if (length >= expectedLength &&
        startsWithIgnoreCase(tag, length, expected) &&
        (length == expectedLength ||
         isspace(static_cast<unsigned char>(tag[expectedLength])) ||
         tag[expectedLength] == '/' || tag[expectedLength] == '>')) {
      return true;
    }
  }
  return false;
}

inline void appendSpace(std::string& output) {
  if (!output.empty() && output.back() != ' ' && output.back() != '\n') {
    output.push_back(' ');
  }
}

inline void appendParagraph(std::string& output) {
  while (!output.empty() && output.back() == ' ') output.pop_back();
  if (output.empty() || output.back() == '\n') return;
  output.push_back('\n');
}

inline std::string htmlToPlainText(const char* html, size_t length,
                                   size_t maximumOutput = 512 * 1024) {
  std::string output;
  output.reserve(length < maximumOutput ? length : maximumOutput);
  bool inScript = false;
  bool inStyle = false;

  for (size_t index = 0; index < length && output.size() < maximumOutput;) {
    if (html[index] == '<') {
      size_t close = index + 1;
      while (close < length && html[close] != '>') ++close;
      if (close == length) break;
      const size_t tagLength = close - index - 1;
      const char* tag = html + index + 1;
      if (startsWithIgnoreCase(tag, tagLength, "script")) inScript = true;
      if (startsWithIgnoreCase(tag, tagLength, "/script")) inScript = false;
      if (startsWithIgnoreCase(tag, tagLength, "style")) inStyle = true;
      if (startsWithIgnoreCase(tag, tagLength, "/style")) inStyle = false;
      if (!inScript && !inStyle && blockTag(tag, tagLength)) {
        appendParagraph(output);
      }
      index = close + 1;
      continue;
    }
    if (inScript || inStyle) {
      ++index;
      continue;
    }
    if (html[index] == '&') {
      size_t semicolon = index + 1;
      while (semicolon < length && semicolon - index <= 16 &&
             html[semicolon] != ';') {
        ++semicolon;
      }
      if (semicolon < length && html[semicolon] == ';' &&
          decodeEntity(html + index + 1, semicolon - index - 1, output)) {
        index = semicolon + 1;
        continue;
      }
    }
    const unsigned char character = static_cast<unsigned char>(html[index++]);
    if (character == '\r' || character == '\n' || character == '\t' ||
        character == '\f') {
      appendSpace(output);
    } else if (character < 0x20) {
      continue;
    } else if (character == ' ') {
      appendSpace(output);
    } else {
      output.push_back(static_cast<char>(character));
    }
  }

  while (!output.empty() &&
         (output.back() == ' ' || output.back() == '\n')) {
    output.pop_back();
  }
  return output;
}

inline int hexadecimalDigit(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

inline std::string percentDecode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size()) {
      const int high = hexadecimalDigit(value[index + 1]);
      const int low = hexadecimalDigit(value[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        continue;
      }
    }
    decoded.push_back(value[index]);
  }
  return decoded;
}

inline std::string attribute(const char* tag, size_t length,
                             const char* name) {
  const size_t nameLength = std::char_traits<char>::length(name);
  size_t index = 0;
  while (index + nameLength < length) {
    while (index < length &&
           isspace(static_cast<unsigned char>(tag[index]))) {
      ++index;
    }
    const size_t start = index;
    while (index < length &&
           (isalnum(static_cast<unsigned char>(tag[index])) ||
            tag[index] == ':' || tag[index] == '-' || tag[index] == '_')) {
      ++index;
    }
    const size_t afterCandidate = index;
    const size_t candidateLength = index - start;
    while (index < length &&
           isspace(static_cast<unsigned char>(tag[index]))) {
      ++index;
    }
    if (index >= length || tag[index] != '=') {
      index = afterCandidate > start ? afterCandidate : start + 1;
      continue;
    }
    ++index;
    while (index < length &&
           isspace(static_cast<unsigned char>(tag[index]))) {
      ++index;
    }
    if (index >= length || (tag[index] != '"' && tag[index] != '\'')) {
      continue;
    }
    const char quote = tag[index++];
    const size_t valueStart = index;
    while (index < length && tag[index] != quote) ++index;
    if (candidateLength == nameLength &&
        startsWithIgnoreCase(tag + start, candidateLength, name)) {
      return std::string(tag + valueStart, index - valueStart);
    }
    if (index < length) ++index;
  }
  return {};
}

inline size_t findStartTag(const std::string& xml, const char* name,
                           size_t offset = 0) {
  std::string marker = "<";
  marker += name;
  while (offset < xml.size()) {
    const size_t start = xml.find(marker, offset);
    if (start == std::string::npos) return start;
    const size_t afterName = start + marker.size();
    if (afterName < xml.size() &&
        (isspace(static_cast<unsigned char>(xml[afterName])) ||
         xml[afterName] == '>' || xml[afterName] == '/')) {
      return start;
    }
    offset = afterName;
  }
  return std::string::npos;
}

inline std::string elementText(const std::string& xml, const char* name) {
  size_t start = findStartTag(xml, name);
  if (start == std::string::npos) return {};
  start = xml.find('>', start + 1);
  if (start == std::string::npos) return {};
  std::string close = "</";
  close += name;
  close += ">";
  const size_t end = xml.find(close, start + 1);
  if (end == std::string::npos) return {};
  return htmlToPlainText(xml.data() + start + 1, end - start - 1, 4096);
}

inline std::string resolvePath(const std::string& baseFile,
                               const std::string& reference) {
  std::string clean = percentDecode(reference);
  const size_t fragment = clean.find_first_of("?#");
  if (fragment != std::string::npos) clean.resize(fragment);
  std::string path;
  if (!clean.empty() && clean[0] == '/') {
    path = clean.substr(1);
  } else {
    const size_t slash = baseFile.find_last_of('/');
    if (slash != std::string::npos) path = baseFile.substr(0, slash + 1);
    path += clean;
  }

  std::string normalized;
  size_t index = 0;
  while (index <= path.size()) {
    const size_t slash = path.find('/', index);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    const std::string part = path.substr(index, end - index);
    if (part == "..") {
      const size_t previous = normalized.find_last_of('/');
      normalized =
          previous == std::string::npos ? "" : normalized.substr(0, previous);
    } else if (!part.empty() && part != ".") {
      if (!normalized.empty()) normalized.push_back('/');
      normalized += part;
    }
    if (slash == std::string::npos) break;
    index = slash + 1;
  }
  return normalized;
}

struct TextPage {
  size_t start = 0;
  size_t end = 0;
  std::vector<std::string> lines;
};

inline size_t utf8CharacterBytes(const char* text, size_t length,
                                 size_t offset) {
  if (offset >= length) return 0;
  const uint8_t first = static_cast<uint8_t>(text[offset]);
  size_t bytes = 1;
  if ((first & 0xE0) == 0xC0) {
    bytes = 2;
  } else if ((first & 0xF0) == 0xE0) {
    bytes = 3;
  } else if ((first & 0xF8) == 0xF0) {
    bytes = 4;
  }
  if (offset + bytes > length) return 1;
  for (size_t index = 1; index < bytes; ++index) {
    if ((static_cast<uint8_t>(text[offset + index]) & 0xC0) != 0x80) return 1;
  }
  return bytes;
}

inline TextPage paginate(const char* text, size_t length, size_t requestedStart,
                         size_t maximumColumns, size_t maximumLines) {
  TextPage page;
  page.start = requestedStart < length ? requestedStart : length;
  page.end = page.start;
  if (text == nullptr || maximumColumns == 0 || maximumLines == 0) return page;

  while (page.start > 0 && page.start < length &&
         (static_cast<uint8_t>(text[page.start]) & 0xC0) == 0x80) {
    --page.start;
  }
  size_t offset = page.start;
  page.lines.reserve(maximumLines);
  while (offset < length && page.lines.size() < maximumLines) {
    if (text[offset] == '\n') {
      page.lines.emplace_back();
      ++offset;
      continue;
    }

    const size_t lineStart = offset;
    size_t lineEnd = offset;
    size_t lastSpace = std::string::npos;
    size_t columns = 0;
    bool endedAtNewline = false;
    while (lineEnd < length && columns < maximumColumns) {
      if (text[lineEnd] == '\n') {
        endedAtNewline = true;
        break;
      }
      if (text[lineEnd] == ' ') lastSpace = lineEnd;
      lineEnd += utf8CharacterBytes(text, length, lineEnd);
      ++columns;
    }

    size_t next = lineEnd;
    if (endedAtNewline) {
      next = lineEnd + 1;
    } else if (lineEnd < length && text[lineEnd] == ' ') {
      next = lineEnd + 1;
      while (next < length && text[next] == ' ') ++next;
    } else if (lineEnd < length && lastSpace != std::string::npos) {
      lineEnd = lastSpace;
      next = lastSpace + 1;
      while (next < length && text[next] == ' ') ++next;
    }
    while (lineEnd > lineStart && text[lineEnd - 1] == ' ') --lineEnd;
    page.lines.emplace_back(text + lineStart, lineEnd - lineStart);
    offset = next > lineStart ? next : lineEnd;
  }
  page.end = offset;
  return page;
}

inline size_t previousPageStart(const char* text, size_t length,
                                size_t currentStart, size_t maximumColumns,
                                size_t maximumLines) {
  if (currentStart == 0) return 0;
  size_t offset = 0;
  size_t previous = 0;
  while (offset < currentStart) {
    const TextPage page =
        paginate(text, length, offset, maximumColumns, maximumLines);
    if (page.end >= currentStart || page.end <= offset) return previous;
    previous = offset;
    offset = page.end;
  }
  return previous;
}

}  // namespace epub_text
