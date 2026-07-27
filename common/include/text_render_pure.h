#pragma once

// Byte-level text normalization shared by every viewer app.
//
// This header is a *pure* C++17 implementation (std::string only) so the
// native test env can exercise it without Arduino headers.  The main
// text_render.h wraps it with the Arduino ``String`` wrapper the firmware
// uses at runtime.
//
// Behavior:
//   * HTML entities that appear in xkcd/weather feeds are unescaped
//     (``&quot;``, ``&apos;``, ``&#39;``, ``&lt;``, ``&gt;``, ``&amp;``).
//   * Bytes in the C0 control range (0x00-0x1F, 0x7F) and the C1 control
//     range (0x80-0x9F when the byte is not part of a valid UTF-8
//     multi-byte sequence) are stripped; whitespace-class controls
//     collapse to a single ASCII space.
//   * Valid UTF-8 sequences (0xC2..0xF4 lead bytes plus their continuation
//     bytes) are passed through verbatim so a smooth font can render
//     Unicode glyphs.  Malformed sequences are dropped rather than
//     emitted, which is what the epaper fonts need.
//   * Runs of whitespace collapse to a single space; the result is
//     trimmed on both ends.

#include <cstdint>
#include <string>

namespace text_render {
namespace pure {

// UTF-8 lead-byte length lookup.  Returns 1..4 for a valid lead byte, or
// 0 for a continuation byte / invalid byte.
inline int utf8LeadLength(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return c >= 0xC2 ? 2 : 0;  // 0xC0/0xC1 illegal
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return c <= 0xF4 ? 4 : 0;  // >0xF4 out of range
  return 0;
}

inline bool htmlUnescapeAt(const std::string& in, size_t& i, std::string& out) {
  static const struct { const char* entity; char replacement; } kEntities[] = {
      {"&quot;", '"'},
      {"&apos;", '\''},
      {"&#39;", '\''},
      {"&lt;", '<'},
      {"&gt;", '>'},
      {"&amp;", '&'},
  };
  for (const auto& e : kEntities) {
    const size_t n = std::char_traits<char>::length(e.entity);
    if (in.compare(i, n, e.entity) == 0) {
      out += e.replacement;
      i += n;
      return true;
    }
  }
  return false;
}

inline std::string displayText(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool lastWasSpace = false;

  size_t i = 0;
  while (i < input.size()) {
    // HTML entity unescape first so a "&nbsp;" style entity (should any
    // appear) becomes its literal character before the byte-level pass.
    if (input[i] == '&' && htmlUnescapeAt(input, i, out)) {
      lastWasSpace = false;
      continue;
    }

    const uint8_t c = static_cast<uint8_t>(input[i]);

    // Printable ASCII.
    if (c >= 0x20 && c < 0x7F) {
      if (c == ' ') {
        if (!lastWasSpace) out += ' ';
        lastWasSpace = true;
      } else {
        out += static_cast<char>(c);
        lastWasSpace = false;
      }
      ++i;
      continue;
    }

    // Whitespace controls collapse to a single space.
    if (c == '\t' || c == '\r' || c == '\n' || c == 0x0B || c == 0x0C) {
      if (!lastWasSpace) out += ' ';
      lastWasSpace = true;
      ++i;
      continue;
    }

    // Other C0 controls and DEL are dropped without producing whitespace.
    if (c < 0x20 || c == 0x7F) {
      ++i;
      continue;
    }

    // 0x80..: must be a UTF-8 lead byte with the right number of valid
    // continuation bytes to follow.  Anything else is dropped.
    const int len = utf8LeadLength(c);
    if (len < 2 || i + len > input.size()) {
      ++i;
      continue;
    }
    bool valid = true;
    for (int k = 1; k < len; ++k) {
      if ((static_cast<uint8_t>(input[i + k]) & 0xC0) != 0x80) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      ++i;
      continue;
    }
    out.append(input, i, len);
    lastWasSpace = false;
    i += len;
  }

  // Trim trailing whitespace we may have appended, and any leading space
  // still present.
  while (!out.empty() && out.back() == ' ') out.pop_back();
  size_t front = 0;
  while (front < out.size() && out[front] == ' ') ++front;
  if (front) out.erase(0, front);
  return out;
}

}  // namespace pure
}  // namespace text_render
