#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure streaming search for a byte sequence in a larger byte stream, used
// by the SD-OTA path to verify that the incoming firmware image was built
// for THIS device's model. The tag is a fixed literal like
// "reterminal-ota:E1004" and lives in the running firmware (baked via
// __attribute__((used)) in sd_ota.cpp), so every firmware image carries
// its own model tag inside its rodata section.
//
// We can't slurp the entire .bin into RAM (~1.4 MB) to memmem-search it,
// and rodata may land anywhere in the flash image, so we scan while
// streaming into the OTA partition. This helper carries the search state
// across chunks; the caller feeds bytes and observes the boolean return
// to know whether the tag has been found so far.
//
// Restrictions on the tag: no self-overlap. Our tag "reterminal-ota:E10xx"
// begins with 'r' and 'r' does not repeat until the tail, so this naive
// KMP-free scanner is correct.

namespace sd_ota {

struct TagScanner {
  const uint8_t* tag;
  size_t         tagLen;
  size_t         state = 0;
  bool           found = false;

  TagScanner(const uint8_t* t, size_t n) : tag(t), tagLen(n) {}

  // Feed a chunk. Returns true once `found` transitions to true (i.e.
  // the tag has been matched somewhere in the streamed data so far).
  bool feed(const uint8_t* data, size_t n) {
    if (found) return true;
    for (size_t i = 0; i < n; ++i) {
      const uint8_t b = data[i];
      if (b == tag[state]) {
        ++state;
        if (state == tagLen) {
          found = true;
          return true;
        }
      } else {
        // Naive restart. Safe because our tag has no self-overlap - see
        // module comment. If the tag ever grows a repeating prefix we'd
        // need KMP failure links here.
        state = (b == tag[0]) ? 1 : 0;
      }
    }
    return false;
  }
};

}  // namespace sd_ota
