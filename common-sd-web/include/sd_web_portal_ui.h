#pragma once

#include <Arduino.h>
#include <qrcode.h>

// The `GFXfont` type used by Adafruit_GFX / TFT_eSPI is a typedef around
// an anonymous struct, so it can't be forward-declared with
// `struct GFXfont;`. Callers of this header must have included
// TFT_eSPI.h (or another header that pulls in gfxfont.h) before this
// one; both viewer apps in the tree already do so via `<TFT_eSPI.h>`
// in their main.cpp.

// On-panel rendering helpers for the SD web portal. Kept as a
// template header so the sd_web_portal library never has to link
// against Seeed_GFX / TFT_eSPI - the caller passes in whichever EPaper
// object it already owns.
//
// The renderer expects the caller to have already fillSprite'd the
// panel to the background colour and initialised the desired gray
// mode. It draws in-place; the caller is responsible for calling
// epaper.update() afterwards.
namespace sd_web_portal {
namespace ui {

// Draw a QR code with the given payload at (x, y) top-left with
// `moduleSize` pixels per QR module. Version is auto-selected: 3 for
// short payloads, 5 for longer WPA strings. ECC medium.
//
// The rendered dimension is (module_count + 8) * moduleSize on each
// side (adds a 4-module quiet zone on every edge, as required by the
// QR spec).
template <typename EPaper>
inline void drawQr(EPaper& epaper, const String& payload, int x, int y,
                   int moduleSize, uint32_t black, uint32_t white) {
  uint8_t version = 3;
  if (payload.length() > 40) version = 5;
  if (payload.length() > 75) version = 8;
  if (payload.length() > 130) version = 12;

  const uint16_t bufSize = qrcode_getBufferSize(version);
  uint8_t buffer[512];
  if (bufSize > sizeof(buffer)) return;

  QRCode qr;
  if (qrcode_initText(&qr, buffer, version, ECC_MEDIUM, payload.c_str()) < 0) {
    return;
  }

  const int side = qr.size;
  const int quiet = 4;
  const int totalSide = (side + 2 * quiet) * moduleSize;

  epaper.fillRect(x, y, totalSide, totalSide, white);
  for (int row = 0; row < side; ++row) {
    for (int col = 0; col < side; ++col) {
      if (qrcode_getModule(&qr, col, row)) {
        epaper.fillRect(x + (quiet + col) * moduleSize,
                        y + (quiet + row) * moduleSize,
                        moduleSize, moduleSize, black);
      }
    }
  }
}

// Return the pixel side length that drawQr will produce for the given
// payload at the given module size. Useful for layout maths.
inline int qrSidePixels(const String& payload, int moduleSize) {
  uint8_t version = 3;
  if (payload.length() > 40) version = 5;
  if (payload.length() > 75) version = 8;
  if (payload.length() > 130) version = 12;
  const int side = 4 * version + 17;
  return (side + 8) * moduleSize;
}

// Font pointers supplied by the caller. All four must be non-null; the
// renderer sets them via epaper.setFreeFont() and draws at TFT_eSPI
// virtual font id 1 (the "current free font" slot).
//
//   titleFont     - big header ("SD Card Portal")
//   subtitleFont  - short tagline immediately below the title
//   captionFont   - "1. Scan to join Wi-Fi" / "2. Scan to open portal"
//   detailFont    - SSID / URL / MAC lines
struct Fonts {
  const GFXfont* titleFont;
  const GFXfont* subtitleFont;
  const GFXfont* captionFont;
  const GFXfont* detailFont;
};

struct RenderInfo {
  const char* modelLabel;
  const char* tagline;      // 2-3 word description printed under the title
  String ssid;
  String url;
  String macAddress;
  String wifiPayload;
  String urlPayload;
  Fonts fonts;
};

// Full-screen welcome layout. From top to bottom:
//   * Title band                                      (info.fonts.titleFont)
//   * Tagline (e.g. "Files over Wi-Fi")               (info.fonts.subtitleFont)
//   * Device MAC line                                 (info.fonts.detailFont)
//   * Two QR codes side by side (landscape) or stacked (portrait):
//       - "1. Scan to join Wi-Fi" caption ABOVE       (info.fonts.captionFont)
//       - Wi-Fi QR
//       - SSID text BELOW                             (info.fonts.detailFont)
//       - "2. Scan to open portal" caption ABOVE
//       - URL QR
//       - URL text BELOW
template <typename EPaper>
inline void renderPortalScreen(EPaper& epaper, int panelW, int panelH,
                               uint32_t black, uint32_t white,
                               const RenderInfo& info) {
  epaper.fillSprite(white);
  epaper.setTextColor(black, white, true);

  const bool portrait = panelH > panelW;

  // ---- Header block: title + tagline + MAC. ----
  epaper.setTextDatum(TC_DATUM);

  epaper.setFreeFont(info.fonts.titleFont);
  const int titleFontH = epaper.fontHeight(1);
  const int titleY = panelH / 30;
  epaper.drawString("SD Card Portal", panelW / 2, titleY, 1);

  epaper.setFreeFont(info.fonts.subtitleFont);
  const int subtitleFontH = epaper.fontHeight(1);
  int y = titleY + titleFontH + panelH / 90;
  if (info.tagline && info.tagline[0]) {
    epaper.drawString(info.tagline, panelW / 2, y, 1);
    y += subtitleFontH;
  }

  epaper.setFreeFont(info.fonts.detailFont);
  const int detailFontH = epaper.fontHeight(1);
  y += panelH / 120;
  epaper.drawString(String("Device MAC ") + info.macAddress, panelW / 2, y, 1);
  y += detailFontH;

  // ---- QR sizing. ----
  // Reserve room for two captions + two QR + two SSID/URL lines. In
  // landscape the two blocks sit side by side, so we only need the
  // vertical space of one block; in portrait we need both stacked.
  epaper.setFreeFont(info.fonts.captionFont);
  const int captionH = epaper.fontHeight(1);

  const int headerBottom = y + panelH / 60;
  const int footerReserve = panelH / 40;
  const int qrRegionTop = headerBottom;
  const int qrRegionBottom = panelH - footerReserve;

  int moduleSize;
  if (portrait) {
    // Two stacked blocks: each block = caption + qr + detail + gap.
    // The QR square is the biggest component; give it the remaining
    // vertical space after captions/details.
    const int blockNonQr = captionH + detailFontH + panelH / 60;
    const int perBlockH = (qrRegionBottom - qrRegionTop) / 2;
    const int qrTargetH = perBlockH - blockNonQr;
    const int qrTargetW = panelW - panelW / 6;
    const int qrTarget = qrTargetH < qrTargetW ? qrTargetH : qrTargetW;
    const int wifiModulesSide =
        4 * (info.wifiPayload.length() > 40 ? 5 : 3) + 17 + 8;
    const int urlModulesSide =
        4 * (info.urlPayload.length() > 40 ? 5 : 3) + 17 + 8;
    const int maxModules =
        wifiModulesSide > urlModulesSide ? wifiModulesSide : urlModulesSide;
    moduleSize = qrTarget / maxModules;
  } else {
    // Landscape: side-by-side blocks. QR height limited by region height
    // minus caption+detail; QR width limited by half panel width minus
    // side gaps.
    const int qrTargetH = (qrRegionBottom - qrRegionTop) - captionH -
                          detailFontH - panelH / 60;
    const int qrTargetW = (panelW - panelW / 4) / 2;
    const int qrTarget = qrTargetH < qrTargetW ? qrTargetH : qrTargetW;
    const int wifiModulesSide =
        4 * (info.wifiPayload.length() > 40 ? 5 : 3) + 17 + 8;
    const int urlModulesSide =
        4 * (info.urlPayload.length() > 40 ? 5 : 3) + 17 + 8;
    const int maxModules =
        wifiModulesSide > urlModulesSide ? wifiModulesSide : urlModulesSide;
    moduleSize = qrTarget / maxModules;
  }
  if (moduleSize < 2) moduleSize = 2;
  const int wifiSide = qrSidePixels(info.wifiPayload, moduleSize);
  const int urlSide = qrSidePixels(info.urlPayload, moduleSize);

  // ---- QR layout. ----
  auto drawBlock = [&](int blockX, int blockTop, int qrSide,
                       const String& payload, const char* caption,
                       const String& detailLine) {
    // Caption above.
    epaper.setFreeFont(info.fonts.captionFont);
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString(caption, blockX + qrSide / 2, blockTop, 1);
    const int qrTop = blockTop + captionH + panelH / 200;
    drawQr(epaper, payload, blockX, qrTop, moduleSize, black, white);
    // Detail below.
    epaper.setFreeFont(info.fonts.detailFont);
    epaper.drawString(detailLine, blockX + qrSide / 2,
                      qrTop + qrSide + panelH / 200, 1);
  };

  if (portrait) {
    const int wifiBlockH = captionH + wifiSide + detailFontH + panelH / 60;
    const int urlBlockH = captionH + urlSide + detailFontH + panelH / 60;
    const int totalH = wifiBlockH + urlBlockH;
    const int available = qrRegionBottom - qrRegionTop;
    const int extra = available > totalH ? (available - totalH) : 0;
    const int wifiTop = qrRegionTop + extra / 4;
    const int urlTop = wifiTop + wifiBlockH + extra / 2;
    const int wifiX = (panelW - wifiSide) / 2;
    const int urlX = (panelW - urlSide) / 2;
    drawBlock(wifiX, wifiTop, wifiSide, info.wifiPayload,
              "1. Scan to join Wi-Fi", info.ssid);
    drawBlock(urlX, urlTop, urlSide, info.urlPayload,
              "2. Scan to open portal", info.url);
  } else {
    const int gap = (panelW - wifiSide - urlSide) / 3;
    const int wifiX = gap;
    const int urlX = wifiX + wifiSide + gap;
    const int blockH = captionH + wifiSide + detailFontH + panelH / 60;
    const int available = qrRegionBottom - qrRegionTop;
    const int blockTop =
        qrRegionTop + (available > blockH ? (available - blockH) / 2 : 0);
    drawBlock(wifiX, blockTop, wifiSide, info.wifiPayload,
              "1. Scan to join Wi-Fi", info.ssid);
    drawBlock(urlX, blockTop, urlSide, info.urlPayload,
              "2. Scan to open portal", info.url);
  }

  // Restore default text state so anything printed later renders sanely.
  epaper.setFreeFont(nullptr);
  epaper.setTextDatum(TL_DATUM);
}

// Simple full-screen error banner used when the SD card is missing (or
// any other fatal precondition failed). Renders a centred title plus
// up to two lines of detail; caller is responsible for `epaper.update()`.
template <typename EPaper>
inline void renderErrorScreen(EPaper& epaper, int panelW, int panelH,
                              uint32_t black, uint32_t white,
                              const GFXfont* titleFont,
                              const GFXfont* bodyFont, const char* title,
                              const char* line1,
                              const char* line2 = nullptr) {
  epaper.fillSprite(white);
  epaper.setTextColor(black, white, true);
  epaper.setFreeFont(titleFont);
  epaper.setTextDatum(MC_DATUM);
  const int titleH = epaper.fontHeight(1);
  const int centerY = panelH / 2 - titleH;
  epaper.drawString(title, panelW / 2, centerY, 1);

  epaper.setFreeFont(bodyFont);
  const int bodyH = epaper.fontHeight(1);
  int y = centerY + titleH;
  if (line1 && line1[0]) {
    epaper.drawString(line1, panelW / 2, y, 1);
    y += bodyH;
  }
  if (line2 && line2[0]) {
    epaper.drawString(line2, panelW / 2, y, 1);
  }
  epaper.setFreeFont(nullptr);
  epaper.setTextDatum(TL_DATUM);
}

}  // namespace ui
}  // namespace sd_web_portal
