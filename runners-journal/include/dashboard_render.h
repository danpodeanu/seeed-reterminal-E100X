#pragma once

// E1005 dashboard renderer for the runners-journal app. Monochrome
// 480x800 portrait. Uses the SD-backed smooth font (sans_bold_*.vlw) so
// Norwegian text (søndag, løp, æøå) renders correctly; falls back to a
// GFX bitmap font (ASCII-only) when the SD card is unavailable.
//
// The smooth-font path on the one-bit E1005 panel can't use TFT_eSPI's
// anti-aliased drawString() — it produces grey levels the panel can't
// show — so loaded glyphs are rasterised pixel-by-pixel with an alpha
// threshold (same approach as xkcd-viewer's drawLoadedSmoothTextMonochrome).

#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>

#include "config.h"
#include "dashboard_data.h"

namespace dashboard_render {

constexpr uint16_t INK = 0x0000;      // panel black
constexpr uint16_t PAPER = 0xFFFF;   // panel white

constexpr int MARGIN = 16;
constexpr int LINE_GAP = 6;

enum class FontSize { Tiny, Small, Medium, Large, Huge };

// E1005 smooth-font pixel sizes. Smaller antialiased cuts lose stroke
// weight on the one-bit panel, so we stay at >= 18px for body text.
struct FontSpec {
  int px;
  const GFXfont* fallback;
};

inline FontSpec fontSpec(FontSize size) {
  switch (size) {
    case FontSize::Huge:   return {48, &FreeSansBold24pt7b};
    case FontSize::Large:  return {28, &FreeSansBold18pt7b};
    case FontSize::Medium: return {22, &FreeSansBold12pt7b};
    case FontSize::Small:  return {18, &FreeSansBold9pt7b};
    case FontSize::Tiny:
    default:               return {16, &FreeSansBold9pt7b};
  }
}

class SmoothFont {
 public:
  explicit SmoothFont(TFT_eSPI& display) : display_(display) {}

  // Load the .vlw from SD for the requested size. Returns false when SD
  // is not ready or the file is missing; in that case callers should
  // select a GFX fallback font and use drawString() instead.
  bool load(FontSize size) {
    const FontSpec spec = fontSpec(size);
    if (spec.px == currentPx_) return true;
    unload();
    const String path = String("/fonts/sans_bold_") + spec.px + ".vlw";
    if (!SD.exists(path)) {
      return false;
    }
    display_.setFreeFont(nullptr);
    display_.loadFont(String("fonts/sans_bold_") + spec.px, SD);
    currentPx_ = spec.px;
    return true;
  }

  void selectGfxFallback(FontSize size) {
    unload();
    display_.setFreeFont(fontSpec(size).fallback);
  }

  void unload() {
    if (currentPx_ == 0) return;
    display_.unloadFont();
    currentPx_ = 0;
  }

  bool loaded() const { return currentPx_ != 0; }
  int px() const { return currentPx_; }

  TFT_eSPI& display() { return display_; }

