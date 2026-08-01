#include "zenith_background.h"

#include <pgmspace.h>

namespace zenith_background {

void draw(TFT_eSPI& epaper) {
  // Map the 4-level palette (0=black .. 3=white) to the E1001 gray
  // constants. Any TFT_eSPI-compatible sprite in 4bpp mode accepts these
  // as colour indices, so drawPixel emits one 2-bit slot per call.
  const uint32_t grays[4] = {
      TFT_GRAY_0,  // ink
      TFT_GRAY_1,
      TFT_GRAY_2,
      TFT_GRAY_3,  // paper / near-white
  };
  const uint16_t w = kWidth;
  const uint16_t h = kHeight;
  const uint8_t* row = kData;
  for (uint16_t y = 0; y < h; ++y) {
    for (uint16_t x = 0; x < w; x += 4) {
      const uint8_t b = pgm_read_byte(row + (x >> 2));
      epaper.drawPixel(x + 0, y, grays[(b >> 6) & 0x3]);
      epaper.drawPixel(x + 1, y, grays[(b >> 4) & 0x3]);
      epaper.drawPixel(x + 2, y, grays[(b >> 2) & 0x3]);
      epaper.drawPixel(x + 3, y, grays[(b >> 0) & 0x3]);
    }
    row += (w >> 2);
  }
}

}  // namespace zenith_background
