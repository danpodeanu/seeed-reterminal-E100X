#include "sd_card.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "app_logger.h"
#include "board_pins.h"
#include "epaper_setup.h"
#include "peripheral_power.h"

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
// the card. Uses the same short polling budget as mount() so a card
// that needs an extra beat to power-cycle (e.g. right after the Wi-Fi
// radio powered down mid-refresh) still comes back. Returns true if
// the remount succeeded.
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
  peripheral_power::enableSd();
  // Re-run the shared peripheral rail + SPI init the initial mount did.
  // wifi_sta::disable() and big HTTPS transfers can leave the rail in a
  // state where a plain SD.begin() sees the card as absent; finalize()
  // is idempotent and cheap.
  epaper_setup::finalize(*g_spi);
  // Poll SD.begin() with the same 250 ms budget as mount(). A cold-ish
  // card can miss the first CMD0 after a bus reset but respond within a
  // few tens of ms; a single-shot retry with a 50 ms delay (the old
  // behaviour) was not enough.
  constexpr uint32_t kSdRemountBudgetMs = 250;
  constexpr uint32_t kSdRemountPollMs = 25;
  const uint32_t startMs = millis();
  bool ok = SD.begin(g_csPin, *g_spi);
  while (!ok && (millis() - startMs) < kSdRemountBudgetMs) {
    SD.end();
    delay(kSdRemountPollMs);
    ok = SD.begin(g_csPin, *g_spi);
  }
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
  // Cheap short-circuit: if the file already doesn't exist, we're done
  // without touching SD at all beyond the directory probe. This is the
  // hot path when the caller uses removeFile() to pre-clear a temporary
  // that may or may not exist.
  if (!SD.exists(path)) return true;
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
  peripheral_power::enableSd();

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
  delay(board::SD_POWER_SETTLE_MS);
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
    return false;
  }
  // Capture the bus + CS so attemptRemount() can reset the driver
  // later without re-plumbing the SPI object through every caller.
  g_spi = &spi;
  g_csPin = board::PIN_SD_CS;
  if (!fileExists(cacheDir) && !retryingMkdir(cacheDir)) {
    LOG.printf("[sd] could not create %s\n", cacheDir);
    SD.end();
    return false;
  }
  LOG.printf("[sd] mounted, card=%lluMB\n",
             static_cast<unsigned long long>(
                 SD.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool formatCard(SPIClass& spi, const char* cacheDir, String& error) {
  // Guarantee a live driver + volume before we start swinging at raw
  // sectors. If nothing's mounted (or the last mount failed), do a
  // fresh SD.begin with format_if_empty=true so a card with no valid
  // filesystem still comes back in a usable state we can then wipe and
  // reformat properly.
  auto pumpMount = [&](bool formatIfEmpty) -> bool {
    if (!SD.begin(board::PIN_SD_CS, spi, 4000000, "/sd", 5, formatIfEmpty)) {
      SD.end();
      delay(50);
      return SD.begin(board::PIN_SD_CS, spi, 4000000, "/sd", 5, formatIfEmpty);
    }
    return true;
  };
  epaper_setup::finalize(spi);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  if (!pumpMount(false)) {
    // Card with a filesystem the driver couldn't parse (or no FS at
    // all). Retry allowing the SD library's built-in mkfs fallback so
    // the card is at least driver-visible for the raw wipe below.
    if (!pumpMount(true)) {
      error = F("SD card could not be initialised");
      LOG.println("[sd] format: card unresponsive; cannot proceed");
      return false;
    }
    LOG.println("[sd] format: card had no filesystem; recovered via mkfs");
  }
  g_spi = &spi;
  g_csPin = board::PIN_SD_CS;

  const uint32_t totalSectors = SD.numSectors();
  LOG.printf("[sd] format: card=%luMB (%lu sectors); wiping partition table\n",
             static_cast<unsigned long>(SD.cardSize() / (1024UL * 1024UL)),
             static_cast<unsigned long>(totalSectors));

  // Zero enough of the front of the card to destroy any existing
  // partitioning scheme (MBR, GPT primary header + partition entries)
  // AND the standard FAT32 partition-1 VBR at LBA 2048, so a leftover
  // superblock inside the old partition can't fool f_mount into
  // thinking a filesystem still exists after the wipe.
  uint8_t sector[512] = {};
  bool wipeOk = true;
  for (uint32_t s = 0; s < 34 && wipeOk; ++s) {
    wipeOk = SD.writeRAW(sector, s);
  }
  if (wipeOk && totalSectors > 2049) {
    wipeOk = SD.writeRAW(sector, 2048);
  }
  if (!wipeOk) {
    error = F("SD card write failed while erasing partition table");
    LOG.println("[sd] format: raw sector wipe failed");
    return false;
  }

  // Drop the driver so the re-mount picks up a fresh view of an
  // apparently-empty card. FR_NO_FILESYSTEM will fire on the next
  // f_mount (nothing to parse now) and format_if_empty=true will
  // trigger f_mkfs(FM_ANY). FatFs's f_mkfs, when neither FM_SFD nor an
  // existing MBR is present, writes a fresh MBR partition table with
  // one primary FAT32 partition covering the card, then formats
  // inside it - which is exactly the layout Windows/macOS/Linux
  // expect. FM_ANY picks FAT32 for anything >= 512 MB.
  SD.end();
  delay(100);
  epaper_setup::finalize(spi);
  if (!pumpMount(true)) {
    error = F("SD card mkfs failed after erase");
    LOG.println("[sd] format: post-wipe mount+mkfs failed");
    g_spi = nullptr;
    g_csPin = -1;
    return false;
  }
  LOG.printf("[sd] format: fresh FAT32 filesystem, card=%lluMB\n",
             static_cast<unsigned long long>(
                 SD.cardSize() / (1024ULL * 1024ULL)));

  // Re-plant the cache directory the caller relies on.
  if (cacheDir && cacheDir[0] &&
      !fileExists(cacheDir) && !retryingMkdir(cacheDir)) {
    LOG.printf("[sd] format: could not create %s on fresh card\n", cacheDir);
    error = F("SD formatted but cache directory could not be created");
    return false;
  }
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
  // Use SD.exists() directly instead of openForRead(). openForRead()
  // is retryingOpen(), which runs the full retry+remount ladder before
  // deciding a file is missing - so a legitimate "does this cache
  // entry exist yet?" probe would spend ~1 s and trigger a bus reset
  // per call. SD.exists() bottoms out in a single VFS open() and
  // returns cleanly on ENOENT.
  return SD.exists(path);
}

}  // namespace sd_card