 private:
  TFT_eSPI& display_;
  int currentPx_ = 0;
};

// Draw `text` with a loaded smooth font using an alpha threshold so the
// glyph renders as solid black on the one-bit panel. Anchored at the
// top-left baseline of the glyph box (caller positions via textWidth).
// Returns false if the font isn't actually loaded (caller should fall
// back to drawString with a GFX font).
inline bool drawSmoothMonochrome(TFT_eSPI& epaper, const String& text,
                                 int left, int top) {
  if (!epaper.fontLoaded || !epaper.fs_font || !epaper.fontFile) {
    return false;
  }
  constexpr uint8_t kSolidAlphaThreshold = 64;
  uint8_t row[256];
  int cursorX = left;
  const int cursorY = top;
  uint16_t offset = 0;
  const uint16_t length = static_cast<uint16_t>(text.length());
  auto* utf8 = reinterpret_cast<uint8_t*>(const_cast<char*>(text.c_str()));
  while (offset < length) {
    const uint16_t code = epaper.decodeUTF8(utf8, &offset, length - offset);
    if (code == 0x20) {
      cursorX += epaper.gFont.spaceWidth;
      continue;
    }
    uint16_t glyph = 0;
    if (!epaper.getUnicodeIndex(code, &glyph)) {
      cursorX += epaper.gFont.spaceWidth;
      continue;
    }
    const uint8_t width = epaper.gWidth[glyph];
    const uint8_t height = epaper.gHeight[glyph];
    const int glyphLeft = cursorX + epaper.gdX[glyph];
    const int glyphTop = cursorY + epaper.gFont.maxAscent - epaper.gdY[glyph];
    if (!epaper.fontFile.seek(epaper.gBitmap[glyph], fs::SeekSet)) {
      return false;
    }
    for (uint8_t y = 0; y < height; ++y) {
      if (epaper.fontFile.read(row, width) != width) return false;
      for (uint8_t x = 0; x < width; ++x) {
        if (row[x] >= kSolidAlphaThreshold) {
          epaper.drawPixel(glyphLeft + x, glyphTop + y, INK);
        }
      }
    }
    cursorX += epaper.gxAdvance[glyph];
  }
  return true;
}

// Measure string width with the currently active font (smooth or GFX).
inline int textWidth(TFT_eSPI& epaper, SmoothFont& font, const String& text) {
  if (font.loaded()) return epaper.textWidth(text, 1);
  return epaper.textWidth(text);
}

// Measure string width for a specific size, loading it temporarily when the
// smooth font path is active (GFX textWidth does not depend on load()).
inline int textWidthFor(TFT_eSPI& epaper, SmoothFont& font, const String& text,
                       FontSize size) {
  if (fontSpec(size).px == font.px()) {
    return epaper.textWidth(text, 1);
  }
  // GFX fallback: select the font first so textWidth measures the right face.
  font.selectGfxFallback(size);
  return epaper.textWidth(text);
}

// Draw a left-aligned string. Uses the smooth font when loaded, else
// selects a GFX bitmap fallback font and uses drawString.
inline void drawText(TFT_eSPI& epaper, SmoothFont& font, const String& text,
                     int left, int top, FontSize size) {
  epaper.setTextColor(INK);
  if (font.loaded() && font.px() == fontSpec(size).px) {
    if (drawSmoothMonochrome(epaper, text, left, top)) return;
  }
  // No smooth font for this size: ensure a GFX fallback is active.
  font.selectGfxFallback(size);
  epaper.drawString(text, left, top);
}

inline int textHeight(TFT_eSPI& epaper, SmoothFont& font) {
  if (font.loaded()) return epaper.gFont.yAdvance;
  return epaper.fontHeight(1);
}

inline void clearPanel(TFT_eSPI& epaper) {
  epaper.fillRect(0, 0, config::PANEL_WIDTH, config::PANEL_HEIGHT, PAPER);
}

inline void drawRule(TFT_eSPI& epaper, int y) {
  epaper.drawFastHLine(MARGIN, y, config::PANEL_WIDTH - 2 * MARGIN, INK);
}

// String formatting helpers (kept inline + simple to avoid pulling printf
// variants for float rendering on the ESP32).
inline String kmString(float km) {
  // One decimal, e.g. "19.3".
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(km));
  return String(buf);
}

inline String pctString(int pct) {
  return String(pct) + "%";
}

