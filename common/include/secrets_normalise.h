#pragma once

// Shared helpers for handling user-pasted secret strings before they hit
// firmware crypto. Historically the weather-viewer had its own copy of the
// hex-digit normaliser inside app_logic.h; hosting it here means any future
// app (xkcd, photo, ...) that needs to consume a hex-encoded token can share
// the exact rules -- and any Python tester can import the mirror in
// common/tools/secrets_normalise.py to guarantee parity.

#include <stddef.h>
#include <stdint.h>

namespace secrets_normalise {

// Decode one hex character to its 0..15 value, or -1 if not a hex digit.
constexpr int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// Copy `text` into `out` while dropping the punctuation people paste along
// with hex-encoded keys: ASCII whitespace, ':' byte separators (as in
// openssl's `priv:` output), and a single leading "0x" / "0X" prefix.
// Any character that is neither a hex digit nor allowed punctuation causes
// the function to return SIZE_MAX; if the cleaned string would not fit in
// `outCapacity` (including the trailing NUL) it also returns SIZE_MAX.
// On success, `out` is NUL-terminated and the return value is the length
// of the cleaned string (excluding the NUL). Callers typically feed the
// result into decodeHex().
inline size_t normalizeHexDigits(const char* text, char* out,
                                 size_t outCapacity) {
  if (text == nullptr || out == nullptr || outCapacity == 0) {
    return static_cast<size_t>(-1);
  }
  size_t o = 0;
  bool consumedPrefix = false;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    const char c = text[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ':') {
      continue;
    }
    if (!consumedPrefix && o == 0 && c == '0' &&
        (text[i + 1] == 'x' || text[i + 1] == 'X')) {
      consumedPrefix = true;
      ++i;  // skip the 'x' as well
      continue;
    }
    if (hexNibble(c) < 0) {
      return static_cast<size_t>(-1);
    }
    if (o + 1u >= outCapacity) return static_cast<size_t>(-1);
    out[o++] = c;
  }
  out[o] = '\0';
  return o;
}

}  // namespace secrets_normalise
