#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure helpers extracted from xkcd_index for unit testing on the native
// platform (which has no Arduino String / SD.h). The public String-based
// entry points in xkcd_index.h forward to these after trimming input.
namespace xkcd_index {

// Interpret the first `length` bytes of `text` (assumed already stripped
// of leading/trailing whitespace) as a decimal unsigned integer, capped
// at 100000. Empty input, non-digit bytes, values > 100000 and (unless
// `allowZero`) the value 0 all return false. Callers that read raw
// lines from disk (e.g. via file.readStringUntil('\n')) must trim first
// -- this helper does not tolerate whitespace.
inline bool parseUnsignedDigits(const char* text, size_t length,
                                uint32_t& value, bool allowZero) {
  if (text == nullptr || length == 0 || length > 10) return false;
  uint64_t parsed = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    if (c < '0' || c > '9') return false;
    parsed = parsed * 10U + static_cast<uint8_t>(c - '0');
    if (parsed > 100000U) return false;
  }
  if (!allowZero && parsed == 0) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

// Pack an 8bpp indexed image (one byte per pixel, values 0..15) into
// 4bpp storage in-place, two pixels per byte. `width` must be even --
// the last column is silently dropped otherwise. Row `y` writes to
// bytes [y*(width/2), (y+1)*(width/2)).
inline void pack4bppInPlace(uint8_t* indices, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* source = indices + static_cast<size_t>(y) * width;
    uint8_t* destination =
        indices + static_cast<size_t>(y) * (width / 2);
    for (int x = 0; x + 1 < width; x += 2) {
      destination[x / 2] = static_cast<uint8_t>(
          ((source[x] & 0x0F) << 4) | (source[x + 1] & 0x0F));
    }
  }
}

}  // namespace xkcd_index
