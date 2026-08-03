#include "sd_ota.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <time.h>

#include "app_logger.h"
#include "board_pins.h"
#include "sd_card.h"
#include "sd_ota_pure.h"
#include "version.h"

namespace {

// The marker every viewer firmware embeds so an SD image can be
// verified as "same model." Baked here via __attribute__((used)) so
// the linker keeps it and it lands in rodata inside the .bin - which
// means the streaming scanner will find it while writing the incoming
// image to flash.
//
// MODEL_NAME_LITERAL comes from board_pins.h and is a preprocessor
// macro (not a constexpr) so we can string-concatenate at compile time.
extern "C" {
__attribute__((used, section(".rodata")))
const char kSdOtaTag[] = "reterminal-ota:" MODEL_NAME_LITERAL;
}

constexpr size_t kSdOtaTagLen = sizeof(kSdOtaTag) - 1;  // exclude NUL

// Chunk size for streaming reads from SD -> OTA writer. A few KB is a
// good balance: big enough to amortise SD.read() overhead, small enough
// that the scanner's per-byte inner loop stays cache-friendly and the
// PSRAM buffer allocation is trivially available. Sized to comfortably
// fit in DRAM even alongside a font cache.
constexpr size_t kChunk = 4096;

[[maybe_unused]] const char* resultLabel(sd_ota::Result r) {
  switch (r) {
    case sd_ota::Result::NoFile:     return "nofile";
    case sd_ota::Result::ReadFail:   return "readfail";
    case sd_ota::Result::OtaFail:    return "otafail";
    case sd_ota::Result::WrongModel: return "wrongmodel";
    case sd_ota::Result::ApplyFail:  return "applyfail";
    case sd_ota::Result::Applied:    return "applied";
  }
  return "unknown";
}

// Rename /update.bin to /update.<suffix>-<epoch> so we don't reflash it
// next boot. Epoch is best-effort: if RTC hasn't been synced yet we use
// millis() which is fine for uniqueness within a single boot.
void archiveUpdateFile(const char* path, const char* suffix) {
  time_t now = time(nullptr);
  char newName[64];
  if (now > 100000) {
    snprintf(newName, sizeof(newName), "/update.%s-%ld", suffix, (long)now);
  } else {
    snprintf(newName, sizeof(newName), "/update.%s-boot%lu", suffix,
             (unsigned long)millis());
  }
  if (!sd_card::renameFile(path, newName)) {
    LOG.printf("[ota] archive rename %s -> %s failed\n", path, newName);
  } else {
    LOG.printf("[ota] archived to %s\n", newName);
  }
}

}  // namespace

namespace sd_ota {

bool hasUpdate(const Options& opts) {
  return sd_card::fileExists(opts.path);
}

Result apply(const Options& opts) {
  File f = sd_card::openForRead(opts.path);
  if (!f) {
    LOG.printf("[ota] openForRead(%s) failed\n", opts.path);
    return Result::NoFile;
  }
  const size_t total = f.size();
  LOG.printf("[ota] starting OTA from %s (%u bytes), running v%s, tag=\"%s\"\n",
       opts.path, (unsigned)total, board::FIRMWARE_VERSION, kSdOtaTag);

  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (!target) {
    LOG.printf("[ota] no next OTA partition - is the partition table OTA-capable?\n");
    f.close();
    archiveUpdateFile(opts.path, "failed-nopart");
    return Result::OtaFail;
  }
  LOG.printf("[ota] target partition: %s @ 0x%x (%u bytes)\n",
       target->label, (unsigned)target->address, (unsigned)target->size);
  if (total > target->size) {
    LOG.printf("[ota] image (%u) larger than partition (%u)\n",
         (unsigned)total, (unsigned)target->size);
    f.close();
    archiveUpdateFile(opts.path, "failed-toobig");
    return Result::OtaFail;
  }

  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(target, total, &handle);
  if (err != ESP_OK) {
    LOG.printf("[ota] esp_ota_begin: %s\n", esp_err_to_name(err));
    f.close();
    archiveUpdateFile(opts.path, "failed-begin");
    return Result::OtaFail;
  }

  TagScanner scanner(reinterpret_cast<const uint8_t*>(kSdOtaTag),
                     kSdOtaTagLen);
  auto* buf = static_cast<uint8_t*>(malloc(kChunk));
  if (!buf) {
    LOG.printf("[ota] chunk buffer malloc failed\n");
    esp_ota_abort(handle);
    f.close();
    archiveUpdateFile(opts.path, "failed-nomem");
    return Result::OtaFail;
  }

  size_t written = 0;
  uint32_t lastLog = millis();
  while (written < total) {
    const size_t want = (total - written) < kChunk ? (total - written) : kChunk;
    const int got = f.read(buf, want);
    if (got <= 0) {
      LOG.printf("[ota] SD read failed at %u/%u\n",
           (unsigned)written, (unsigned)total);
      free(buf);
      esp_ota_abort(handle);
      f.close();
      archiveUpdateFile(opts.path, "failed-read");
      return Result::ReadFail;
    }
    err = esp_ota_write(handle, buf, got);
    if (err != ESP_OK) {
      LOG.printf("[ota] esp_ota_write @%u: %s\n",
           (unsigned)written, esp_err_to_name(err));
      free(buf);
      esp_ota_abort(handle);
      f.close();
      archiveUpdateFile(opts.path, "failed-write");
      return Result::OtaFail;
    }
    scanner.feed(buf, got);
    written += got;

    // Periodic progress log so a long OTA doesn't look hung on serial.
    if (millis() - lastLog > 2000) {
      LOG.printf("[ota] progress %u/%u (%u%%)\n",
           (unsigned)written, (unsigned)total,
           (unsigned)((written * 100) / (total ? total : 1)));
      lastLog = millis();
    }
  }
  free(buf);
  f.close();

  err = esp_ota_end(handle);
  if (err != ESP_OK) {
    // Includes SHA-256 mismatch (ESP_ERR_OTA_VALIDATE_FAILED). Anything
    // truncated, corrupted mid-transfer, or not a legitimate ESP32
    // image ends up here - current firmware stays intact.
    LOG.printf("[ota] esp_ota_end: %s\n", esp_err_to_name(err));
    archiveUpdateFile(opts.path, "failed-verify");
    return Result::OtaFail;
  }

  // Only NOW check the model tag. We wait until esp_ota_end has
  // validated the image so we don't reject legitimate cross-model
  // images differently from garbage - either way current firmware is
  // safe because we haven't flipped the boot partition yet.
  if (!scanner.found) {
    LOG.printf("[ota] image lacks model tag \"%s\" - refusing to boot it\n",
         kSdOtaTag);
    archiveUpdateFile(opts.path, "failed-wrongmodel");
    return Result::WrongModel;
  }

  err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    LOG.printf("[ota] esp_ota_set_boot_partition: %s\n", esp_err_to_name(err));
    archiveUpdateFile(opts.path, "failed-boot");
    return Result::ApplyFail;
  }

  LOG.printf("[ota] OTA applied, next boot -> %s\n", target->label);
  archiveUpdateFile(opts.path, "applied");
  return Result::Applied;
}

void confirmRunningImage() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    LOG.printf("[ota] marking running image %s valid\n", running->label);
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

}  // namespace sd_ota
