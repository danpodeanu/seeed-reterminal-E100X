#pragma once

#include <SPI.h>

// Bring-up glue that every consumer of the reTerminal e-paper stack
// must run once, straight after EPaper::begin(). Kept out of the
// individual apps so the requirement lives in exactly one place.
namespace epaper_setup {

// Finish attaching the panel's SPI bus to real GPIOs and enable the
// shared peripheral power rail.
//
// EPaper::begin() sets the SPI bus up from the Seeed_GFX Setup header,
// but Setup520 (E1001 / UC8179) declares TFT_MISO = -1. That works for
// the smallish 1-bit writes the driver issues in mono mode, but the
// ~192 KB Gray4 push after initGrayMode(GRAY_LEVEL4) silently vanishes
// on the ESP32-S3 SPI peripheral - no error, panel just never refreshes.
// The fix is to re-init the same SPI instance with the real MISO pin
// (board::PIN_SD_MISO), which is what SD.begin() happens to do as a
// side effect via sd_card::mount().
//
// Historically the viewer apps only worked because they mounted SD
// straight after epaper.begin(); tools that don't touch SD were left
// looking at a stuck panel until this coupling was untangled. Any new
// app or tool that talks to the panel must call this helper directly.
//
// Idempotent: safe to call more than once.
void finalize(SPIClass& panelSpi);

}  // namespace epaper_setup
