#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <stddef.h>

// SD-card helpers shared by every viewer app: mount + safe read/write.
// All three apps repeat the same mount sequence (shared peripheral rail
// enabled, share the e-paper SPI bus, create a cache directory), and two repeat
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

// Force an in-place SD.end()/SD.begin() recovery using the SPI bus and CS
// captured by mount(). Streaming callers must close their File handle first,
// then reopen it after this returns true.
bool recover();

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

// Reformat the SD card to a fresh MBR + FAT32 filesystem, then re-mount.
//
// Steps:
//   1. Ensure the card is mounted so we can hit its raw sectors.
//   2. Zero the first 34 sectors (kills MBR at sector 0, any FAT VBR at
//      sector 1 in an SFD layout, and any GPT primary header at sector 1
//      + protective MBR / primary partition entries in sectors 2-33).
//   3. Zero the standard FAT32-partition-1 VBR at sector 2048 so a lingering
//      old superblock inside the partition can't be mistaken for a live FS.
//   4. SD.end() / SD.begin(format_if_empty=true) - the wiped card comes back
//      as FR_NO_FILESYSTEM, which triggers f_mkfs(FM_ANY) inside sd_diskio.
//      Because we don't set FM_SFD, FatFs writes a fresh MBR partition table
//      itself; the resulting card is FAT32 with a standard PC-style layout
//      and reads on Windows/macOS/Linux without special drivers.
//   5. Recreate `cacheDir` so the caller's post-mount code doesn't have to.
//
// Writes to `error` on failure so callers (HTTP handlers) can surface it.
// Returns true on success. On success the card is remounted and usable via
// the normal SD.* API without a further mount() call.
bool formatCard(SPIClass& spi, const char* cacheDir, String& error);

}  // namespace sd_card
