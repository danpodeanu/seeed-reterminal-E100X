#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace epub_text {

enum class TextStyle : uint8_t {
  Regular = 0,
  Bold = 1,
  Italic = 2,
  BoldItalic = 3,
};

inline char styleMarker(TextStyle style) {
  return static_cast<char>(static_cast<uint8_t>(style) + 1);
}

inline bool isStyleMarker(char character) {
  const uint8_t value = static_cast<uint8_t>(character);
  return value >= 1 && value <= 4;
}

inline TextStyle styleFromMarker(char marker) {
  return static_cast<TextStyle>(static_cast<uint8_t>(marker) - 1);
}

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

inline bool tagNameEquals(const char* tag, size_t length, const char* expected,
                          bool closing) {
  size_t offset = 0;
  while (offset < length &&
         isspace(static_cast<unsigned char>(tag[offset]))) {
    ++offset;
  }
  const bool isClosing = offset < length && tag[offset] == '/';
  if (isClosing) ++offset;
  if (isClosing != closing) return false;

  const size_t nameLength = std::char_traits<char>::length(expected);
  if (offset + nameLength > length ||
      !startsWithIgnoreCase(tag + offset, length - offset, expected)) {
    return false;
  }
  const size_t afterName = offset + nameLength;
  return afterName == length ||
         isspace(static_cast<unsigned char>(tag[afterName])) ||
         tag[afterName] == '/';
}

inline bool headingTag(const char* tag, size_t length, bool closing) {
  return tagNameEquals(tag, length, "h1", closing) ||
         tagNameEquals(tag, length, "h2", closing) ||
         tagNameEquals(tag, length, "h3", closing) ||
         tagNameEquals(tag, length, "h4", closing) ||
         tagNameEquals(tag, length, "h5", closing) ||
         tagNameEquals(tag, length, "h6", closing);
}

