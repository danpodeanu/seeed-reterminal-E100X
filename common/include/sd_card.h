#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <stddef.h>

// SD-card helpers shared by every viewer app: mount + safe read/write.
// All three apps repeat the same mount sequence (SD_ENABLE high, share
// the epaper SPI bus, create a cache directory), and two of them repeat
// a bounded-size read plus an atomic write. Keep the retry/error paths
// in one place so future changes need only touch one file.
//
// Every SPI SD op below retries a few times on transient failure. The
// reTerminal shares one SPI bus between the e-paper controller and the
// SD card, and observed real-world glitches after Wi-Fi teardown, big
// HTTPS transfers, or panel refreshes make single SD.open()/SD.exists()
// /SD.rename() calls unreliable. Centralising the retry policy here
// stops each caller from open-coding it (or, worse, from treating a
// one-shot false as an authoritative "missing").
namespace sd_card {

// Bring up the SD card on the same SPI bus that the e-paper uses. On
// success the given `cacheDir` is guaranteed to exist. On failure the
// power rail is turned back off and a log line is emitted.
bool mount(SPIClass& spi, const char* cacheDir);

// Read a whole file into `out`, refusing to allocate more than
// `maxBytes` so a corrupted or hostile cache entry can't exhaust heap.
// Returns false on missing files, oversized files, or empty content.
bool readFile(const String& path, String& out, size_t maxBytes);

// Reliable existence probe.  SD.exists() on the ESP-IDF SD stack is
// known to return spurious false-negatives even for files that were
// successfully read on the previous boot (observed with .vlw fonts on
// E1001), which then poisons cache/miss decisions.  Open+close is
// authoritative.  Callers should prefer this over `SD.exists()`.
bool fileExists(const String& path);

// Write `contents` to `path` via a `.part` temporary + rename. Any
// existing file at `path` is replaced only after the write succeeds and
// the byte count matches. Returns false on any I/O error.
bool writeFileAtomically(const String& path, const String& contents);

// Retry-aware wrappers for the raw SD ops. Callers that manage their
// own File handle (e.g. streaming HTTP downloads to disk, screenshot
// BMP writers, xkcd index atomic writes) should use these instead of
// SD.open()/SD.rename() directly so a single SPI hiccup does not fail
// their operation.
File openForRead(const String& path);
File openForWrite(const String& path);
// Open for append: existing file is preserved and writes go to end.
// Missing files are created empty. Used by the SD log sink, which
// keeps a rolling boot log under /logs/.
File openForAppend(const String& path);
bool renameFile(const String& from, const String& to);

// Retrying wrappers for the remaining raw SD ops. Callers should use
// these instead of SD.remove()/SD.mkdir()/SD.rmdir() so that transient
// SPI stalls do not surface as leaked temp files, missing cache
// directories, or half-deleted trees.
//
// `removeFile` returns true when the path no longer exists on return
// (successful delete or already absent) - callers cleaning up a temp
// file typically ignore the return value, but code that must know the
// file was actually gone can check it.
bool removeFile(const String& path);
// Retrying SD.mkdir wrapper. Returns true only when the directory was
// created; callers that only need "exists on return" should probe
// fileExists() first.
bool makeDir(const String& path);
// Retrying SD.rmdir wrapper. Fails if the directory is non-empty.
bool removeDir(const String& path);

}  // namespace sd_card
