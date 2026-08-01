#include "sd_card.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "app_logger.h"
#include "board_pins.h"
#include "epaper_setup.h"

namespace sd_card {

namespace {

// Number of attempts to make an SD operation succeed before giving up.
// The reTerminal shares one SPI bus between the e-paper controller and
// the SD card. Under load (right after Wi-Fi teardown, big HTTPS
// transfers, or large panel refreshes) SD.open() / SD.exists() /
// SD.rename() have all been observed to spuriously fail on a file/path
// that is definitely present and would succeed on a re-attempt. Treat
// every SPI SD op as retryable so a single glitch doesn't propagate
// into "missing font", "cache write dropped", etc.
//
// Two-stage recovery:
//   - Stage 1: 5 fast retries with exponential backoff (25/50/100/200 ms
//     between attempts, ~375 ms of sleeps). Handles brief SPI stalls
//     without touching the SD driver state.
//   - Stage 2: if all fast retries fail, do a full SD.end() +
//     SD.begin() bus reset and try one more time. Recovers from the
//     "driver got wedged after a big HTTPS+gzip transfer" case where
//     every SD.open() itself blocks for ~1 s before returning false -
//     no amount of additional retries helps there, only a bus reset.
// A healthy card still succeeds on the first attempt, so the boot
// cost is unchanged; the extra logic only pays when it actually
// prevents a lost cache write or a missing font.
constexpr int kSdOpRetries = 5;
constexpr uint32_t kSdOpInitialDelayMs = 25;

// Bus state captured during mount() so we can transparently remount
// during the fallback recovery step. Set once at mount, never
// modified afterwards; nullptr means mount() has not run.
SPIClass* g_spi = nullptr;
int g_csPin = -1;

// Rate-limit remount attempts to at most once per burst window so
// repeated failing ops don't each pay the SD.begin() cost.
uint32_t g_lastRemountMs = 0;
constexpr uint32_t kRemountCooldownMs = 500;

// Sleep between attempt `attempt` (0-based) and the next one. Doubles
// each step so the tail attempts absorb longer SPI stalls without
// paying that cost when the first retry is enough.
void backoffSleep(int attempt) {
  uint32_t delayMs = kSdOpInitialDelayMs << attempt;  // 25, 50, 100, 200
  delay(delayMs);
}

// Reset the SD driver in place. Called after a burst of failed ops,
// to recover the "driver wedged" case where SD.open()/rename()/remove()
// all keep blocking for ~1 s before returning false. SD.end() +
// SD.begin() flushes the driver's internal state and re-negotiates
// the card. Returns true if the remount succeeded.
bool attemptRemount() {
  if (!g_spi || g_csPin < 0) return false;
  const uint32_t now = millis();
  if (g_lastRemountMs != 0 &&
      (now - g_lastRemountMs) < kRemountCooldownMs) {
    // Another op in the same burst already remounted us; don't do
    // it again in case the card genuinely is unhappy.
    return false;
  }
  g_lastRemountMs = now;
  SD.end();
  delay(50);
  const bool ok = SD.begin(g_csPin, *g_spi);
  LOG.printf("[sd] bus reset after stalled op -> %s\n",
             ok ? "remounted" : "failed");
  return ok;
}

// Try opening `path` in `mode` up to kSdOpRetries times, then do one
// remount + one more attempt on the failure path. Returns the first
// non-falsy File; the caller inherits ownership (call close()).
File retryingOpen(const String& path, const char* mode) {
  File file = SD.open(path, mode);
  for (int attempt = 0; !file && attempt < kSdOpRetries - 1; ++attempt) {
    backoffSleep(attempt);
    file = SD.open(path, mode);
  }
  if (!file && attemptRemount()) {
    file = SD.open(path, mode);
  }
  return file;
}

// Try SD.rename until it succeeds or we've burned through kSdOpRetries
// plus a remount recovery attempt.
bool retryingRename(const String& from, const String& to) {
  if (SD.rename(from, to)) return true;
  for (int attempt = 0; attempt < kSdOpRetries - 1; ++attempt) {
    backoffSleep(attempt);
    if (SD.rename(from, to)) return true;
  }
  if (attemptRemount() && SD.rename(from, to)) return true;
  return false;
}

// Try SD.mkdir. mkdir on an existing directory returns false, so the
// caller should probe fileExists() first; this only handles the "SPI
// hiccup dropped the mkdir" case.
bool retryingMkdir(const char* path) {
  if (SD.mkdir(path)) return true;
  for (int attempt = 0; attempt < kSdOpRetries - 1; ++attempt) {
    backoffSleep(attempt);
    if (SD.mkdir(path)) return true;
  }
  if (attemptRemount() && SD.mkdir(path)) return true;
  return false;
}

// Try SD.remove. Succeeds if the file is gone after the call; a first
// false may just mean the file did not exist, so we probe existence
// before scheduling another attempt to avoid burning the retry budget
// on already-clean paths.
bool retryingRemove(const String& path) {
  if (SD.remove(path)) return true;
  for (int attempt = 0; attempt < kSdOpRetries - 1; ++attempt) {
    // Probe: if the file already vanished (or never existed), we're
    // done. openForRead() itself retries, so this reflects the true
    // filesystem state after the last SPI stall.
    File probe = SD.open(path, FILE_READ);
    if (!probe) return true;
    probe.close();
    backoffSleep(attempt);
    if (SD.remove(path)) return true;
  }
  if (attemptRemount()) {
    File probe = SD.open(path, FILE_READ);
    if (!probe) return true;
    probe.close();
    if (SD.remove(path)) return true;
  }
  return false;
}

// Retrying SD.rmdir. Non-empty directories return false immediately
// (a legitimate failure) but transient SPI stalls also produce false,
// so we still retry a few times.
bool retryingRmdir(const String& path) {
  if (SD.rmdir(path)) return true;
  for (int attempt = 0; attempt < kSdOpRetries - 1; ++attempt) {
    backoffSleep(attempt);
    if (SD.rmdir(path)) return true;
  }
  if (attemptRemount() && SD.rmdir(path)) return true;
  return false;
}

}  // namespace

File openForRead(const String& path) {
  return retryingOpen(path, FILE_READ);
}

File openForWrite(const String& path) {
  return retryingOpen(path, FILE_WRITE);
}

File openForAppend(const String& path) {
  return retryingOpen(path, FILE_APPEND);
}

bool renameFile(const String& from, const String& to) {
  return retryingRename(from, to);
}

bool removeFile(const String& path) {
  return retryingRemove(path);
}

bool makeDir(const String& path) {
  return retryingMkdir(path.c_str());
}

bool removeDir(const String& path) {
  return retryingRmdir(path);
}

bool mount(SPIClass& spi, const char* cacheDir) {
  // Enable the shared peripheral power rail and re-init the panel's SPI
  // bus with a real MISO pin. Historically this was inlined here and
  // the viewer apps got E1001 panel support as a side effect of mounting
  // SD; that coupling now lives in epaper_setup so tools without SD can
  // opt in explicitly. Safe to call twice.
  epaper_setup::finalize(spi);

  pinMode(board::PIN_SD_DETECT, INPUT_PULLUP);
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);