inline TextStyle textStyle(int boldDepth, int italicDepth) {
  if (boldDepth > 0 && italicDepth > 0) return TextStyle::BoldItalic;
  if (boldDepth > 0) return TextStyle::Bold;
  if (italicDepth > 0) return TextStyle::Italic;
  return TextStyle::Regular;
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

inline void appendSpaceInPlace(char* output, size_t& length) {
  if (length > 0 && output[length - 1] != ' ' &&
      output[length - 1] != '\n') {
    output[length++] = ' ';
  }
}

inline void appendBreakInPlace(char* output, size_t& length,
                               size_t maximumOutput, size_t lineCount) {
  while (length > 0 && output[length - 1] == ' ') --length;
  if (length == 0) return;
  size_t trailingNewlines = 0;
  while (trailingNewlines < length &&
         output[length - trailingNewlines - 1] == '\n') {
    ++trailingNewlines;
  }
  while (trailingNewlines < lineCount && length < maximumOutput) {
    output[length++] = '\n';
    ++trailingNewlines;
  }
}

inline bool paragraphBreakTag(const char* tag, size_t length) {
  return tagNameEquals(tag, length, "p", true) ||
         tagNameEquals(tag, length, "div", true) ||
         headingTag(tag, length, true) ||
         tagNameEquals(tag, length, "blockquote", true);
}

inline size_t htmlToPlainTextInPlace(char* html, size_t length,
                                     size_t maximumOutput = 512 * 1024,
                                     bool preserveStyles = false) {
  if (html == nullptr || length == 0 || maximumOutput == 0) return 0;
  if (maximumOutput > length) maximumOutput = length;
  size_t outputLength = 0;
  bool inScript = false;
  bool inStyle = false;
  int boldDepth = 0;
  int italicDepth = 0;
  TextStyle emittedStyle = TextStyle::Regular;

  const auto emitStyle = [&]() {
    if (!preserveStyles || outputLength >= maximumOutput) return;
    const TextStyle desiredStyle = textStyle(boldDepth, italicDepth);
    if (desiredStyle != emittedStyle) {
      html[outputLength++] = styleMarker(desiredStyle);
      emittedStyle = desiredStyle;
    }
  };

  for (size_t index = 0;
       index < length && outputLength < maximumOutput;) {
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
      if (!inScript && !inStyle && preserveStyles) {
        if (tagNameEquals(tag, tagLength, "b", false) ||
            tagNameEquals(tag, tagLength, "strong", false) ||
            headingTag(tag, tagLength, false)) {
          ++boldDepth;
        } else if ((tagNameEquals(tag, tagLength, "b", true) ||
                    tagNameEquals(tag, tagLength, "strong", true) ||
                    headingTag(tag, tagLength, true)) &&
                   boldDepth > 0) {
          --boldDepth;
        }
        if (tagNameEquals(tag, tagLength, "i", false) ||
            tagNameEquals(tag, tagLength, "em", false)) {
          ++italicDepth;
        } else if ((tagNameEquals(tag, tagLength, "i", true) ||
                    tagNameEquals(tag, tagLength, "em", true)) &&
                   italicDepth > 0) {
          --italicDepth;
        }
      }
      if (!inScript && !inStyle && blockTag(tag, tagLength)) {
        appendBreakInPlace(html, outputLength, maximumOutput,
                           paragraphBreakTag(tag, tagLength) ? 2 : 1);
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
      if (semicolon < length && html[semicolon] == ';') {
        std::string decoded;
        if (decodeEntity(html + index + 1, semicolon - index - 1, decoded)) {
          if (decoded == " ") {
            appendSpaceInPlace(html, outputLength);
          } else {
            emitStyle();
            if (outputLength + decoded.length() > maximumOutput) break;
            for (char character : decoded) html[outputLength++] = character;
          }
          index = semicolon + 1;
          continue;
        }
      }
    }
    const unsigned char character = static_cast<unsigned char>(html[index++]);
    if (character == '\r' || character == '\n' || character == '\t' ||
        character == '\f') {
      appendSpaceInPlace(html, outputLength);
    } else if (character < 0x20) {
      continue;
    } else if (character == ' ') {
      appendSpaceInPlace(html, outputLength);
    } else {
      emitStyle();
      if (outputLength >= maximumOutput) break;
      html[outputLength++] = static_cast<char>(character);
    }
  }

  while (outputLength > 0 &&
         (html[outputLength - 1] == ' ' ||
          html[outputLength - 1] == '\n')) {
    --outputLength;
  }
  html[outputLength] = '\0';
  return outputLength;
}

inline std::string htmlToPlainText(const char* html, size_t length,
                                   size_t maximumOutput = 512 * 1024,
                                   bool preserveStyles = false) {
  if (html == nullptr || length == 0) return {};
  std::string output(html, length);
  output.resize(htmlToPlainTextInPlace(&output[0], output.length(),
                                      maximumOutput, preserveStyles));
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
  TextStyle initialStyle = TextStyle::Regular;
  TextStyle finalStyle = TextStyle::Regular;
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

inline uint32_t utf8Codepoint(const char* text, size_t length, size_t offset) {
  const size_t bytes = utf8CharacterBytes(text, length, offset);
  if (bytes == 0) return 0;
  const uint8_t first = static_cast<uint8_t>(text[offset]);
  if (bytes == 1) return first;
  uint32_t codepoint =
      first & static_cast<uint8_t>(bytes == 2 ? 0x1F : bytes == 3 ? 0x0F : 0x07);
  for (size_t index = 1; index < bytes; ++index) {
    codepoint =
        (codepoint << 6) | (static_cast<uint8_t>(text[offset + index]) & 0x3F);
  }
  return codepoint;
}

inline size_t normalizeTypographyInPlace(char* text, size_t length) {
  size_t readOffset = 0;
  size_t writeOffset = 0;
  while (readOffset < length) {
    if (isStyleMarker(text[readOffset])) {
      text[writeOffset++] = text[readOffset++];
      continue;
    }
    const size_t bytes = utf8CharacterBytes(text, length, readOffset);
    const uint32_t codepoint = utf8Codepoint(text, length, readOffset);
    const char* replacement = nullptr;
    size_t replacementLength = 0;
    switch (codepoint) {
      case 0x00A0:
        replacement = " ";
        replacementLength = 1;
        break;
      case 0x02BC:
      case 0x2018:
      case 0x2019:
        replacement = "'";
        replacementLength = 1;
        break;
      case 0x201C:
      case 0x201D:
        replacement = "\"";
        replacementLength = 1;
        break;
      case 0x2013:
      case 0x2014:
        replacement = "-";
        replacementLength = 1;
        break;
      case 0x2026:
        replacement = "...";
        replacementLength = 3;
        break;
      default:
        break;
    }
    if (replacement != nullptr) {
      for (size_t index = 0; index < replacementLength; ++index) {
        text[writeOffset++] = replacement[index];
      }
    } else {
      for (size_t index = 0; index < bytes; ++index) {
        text[writeOffset++] = text[readOffset + index];
      }
    }
    readOffset += bytes;
  }
  text[writeOffset] = '\0';
  return writeOffset;
}

inline bool isCjkCodepoint(uint32_t codepoint) {
  return (codepoint >= 0x1100 && codepoint <= 0x11FF) ||
         (codepoint >= 0x2E80 && codepoint <= 0xA4CF) ||
         (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
         (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
         (codepoint >= 0xFE10 && codepoint <= 0xFE19) ||
         (codepoint >= 0xFE30 && codepoint <= 0xFE6F) ||
         (codepoint >= 0xFF01 && codepoint <= 0xFF60) ||
         (codepoint >= 0xFFE0 && codepoint <= 0xFFE6);
}

inline bool requiresCjkFont(uint32_t codepoint) {
  return isCjkCodepoint(codepoint) ||
         (codepoint >= 0xFF61 && codepoint <= 0xFFDC);
}

inline size_t displayColumns(uint32_t codepoint) {
  if (codepoint >= 1 && codepoint <= 4) return 0;
  if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||
      (codepoint >= 0xFE00 && codepoint <= 0xFE0F)) {
    return 0;
  }
  return isCjkCodepoint(codepoint) ? 2 : 1;
}

inline bool containsCjk(const char* text, size_t length) {
  for (size_t offset = 0; offset < length;) {
    if (requiresCjkFont(utf8Codepoint(text, length, offset))) return true;
    offset += utf8CharacterBytes(text, length, offset);
  }
  return false;
}

inline TextStyle styleAt(const char* text, size_t offset) {
  TextStyle style = TextStyle::Regular;
  for (size_t index = 0; index < offset; ++index) {
    if (isStyleMarker(text[index])) style = styleFromMarker(text[index]);
  }
  return style;
}

inline TextStyle styleAfter(const char* text, size_t start, size_t end,
                            TextStyle initialStyle) {
  TextStyle style = initialStyle;
  for (size_t index = start; index < end; ++index) {
    if (isStyleMarker(text[index])) style = styleFromMarker(text[index]);
  }
  return style;
}

using CharacterWidth = size_t (*)(uint32_t codepoint, TextStyle style);

inline TextPage paginate(const char* text, size_t length, size_t requestedStart,
                         size_t maximumWidth, size_t maximumLines,
                         TextStyle initialStyle = TextStyle::Regular,
                         bool initialStyleKnown = false,
                         CharacterWidth characterWidth = nullptr) {
  TextPage page;
  page.start = requestedStart < length ? requestedStart : length;
  page.end = page.start;
  if (text == nullptr || maximumWidth == 0 || maximumLines == 0) return page;

  while (page.start > 0 && page.start < length &&
         (static_cast<uint8_t>(text[page.start]) & 0xC0) == 0x80) {
    --page.start;
  }
  page.initialStyle =
      initialStyleKnown ? initialStyle : styleAt(text, page.start);
  TextStyle pageStyle = page.initialStyle;
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
    size_t width = 0;
    TextStyle lineStyle = pageStyle;
    bool endedAtNewline = false;
    while (lineEnd < length) {
      if (text[lineEnd] == '\n') {
        endedAtNewline = true;
        break;
      }
      if (text[lineEnd] == ' ') lastSpace = lineEnd;
      const size_t characterBytes =
          utf8CharacterBytes(text, length, lineEnd);
      const uint32_t codepoint =
          utf8Codepoint(text, length, lineEnd);
      if (isStyleMarker(text[lineEnd])) {
        lineStyle = styleFromMarker(text[lineEnd]);
      }
      const size_t glyphWidth =
          characterWidth == nullptr ? displayColumns(codepoint)
                                    : characterWidth(codepoint, lineStyle);
      if (width > 0 && width + glyphWidth > maximumWidth) break;
      lineEnd += characterBytes;
      width += glyphWidth;
      if (glyphWidth > 0 && width >= maximumWidth) break;
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
    pageStyle = styleAfter(text, lineStart, offset, pageStyle);
  }
  page.end = offset;
  page.finalStyle = pageStyle;
  return page;
}

inline size_t previousPageStart(const char* text, size_t length,
                                size_t currentStart, size_t maximumWidth,
                                size_t maximumLines,
                                CharacterWidth characterWidth = nullptr) {
  if (currentStart == 0) return 0;
  size_t offset = 0;
  size_t previous = 0;
  TextStyle style = TextStyle::Regular;
  while (offset < currentStart) {
    const TextPage page =
        paginate(text, length, offset, maximumWidth, maximumLines, style, true,
                 characterWidth);
    if (page.end <= offset) return previous;
    if (page.end >= currentStart) return offset;
    previous = offset;
    offset = page.end;
    style = page.finalStyle;
  }
  return previous;
}

}  // namespace epub_text
