#pragma once

#include <stddef.h>
#include <stdint.h>

namespace calendar_latin_text {

constexpr uint32_t kReplacementCodepoint = 0xFFFD;

inline bool isContinuationByte(uint8_t value) {
  return (value & 0xC0) == 0x80;
}

inline uint32_t nextCodepoint(const char* text, size_t length, size_t& offset) {
  if (offset >= length) return 0;

  const uint8_t lead = static_cast<uint8_t>(text[offset]);
  if (lead < 0x80) {
    ++offset;
    return lead;
  }

  int byteCount = 0;
  uint32_t codepoint = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    byteCount = 2;
    codepoint = lead & 0x1F;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    byteCount = 3;
    codepoint = lead & 0x0F;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    byteCount = 4;
    codepoint = lead & 0x07;
  } else {
    ++offset;
    return kReplacementCodepoint;
  }

  if (offset + byteCount > length) {
    ++offset;
    return kReplacementCodepoint;
  }
  for (int index = 1; index < byteCount; ++index) {
    const uint8_t continuation =
        static_cast<uint8_t>(text[offset + index]);
    if (!isContinuationByte(continuation)) {
      ++offset;
      return kReplacementCodepoint;
    }
    codepoint = (codepoint << 6) | (continuation & 0x3F);
  }

  const bool overlong =
      (byteCount == 2 && codepoint < 0x80) ||
      (byteCount == 3 && codepoint < 0x800) ||
      (byteCount == 4 && codepoint < 0x10000);
  const bool surrogate = codepoint >= 0xD800 && codepoint <= 0xDFFF;
  if (overlong || surrogate || codepoint > 0x10FFFF) {
    ++offset;
    return kReplacementCodepoint;
  }

  offset += byteCount;
  return codepoint;
}

inline bool isEmbeddedLatinCodepoint(uint32_t codepoint) {
  return (codepoint >= 0x0020 && codepoint <= 0x02FF) ||
         (codepoint >= 0x0300 && codepoint <= 0x036F) ||
         (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
         (codepoint >= 0x1D00 && codepoint <= 0x1DBF) ||
         (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
         (codepoint >= 0x1E00 && codepoint <= 0x1EFF) ||
         (codepoint >= 0x2000 && codepoint <= 0x218F) ||
         (codepoint >= 0x2C60 && codepoint <= 0x2C7F) ||
         (codepoint >= 0xA720 && codepoint <= 0xA7FF) ||
         (codepoint >= 0xAB30 && codepoint <= 0xAB6F) ||
         (codepoint >= 0xFB00 && codepoint <= 0xFB06) ||
         (codepoint >= 0xFE20 && codepoint <= 0xFE2F);
}

inline bool isIgnorableCodepoint(uint32_t codepoint) {
  return codepoint == 0x200B || codepoint == 0x200C ||
         codepoint == 0x200D || codepoint == 0x2060 ||
         codepoint == 0xFEFF ||
         (codepoint >= 0xFE00 && codepoint <= 0xFE0F);
}

}  // namespace calendar_latin_text