  // The SD card needs its VDD rail to settle and its internal power-on
  // reset to finish before it will respond to CMD0. There is no ready
  // pin to sample, so poll SD.begin() at short intervals until it
  // succeeds or the SD spec's 250 ms power-up budget elapses. Healthy
  // modern cards typically respond on the first attempt within a few
  // milliseconds; the retry only pays for itself on a slow or beat-up
  // card that would previously have failed.
  constexpr uint32_t kSdInitBudgetMs = 250;
  constexpr uint32_t kSdInitPollMs = 5;
  const uint32_t startMs = millis();
  bool mounted = SD.begin(board::PIN_SD_CS, spi);
  while (!mounted && (millis() - startMs) < kSdInitBudgetMs) {
    // Some SPI SD drivers need a clean SD.end() before retrying so any
    // half-initialised state is cleared out. Cheap on ESP32 Arduino SD.
    SD.end();
    delay(kSdInitPollMs);
    mounted = SD.begin(board::PIN_SD_CS, spi);
  }
  if (!mounted) {
    LOG.println("[sd] mount failed; insert a FAT32/exFAT card");
    digitalWrite(board::PIN_SD_ENABLE, LOW);
    return false;
  }
  // Capture the bus + CS so attemptRemount() can reset the driver
  // later without re-plumbing the SPI object through every caller.
  g_spi = &spi;
  g_csPin = board::PIN_SD_CS;
  if (!fileExists(cacheDir) && !retryingMkdir(cacheDir)) {
    LOG.printf("[sd] could not create %s\n", cacheDir);
    SD.end();
    digitalWrite(board::PIN_SD_ENABLE, LOW);
    return false;
  }
  LOG.printf("[sd] mounted, card=%lluMB\n",
             static_cast<unsigned long long>(
                 SD.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool readFile(const String& path, String& out, size_t maxBytes) {
  File file = openForRead(path);
  if (!file) return false;
  if (file.size() > maxBytes) {
    LOG.printf("[cache] refusing to read oversized %s (%lu bytes)\n",
               path.c_str(), static_cast<unsigned long>(file.size()));
    file.close();
    return false;
  }
  out = file.readString();
  file.close();
  return !out.isEmpty();
}

bool writeFileAtomically(const String& path, const String& contents) {
  const String temporary = path + ".part";
  retryingRemove(temporary);
  File file = openForWrite(temporary);
  if (!file) {
    LOG.printf("[cache] could not create %s\n", temporary.c_str());
    return false;
  }
  const size_t written = file.print(contents);
  file.flush();
  file.close();
  if (written != contents.length()) {
    LOG.printf("[cache] short write for %s\n", path.c_str());
    retryingRemove(temporary);
    return false;
  }
  retryingRemove(path);
  if (!renameFile(temporary, path)) {
    LOG.printf("[cache] could not install %s\n", path.c_str());
    retryingRemove(temporary);
    return false;
  }
  return true;
}

bool fileExists(const String& path) {
  File probe = openForRead(path);
  if (!probe) return false;
  probe.close();
  return true;
}

}  // namespace sd_card
