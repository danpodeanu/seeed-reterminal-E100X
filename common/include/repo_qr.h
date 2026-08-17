#pragma once

#include <stdint.h>

namespace repo_qr {

constexpr char kUrl[] =
    "https://github.com/danpodeanu/seeed-reterminal-E100X";
constexpr int kModules = 29;
constexpr uint32_t kRows[kModules] = {
    0x1FCF537FUL, 0x1059B441UL, 0x1749375DUL, 0x175CCA5DUL, 0x17470F5DUL,
    0x10586941UL, 0x1FD5557FUL, 0x00097000UL, 0x1F78E8AAUL, 0x02935771UL,
    0x14647010UL, 0x0C10350AUL, 0x037DE80CUL, 0x061017F1UL, 0x0DDC719CUL,
    0x109C73F2UL, 0x09E3EAACUL, 0x1B3756F5UL, 0x1065B574UL, 0x11AC31C2UL,
    0x15DFEBF7UL, 0x00129D1FUL, 0x1FD87B5CUL, 0x10454712UL, 0x1759F9F4UL,
    0x1756930DUL, 0x175933FEUL, 0x105E51AAUL, 0x1FD7CE74UL,
};

template <typename Display>
inline void drawBottomRight(Display& display, int displayWidth,
                            int displayHeight, int moduleSize, int margin,
                            uint32_t black, uint32_t white) {
  constexpr int kQuietModules = 4;
  const int qrPixels = (kModules + kQuietModules * 2) * moduleSize;
  const int left = displayWidth - qrPixels - margin;
  const int top = displayHeight - qrPixels - margin;

  display.fillRect(left, top, qrPixels, qrPixels, white);
  for (int row = 0; row < kModules; ++row) {
    for (int column = 0; column < kModules; ++column) {
      const uint32_t mask = 1UL << (kModules - 1 - column);
      if ((kRows[row] & mask) == 0) continue;
      display.fillRect(left + (column + kQuietModules) * moduleSize,
                       top + (row + kQuietModules) * moduleSize, moduleSize,
                       moduleSize, black);
    }
  }
}

}  // namespace repo_qr