// Main dashboard layout. Renders into the panel framebuffer; the caller
// is responsible for committing the frame to the panel (panel refresh).
template <typename EPaper>
inline void renderDashboard(EPaper& epaper, SmoothFont& font,
                            const dashboard::DashboardData& data) {
  clearPanel(epaper);
  int y = MARGIN;

  // Header: week label + "oppdatert" timestamp.
  font.load(FontSize::Small);
  drawText(epaper, font, data.uke.merkelapp, MARGIN, y, FontSize::Small);
  const String updated = String("oppdatert: ") + data.oppdatert;
  {
    const int w = textWidthFor(epaper, font, updated, FontSize::Tiny);
    drawText(epaper, font, updated,
             config::PANEL_WIDTH - MARGIN - w, y, FontSize::Tiny);
  }
  y += textHeight(epaper, font) + LINE_GAP;
  drawRule(epaper, y);
  y += LINE_GAP * 2;

  // Weekly progress: total km + goal percentage.
  font.load(FontSize::Huge);
  {
    const String km = kmString(data.uke.total_km);
    drawText(epaper, font, km, MARGIN, y, FontSize::Huge);
    // Measure km width while the Huge font is still active.
    const int kmW = textWidth(epaper, font, km);
    font.load(FontSize::Medium);
    drawText(epaper, font, "km", MARGIN + kmW + 8, y + 18, FontSize::Medium);
  }
  // Goal percentage on the right.
  font.load(FontSize::Large);
  {
    const String pct = pctString(data.uke.maal_pct);
    const int w = textWidth(epaper, font, pct);
    drawText(epaper, font, pct,
             config::PANEL_WIDTH - MARGIN - w, y, FontSize::Large);
    const String goal = String("av ") + data.maal_km + " km";
    font.load(FontSize::Small);
    const int gw = textWidth(epaper, font, goal);
    drawText(epaper, font, goal,
             config::PANEL_WIDTH - MARGIN - gw, y + 34, FontSize::Small);
  }
  y += 60 + LINE_GAP * 2;
  drawRule(epaper, y);
  y += LINE_GAP * 2;

  // Recent runs.
  font.load(FontSize::Small);
  drawText(epaper, font, "Siste løp", MARGIN, y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  font.load(FontSize::Small);
  const size_t runsToShow =
      data.siste_lop.size() > 5 ? 5 : data.siste_lop.size();
  for (size_t i = 0; i < runsToShow; ++i) {
    const dashboard::RunEntry& r = data.siste_lop[i];
    const String left = String(r.dato) + "  " + r.type;
    const String right = kmString(r.km) + "km  " + r.pace;
    drawText(epaper, font, left, MARGIN, y, FontSize::Small);
    const int rw = textWidth(epaper, font, right);
    drawText(epaper, font, right, config::PANEL_WIDTH - MARGIN - rw, y,
             FontSize::Small);
    y += textHeight(epaper, font) + LINE_GAP;
  }
  y += LINE_GAP;
  drawRule(epaper, y);
  y += LINE_GAP * 2;

  // Type breakdown.
  font.load(FontSize::Small);
  drawText(epaper, font, "Typer", MARGIN, y, FontSize::Small);
  y += textHeight(epaper, font) + LINE_GAP;
  for (const dashboard::TypeEntry& t : data.typer) {
    const String left = String(t.type) + " (" + t.antall + ")";
    const String right = kmString(t.km) + "km";
    drawText(epaper, font, left, MARGIN, y, FontSize::Small);
    const int rw = textWidth(epaper, font, right);
    drawText(epaper, font, right, config::PANEL_WIDTH - MARGIN - rw, y,
             FontSize::Small);
    y += textHeight(epaper, font) + LINE_GAP;
  }
  y += LINE_GAP;
  drawRule(epaper, y);
  y += LINE_GAP * 2;

  // History bar chart (weekly km).
  font.load(FontSize::Tiny);
  drawText(epaper, font, "Historikk", MARGIN, y, FontSize::Tiny);
  y += textHeight(epaper, font) + LINE_GAP;
  if (!data.historikk.empty()) {
    float maxKm = 0.0f;
    for (const dashboard::HistoryEntry& h : data.historikk) {
      if (h.km > maxKm) maxKm = h.km;
    }
    if (maxKm <= 0.0f) maxKm = 1.0f;
    const int chartTop = y;
    const int chartBottom = config::PANEL_HEIGHT - MARGIN;
    const int chartH = chartBottom - chartTop;
    if (chartH > 20) {
      const int n = static_cast<int>(data.historikk.size());
      const int slotW = (config::PANEL_WIDTH - 2 * MARGIN) / n;
      const int barGap = 4;
      for (int i = 0; i < n; ++i) {
        const dashboard::HistoryEntry& h = data.historikk[i];
        const int barH = static_cast<int>(chartH * (h.km / maxKm));
        const int bx = MARGIN + i * slotW + barGap;
        const int bw = slotW - 2 * barGap;
        epaper.fillRect(bx, chartBottom - barH, bw, barH, INK);
        font.load(FontSize::Tiny);
        drawText(epaper, font, h.uke, bx, chartBottom + 2, FontSize::Tiny);
      }
    }
  }

  font.unload();
}

// Minimal status screen for WiFi/fetch/parse failures.
template <typename EPaper>
inline void renderStatus(EPaper& epaper, SmoothFont& font,
                         const String& title, const String& detail) {
  clearPanel(epaper);
  int y = MARGIN + 40;
  font.load(FontSize::Large);
  const int tw = textWidth(epaper, font, title);
  drawText(epaper, font, title, (config::PANEL_WIDTH - tw) / 2, y,
           FontSize::Large);
  y += textHeight(epaper, font) + LINE_GAP * 2;
  font.load(FontSize::Small);
  int dw = textWidth(epaper, font, detail);
  if (dw > config::PANEL_WIDTH - 2 * MARGIN) dw = config::PANEL_WIDTH - 2 * MARGIN;
  drawText(epaper, font, detail, (config::PANEL_WIDTH - dw) / 2, y,
           FontSize::Small);
  font.unload();
}

}  // namespace dashboard_render
