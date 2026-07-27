#include "sd_card.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "app_logger.h"
#include "board_pins.h"
#include "epaper_setup.h"

namespace sd_card {

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
  if (!fileExists(cacheDir) && !SD.mkdir(cacheDir)) {
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
  File file = SD.open(path, FILE_READ);
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
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) {
    LOG.printf("[cache] could not create %s\n", temporary.c_str());
    return false;
  }
  const size_t written = file.print(contents);
  file.flush();
  file.close();
  if (written != contents.length()) {
    LOG.printf("[cache] short write for %s\n", path.c_str());
    SD.remove(temporary);
    return false;
  }
  SD.remove(path);
  if (!SD.rename(temporary, path)) {
    LOG.printf("[cache] could not install %s\n", path.c_str());
    SD.remove(temporary);
    return false;
  }
  return true;
}

bool fileExists(const String& path) {
  File probe = SD.open(path, FILE_READ);
  if (!probe) return false;
  probe.close();
  return true;
}

}  // namespace sd_card
