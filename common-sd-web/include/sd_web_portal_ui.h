#pragma once

#include <Arduino.h>
#include <qrcode.h>

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
  // Pick the smallest version that fits with ECC_MEDIUM. Version 3 is
  // 29x29 modules and holds ~47 alphanumeric chars; version 5 (37x37)
  // holds ~84. Bigger versions need more RAM for the internal buffer,
  // hence not just always picking 10.
  uint8_t version = 3;
  if (payload.length() > 40) version = 5;
  if (payload.length() > 75) version = 8;
  if (payload.length() > 130) version = 12;

  const uint16_t bufSize = qrcode_getBufferSize(version);
  // Stack buffer up to a reasonable ceiling; anything larger than
  // version 12 (~300 bytes) is unlikely for this portal.
  uint8_t buffer[512];
  if (bufSize > sizeof(buffer)) return;

  QRCode qr;
  if (qrcode_initText(&qr, buffer, version, ECC_MEDIUM, payload.c_str()) < 0) {
    return;
  }

  const int side = qr.size;
  const int quiet = 4;
  const int totalSide = (side + 2 * quiet) * moduleSize;

  // Paint the entire QR bounding box (quiet zone included) white.
  epaper.fillRect(x, y, totalSide, totalSide, white);

  // Draw dark modules.
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
  // Version N -> 4*N + 17 modules per side.
  const int side = 4 * version + 17;
  return (side + 8) * moduleSize;
}

struct RenderInfo {
  const char* modelLabel;   // e.g. "reTerminal E1001"
  String ssid;              // "ReTerminal ABCDEF"
  String url;               // "http://192.168.1.1/"
  String macAddress;        // "AA:BB:CC:DD:EE:FF" (station MAC)
  String wifiPayload;       // "WIFI:S:...;T:nopass;;"
  String urlPayload;        // same as `url`
};

