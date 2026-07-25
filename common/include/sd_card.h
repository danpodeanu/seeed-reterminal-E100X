#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <stddef.h>

// SD-card helpers shared by every viewer app: mount + safe read/write.
// All three apps repeat the same mount sequence (SD_ENABLE high, share
// the epaper SPI bus, create a cache directory), and two of them repeat
// a bounded-size read plus an atomic write. Keep the retry/error paths
// in one place so future changes need only touch one file.
namespace sd_card {

// Bring up the SD card on the same SPI bus that the e-paper uses. On
// success the given `cacheDir` is guaranteed to exist. On failure the
// power rail is turned back off and a log line is emitted.
bool mount(SPIClass& spi, const char* cacheDir);

// Read a whole file into `out`, refusing to allocate more than
// `maxBytes` so a corrupted or hostile cache entry can't exhaust heap.
// Returns false on missing files, oversized files, or empty content.
bool readFile(const String& path, String& out, size_t maxBytes);

// Write `contents` to `path` via a `.part` temporary + rename. Any
// existing file at `path` is replaced only after the write succeeds and
// the byte count matches. Returns false on any I/O error.
bool writeFileAtomically(const String& path, const String& contents);

}  // namespace sd_card
