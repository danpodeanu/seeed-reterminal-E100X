// Zenith weather background - extracted from the reTerminal Sticky official
// firmware's /apps/weather/icon_zenith_h.bin (2bpp 4-gray Chinese ink-wash
// landscape at 800x480). Smoothed and re-dithered to each panel's palette.
//
// One data file per model lives next to this blitter; each is guarded with
// `#if RETERMINAL_MODEL == NNNN` so the linker only picks up the payload
// matching the active build. Regenerate everything via
// `python tools/embed_zenith_background.py --all` after updating the source
// PNG or the target model list.
#pragma once

#include <TFT_eSPI.h>
#include <stdint.h>
#include <stddef.h>

namespace zenith_background {

extern const uint16_t kWidth;
extern const uint16_t kHeight;
extern const uint8_t  kBitsPerPixel;   // 1 or 2, MSB-first packing
extern const uint8_t  kLevels;         // number of gray levels stored
extern const size_t   kDataLen;
extern const uint8_t  kData[];

// Blit the zenith background across the whole sprite. Per-model palette
// mapping lives inside the .cpp so callers don't need to know whether the
// current panel is 4-gray, 16-gray, or Spectra-6 BW.
void draw(TFT_eSPI& epaper);

}  // namespace zenith_background