// Full-screen "welcome" layout: title top, two QR codes side by side
// (landscape) or stacked (portrait), captions, then the AP details at
// the bottom. Fonts and colours are picked to look reasonable on every
// reTerminal E-series panel.
template <typename EPaper>
inline void renderPortalScreen(EPaper& epaper, int panelW, int panelH,
                               uint32_t black, uint32_t white,
                               const RenderInfo& info) {
  epaper.fillSprite(white);
  epaper.setTextColor(black, white, true);

  // Pick a QR module size that fills roughly one-third of the smaller
  // panel dimension - large enough for a phone camera to lock onto
  // from arm's length, small enough to leave room for the caption.
  // Both QR codes use the same module size so the two blocks match.
  const bool portrait = panelH > panelW;
  const int targetSide = portrait ? panelW / 2 : (panelH - panelH / 5) - panelH / 8;
  const int wifiSideModules = 4 * (info.wifiPayload.length() > 40 ? 5 : 3) + 17 + 8;
  const int urlSideModules  = 4 * (info.urlPayload.length()  > 40 ? 5 : 3) + 17 + 8;
  const int maxModules = wifiSideModules > urlSideModules ? wifiSideModules : urlSideModules;
  int moduleSize = targetSide / maxModules;
  if (moduleSize < 2) moduleSize = 2;

  const int wifiSide = qrSidePixels(info.wifiPayload, moduleSize);
  const int urlSide  = qrSidePixels(info.urlPayload,  moduleSize);

  // Fonts (fall back to built-in for tiny panels).
  const int titleFontId = panelW >= 1200 ? 6 : 4;
  const int labelFontId = panelW >= 1200 ? 4 : 2;
  const int bodyFontId  = panelW >= 1200 ? 4 : 2;

  // Title band.
  epaper.setTextDatum(TC_DATUM);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(titleFontId);
  const int titleY = panelH / 20;
  epaper.drawString("SD Card Wi-Fi Portal", panelW / 2, titleY, titleFontId);

  epaper.setTextFont(labelFontId);
  epaper.drawString(String("on ") + info.modelLabel, panelW / 2,
                    titleY + epaper.fontHeight(titleFontId), labelFontId);

  // Layout: two QR codes.
  int qrTop;
  int wifiX, urlX, wifiCaptionY, urlCaptionY;
  if (portrait) {
    qrTop = titleY + epaper.fontHeight(titleFontId) +
            epaper.fontHeight(labelFontId) + panelH / 30;
    wifiX = (panelW - wifiSide) / 2;
    urlX  = (panelW - urlSide)  / 2;
    // Stack: Wi-Fi on top, URL below.
    wifiCaptionY = qrTop + wifiSide + panelH / 90;
    urlCaptionY  = wifiCaptionY + epaper.fontHeight(labelFontId) * 3 + urlSide + panelH / 90;
  } else {
    qrTop = titleY + epaper.fontHeight(titleFontId) +
            epaper.fontHeight(labelFontId) + panelH / 20;
    // Two blocks: [left third | wifi | middle third | url | right third]
    const int totalWidth = wifiSide + urlSide;
    const int gap = (panelW - totalWidth) / 3;
    wifiX = gap;
    urlX  = wifiX + wifiSide + gap;
    wifiCaptionY = qrTop + wifiSide + panelH / 40;
    urlCaptionY  = wifiCaptionY;
  }

  // Draw the two QR codes.
  drawQr(epaper, info.wifiPayload, wifiX, qrTop, moduleSize, black, white);
  int urlTopY = qrTop;
  if (portrait) {
    urlTopY = wifiCaptionY + epaper.fontHeight(labelFontId) * 2 + panelH / 40;
  }
  drawQr(epaper, info.urlPayload, urlX, urlTopY, moduleSize, black, white);

  // Captions.
  epaper.setTextFont(labelFontId);
  epaper.setTextDatum(TC_DATUM);
  epaper.drawString("1. Scan to join Wi-Fi", wifiX + wifiSide / 2,
                    wifiCaptionY, labelFontId);
  epaper.drawString(info.ssid, wifiX + wifiSide / 2,
                    wifiCaptionY + epaper.fontHeight(labelFontId),
                    labelFontId);

  const int urlCapY = portrait
                          ? urlTopY + urlSide + panelH / 90
                          : urlCaptionY;
  epaper.drawString("2. Scan to open portal", urlX + urlSide / 2,
                    urlCapY, labelFontId);
  epaper.drawString(info.url, urlX + urlSide / 2,
                    urlCapY + epaper.fontHeight(labelFontId), labelFontId);

  // Footer with MAC + instructions.
  epaper.setTextFont(bodyFontId);
  epaper.setTextDatum(BC_DATUM);
  epaper.drawString(String("Device MAC ") + info.macAddress,
                    panelW / 2, panelH - panelH / 50, bodyFontId);
  epaper.setTextDatum(TL_DATUM);
}

// Simple full-screen error banner used when the SD card is missing (or
// any other fatal precondition failed). Renders a centred title plus
// up to two lines of detail; caller is responsible for `epaper.update()`.
template <typename EPaper>
inline void renderErrorScreen(EPaper& epaper, int panelW, int panelH,
                              uint32_t black, uint32_t white,
                              const char* title, const char* line1,
                              const char* line2 = nullptr) {
  epaper.fillSprite(white);
  epaper.setTextColor(black, white, true);
  epaper.setFreeFont(nullptr);
  const int titleFontId = panelW >= 1200 ? 8 : 6;
  const int bodyFontId = panelW >= 1200 ? 6 : 4;
  epaper.setTextFont(titleFontId);
  epaper.setTextDatum(MC_DATUM);
  const int centerY = panelH / 2 - epaper.fontHeight(titleFontId);
  epaper.drawString(title, panelW / 2, centerY, titleFontId);

  epaper.setTextFont(bodyFontId);
  const int gap = epaper.fontHeight(bodyFontId);
  int y = centerY + epaper.fontHeight(titleFontId);
  if (line1 && line1[0]) {
    epaper.drawString(line1, panelW / 2, y, bodyFontId);
    y += gap;
  }
  if (line2 && line2[0]) {
    epaper.drawString(line2, panelW / 2, y, bodyFontId);
  }
  epaper.setTextDatum(TL_DATUM);
}

}  // namespace ui
}  // namespace sd_web_portal
