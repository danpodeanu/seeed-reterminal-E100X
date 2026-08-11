#pragma once

#include <Arduino.h>
#include <stdint.h>

// SD-driven firmware update shared by the viewers and Sticky Arcade: at boot,
// if /update.bin exists on the SD card, we stream it into the inactive
// OTA slot, verify the built-in SHA-256, refuse the update unless the
// image was built for the same board model (E1001/E1002/E1003/E1004/E1005),
// flip the boot pointer, and reboot. On any failure the current
// firmware is untouched and the file is renamed so we don't retry every
// boot.
//
// Prereqs: partition table with app0/app1 OTA slots. See
// common/reterminal_ota.csv — each app's platformio.ini points at it.
//
// UI: this module does no rendering. Callers should render a "Updating
// firmware, please wait" screen before invoking apply() and a "Restart"
// screen after Applied; the actual OTA write takes several seconds.
//
// Kept board-agnostic: the module fingerprints images by scanning the
// running firmware's baked marker string ("reterminal-ota:E1004" and
// friends). The scanning state machine is in sd_ota_pure.h so the
// native test harness can cover it.

namespace sd_ota {

enum class Result : uint8_t {
  NoFile,        // /update.bin not present - not an error, just a no-op.
  ReadFail,      // SD read error mid-stream.
  OtaFail,       // esp_ota_begin / write / end failure (e.g. SHA-256 mismatch).
  WrongModel,    // Image is a valid ESP32 firmware but not this board.
  ApplyFail,     // esp_ota_set_boot_partition returned an error.
  Applied,       // Success. Caller should render "restarting" and reboot.
};

struct Options {
  const char* path = "/update.bin";
};

// Cheap probe: does the SD carry a firmware image we should attempt to
// flash? Callers use this to render a "please wait" screen before
// invoking apply() (which blocks for the OTA write).
bool hasUpdate(const Options& opts = {});

// Stream /update.bin into the inactive OTA slot and, if it validates
// and is tagged for THIS device model, flip the boot pointer. The file
// is renamed to `.applied-<timestamp>` on success or `.failed-<reason>-
// <timestamp>` on any failure, so subsequent boots don't loop.
Result apply(const Options& opts = {});

// Mark the running image valid so ESP-IDF's rollback watchdog won't
// swap us back after 3 boots. Call once early-boot health is
// confirmed - typically after the first successful panel refresh.
// Safe to call every boot; a no-op when no rollback is pending.
void confirmRunningImage();

}  // namespace sd_ota
