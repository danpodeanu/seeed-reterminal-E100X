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
// On-panel rendering helpers for the reusable configuration portal.
// Header-only template; the caller passes in whichever EPaper object it
// already owns. This is a near-verbatim copy of
// common-sd-web/include/sd_web_portal_ui.h under a different namespace
// so future viewers can depend on `common-config-portal` without also
// pulling in the SD-web library. Any layout tweaks made here should be
// mirrored to sd_web_portal_ui.h (and vice versa).
namespace config_portal {
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
  const char* title = nullptr;  // overrides "SD Card Portal" header when set
  const char* tagline;      // 2-3 word description printed under the title
  String ssid;
  String wifiPassword;      // optional; rendered as a second detail line
                            // under the SSID when non-empty
  String url;
  String macAddress;
  String wifiPayload;
  String urlPayload;
  // Optional third QR (e.g. "Help" -> GitHub README URL). Rendered
  // smaller than the two main QRs. When helpPayload is empty the
  // renderer omits this block entirely.
  String helpPayload;
  String helpCaption;       // e.g. "Help"
  Fonts fonts;
};

// Full-screen welcome layout. From top to bottom:
//   * Title band                                      (info.fonts.titleFont)
//   * Tagline (e.g. "Files over Wi-Fi")               (info.fonts.subtitleFont)
//   * Device MAC line                                 (info.fonts.detailFont)
//   * Two QR codes side by side (landscape) or stacked (portrait):
//       - "1. Scan to join Wi-Fi" caption ABOVE       (info.fonts.captionFont)
//       - Wi-Fi QR
//       - "Wi-Fi Name: <ssid>" BELOW                  (info.fonts.captionFont)
//       - "Wi-Fi Password: <pw>" BELOW                (info.fonts.captionFont)
//       - "2. Scan to open portal" caption ABOVE
//       - URL QR
//       - URL text BELOW                              (info.fonts.captionFont)
//   * Footer hint centred at the bottom               (info.fonts.captionFont)
//       "Press green button to return to viewer"
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
  epaper.drawString(info.title && info.title[0] ? info.title : "SD Card Portal",
                    panelW / 2, titleY, 1);

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
  // Reserve enough space at the bottom for the "Press green button to
  // return to viewer" footer hint, drawn in captionFont so it's
  // readable from arm's length. Margin above matches the general
  // panelH/60 spacing used elsewhere; margin below is a hair smaller
  // so the text sits flush without touching the edge.
  const int footerHintMargin = panelH / 60;
  const int footerReserve = captionH + footerHintMargin + panelH / 200;
  const int qrRegionTop = headerBottom;
  const int qrRegionBottom = panelH - footerReserve;

  int moduleSize;
  int helpModule = 0;
  int helpSide = 0;
  if (info.helpPayload.length() > 0) {
    // Pick a fixed help module size based on panel width so the corner
    // QR is roughly the same visual weight on every board. Bounds keep
    // it scannable without dominating the layout.
    helpModule = panelW / 160;
    if (helpModule < 2) helpModule = 2;
    if (helpModule > 4) helpModule = 4;
    helpSide = qrSidePixels(info.helpPayload, helpModule);
  }

  // The Wi-Fi block always shows "Wifi name: <ssid>" underneath the QR;
  // when the AP has a password we add a second "Wifi password: <pw>"
  // line right below it. Compute the count up front so QR sizing can
  // reserve space for both lines. The URL block always has one detail
  // line ("http://192.168.1.1"), so it needs the same reservation.
  const bool wifiHasPass = info.wifiPassword.length() > 0;
  const int wifiDetailLines = wifiHasPass ? 2 : 1;
  // Detail lines under the QR are drawn in captionFont (one size up
  // from detailFont) so SSID/password/URL stay legible while the user
  // types them on a phone across the room. The Device MAC header line
  // stays at detailFont because it's informational, not a scan target.
  const int bigDetailH = captionH;
  if (portrait) {
    // Two stacked blocks. Reserve for the worst-case block (Wi-Fi with
    // two detail lines) so the QR shrinks enough to fit both.
    const int blockNonQr = captionH + wifiDetailLines * bigDetailH + panelH / 60;
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
    // minus caption + detail lines; QR width limited by half panel width
    // minus side gaps. Reserve for the Wi-Fi block's detail-line count so
    // both blocks stay vertically aligned.
    const int qrTargetH = (qrRegionBottom - qrRegionTop) - captionH -
                          wifiDetailLines * bigDetailH - panelH / 60;
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
                       const String& detailLine,
                       const String& detailLine2 = String()) {
    // Caption above.
    epaper.setFreeFont(info.fonts.captionFont);
    epaper.setTextDatum(TC_DATUM);
    epaper.drawString(caption, blockX + qrSide / 2, blockTop, 1);
    const int qrTop = blockTop + captionH + panelH / 200;
    drawQr(epaper, payload, blockX, qrTop, moduleSize, black, white);
    // Detail below. Reuse captionFont (one size up from detailFont) so
    // the SSID / password / URL are readable across the room.
    epaper.setFreeFont(info.fonts.captionFont);
    int detailY = qrTop + qrSide + panelH / 200;
    epaper.drawString(detailLine, blockX + qrSide / 2, detailY, 1);
    if (detailLine2.length() > 0) {
      detailY += bigDetailH;
      epaper.drawString(detailLine2, blockX + qrSide / 2, detailY, 1);
    }
  };

  // "Wi-Fi Name: <ssid>" / "Wi-Fi Password: <pw>" - full labels so the
  // pane reads naturally even for people who've never used the portal
  // before. Kept as locals so both layouts reuse them.
  const String wifiNameLine = String("Wi-Fi Name: ") + info.ssid;
  const String wifiPassLine =
      wifiHasPass ? String("Wi-Fi Password: ") + info.wifiPassword : String();

  if (portrait) {
    const int wifiExtra = wifiHasPass ? bigDetailH : 0;
    const int wifiBlockH =
        captionH + wifiSide + bigDetailH + wifiExtra + panelH / 60;
    const int urlBlockH = captionH + urlSide + bigDetailH + panelH / 60;
    const int totalH = wifiBlockH + urlBlockH;
    const int available = qrRegionBottom - qrRegionTop;
    const int extra = available > totalH ? (available - totalH) : 0;
    const int wifiTop = qrRegionTop + extra / 4;
    const int urlTop = wifiTop + wifiBlockH + extra / 2;
    const int wifiX = (panelW - wifiSide) / 2;
    const int urlX = (panelW - urlSide) / 2;
    drawBlock(wifiX, wifiTop, wifiSide, info.wifiPayload,
              "1. Scan to join Wi-Fi", wifiNameLine, wifiPassLine);
    drawBlock(urlX, urlTop, urlSide, info.urlPayload,
              "2. Scan to open portal", info.url);

    // Third QR (Help) sits to the right of the URL QR, with its bottom
    // aligned to the URL QR's bottom so the pair reads as one row.
    if (info.helpPayload.length() > 0) {
      const int urlQrTop = urlTop + captionH + panelH / 200;
      const int urlQrBottom = urlQrTop + urlSide;
      const int helpX = panelW - helpSide - panelW / 30;
      const int helpQrTop = urlQrBottom - helpSide;
      const int helpCaptionTop = helpQrTop - captionH - panelH / 200;
      epaper.setFreeFont(info.fonts.captionFont);
      epaper.setTextDatum(TC_DATUM);
      epaper.drawString(info.helpCaption.length() ? info.helpCaption.c_str()
                                                  : "Help",
                        helpX + helpSide / 2, helpCaptionTop, 1);
      drawQr(epaper, info.helpPayload, helpX, helpQrTop, helpModule, black,
             white);
    }
  } else {
    const int gap = (panelW - wifiSide - urlSide) / 3;
    const int wifiX = gap;
    const int urlX = wifiX + wifiSide + gap;
    const int blockH =
        captionH + wifiSide + wifiDetailLines * bigDetailH + panelH / 60;
    const int available = qrRegionBottom - qrRegionTop;
    const int blockTop =
        qrRegionTop + (available > blockH ? (available - blockH) / 2 : 0);
    drawBlock(wifiX, blockTop, wifiSide, info.wifiPayload,
              "1. Scan to join Wi-Fi", wifiNameLine, wifiPassLine);
    drawBlock(urlX, blockTop, urlSide, info.urlPayload,
              "2. Scan to open portal", info.url);

    // Small third QR (Help) tucked in the bottom-right corner of the
    // landscape layout. Only rendered when the caller supplied a
    // payload.
    if (info.helpPayload.length() > 0) {
      const int helpX = panelW - helpSide - panelW / 30;
      const int helpTop = qrRegionBottom - helpSide - captionH - panelH / 60;
      epaper.setFreeFont(info.fonts.captionFont);
      epaper.setTextDatum(TC_DATUM);
      epaper.drawString(info.helpCaption.length() ? info.helpCaption.c_str()
                                                  : "Help",
                        helpX + helpSide / 2, helpTop, 1);
      drawQr(epaper, info.helpPayload, helpX,
             helpTop + captionH + panelH / 200, helpModule, black, white);
    }
  }

  // Footer hint centred at the bottom. Rendered in captionFont so
  // it matches the SSID/URL text weight and stays legible from a few
  // feet away. Drawn last so it can't be overwritten by QR blocks.
  epaper.setFreeFont(info.fonts.captionFont);
  epaper.setTextDatum(BC_DATUM);
  epaper.drawString("Press green button to return to viewer", panelW / 2,
                    panelH - panelH / 200, 1);

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
}  // namespace config_portal
