#pragma once

#include <SPI.h>

#include "app_logger.h"
#include "panel_traits.h"

// Bring-up glue that every consumer of the reTerminal e-paper stack uses.
namespace epaper_setup {

// Internal stages remain public for SD bus recovery. Application code should
// call begin() below rather than invoking EPaper::begin() directly.
void prepare();

// Power-cycle the E1003 panel-bias rail and IT8951 core before controller
// initialization. Other models have no separate rails and treat this as a no-op.
void resetPanelPower();

// Shut down and hold the E1003 display rails off through deep sleep.
// Call only after the panel controller has entered sleep.
void shutdownPanelPower();

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
// Historically the viewer apps only worked because they mounted SD straight
// after panel initialization; tools that did not touch SD were left looking
// at a stuck panel until this coupling was untangled.
//
// Idempotent: safe to call more than once.
void finalize(SPIClass& panelSpi);

// The mandatory panel startup sequence: power and settle the shared rail,
// initialize the controller, apply the model's physical orientation, then
// attach the shared SPI bus to its complete pin set. This ordering is required
// after deep sleep and on every model.
template <typename EPaper>
void begin(EPaper& epaper) {
  prepare();
  resetPanelPower();
  epaper.begin();
#if RETERMINAL_MODEL == 1003
  constexpr uint16_t kExpectedVcomMv = 1400;
  uint16_t actualVcomMv = 0;
  for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
    epaper.setTconVcom(kExpectedVcomMv);
    delay(20);
    actualVcomMv = epaper.getTconVcom();
    if (actualVcomMv == kExpectedVcomMv) break;
    if (attempt < 3) delay(100);
  }
  if (actualVcomMv == kExpectedVcomMv) {
    LOG.printf("[panel] E1003 VCOM verified at -%u.%03uV\n",
               static_cast<unsigned>(actualVcomMv / 1000),
               static_cast<unsigned>(actualVcomMv % 1000));
  } else {
    LOG.printf(
        "[panel] ERROR: E1003 VCOM readback=%u mV, expected=%u mV\n",
        static_cast<unsigned>(actualVcomMv),
        static_cast<unsigned>(kExpectedVcomMv));
  }
#endif
  epaper.setRotation(panel_traits::DISPLAY_ROTATION);
  finalize(epaper.getSPIinstance());
}

}  // namespace epaper_setup
