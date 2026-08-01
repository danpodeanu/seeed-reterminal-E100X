// Zenith weather background - extracted from the reTerminal Sticky official
// firmware's /apps/weather/icon_zenith_h.bin (2bpp 4-gray Chinese ink-wash
// landscape at 800x480). Smoothed and re-dithered to the E1001 palette.
//
// The blitter is compiled only for E1001 (the only viewer at 800x480 4-gray).
// Other panels will still link the PROGMEM data (small enough for a test) but
// won't draw it. Regenerate via `tools/embed_zenith_background.py` after
// updating the source PNG.
#pragma once

#include <TFT_eSPI.h>
#include <stdint.h>
#include <stddef.h>

namespace zenith_background {

extern const uint16_t kWidth;
extern const uint16_t kHeight;
extern const size_t kDataLen;
extern const uint8_t kData[];

// Draw the 800x480 zenith background over the whole sprite. Palette indices
// 0..3 (dark to light) are mapped to TFT_GRAY_0..TFT_GRAY_3 so the picture
// composites correctly against the E1001 four-gray output. Safe to call
// before any drawHeader / drawIcons overlays.
void draw(TFT_eSPI& epaper);

}  // namespace zenith_background
