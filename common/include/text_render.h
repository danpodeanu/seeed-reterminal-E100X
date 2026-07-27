#pragma once

#include <Arduino.h>
#include <stdint.h>

#include <string>

#include "text_render_pure.h"

// Text rendering helpers shared by every viewer app. Kept template-only
// so common/ doesn't need to depend on Seeed_GFX / TFT_eSPI headers -
// the caller's `EPaper` type just has to expose the textWidth() and
// draw{Rect,String,fillRect} methods each app already uses.
namespace text_render {

// Truncate `text` so it fits inside `maxWidth` pixels when rendered
// with the given TFT_eSPI font id, appending "..." when a truncation
// happens. When the text already fits it is returned unchanged.  Bytes
// are removed one at a time from the tail, but UTF-8 continuation
// bytes (0x80-0xBF) are always removed together with their lead byte
// so we never leave a truncated codepoint dangling in front of the
// "..." suffix.
template <typename EPaper>
inline String ellipsize(EPaper& epaper, String text, int maxWidth,
                        uint8_t font = 1) {
  if (epaper.textWidth(text, font) <= maxWidth) return text;
  const String suffix = "...";
  while (text.length() > 1 &&
         epaper.textWidth(text + suffix, font) > maxWidth) {
    text.remove(text.length() - 1);
    // Also drop trailing UTF-8 continuation bytes so the string
    // never ends mid-codepoint.
    while (text.length() > 0 &&
           (static_cast<uint8_t>(text[text.length() - 1]) & 0xC0) == 0x80) {
      text.remove(text.length() - 1);
    }
  }
  text.trim();
  return text + suffix;
}

// Normalize an arbitrary Unicode string for on-panel rendering:
// unescape HTML entities that appear in xkcd/weather feeds, strip
// control bytes, collapse whitespace runs, and trim both ends.  Valid
// UTF-8 sequences are preserved so a loaded smooth font (.vlw) can
// render Unicode glyphs; the built-in TFT_eSPI bitmap fonts still ignore
// bytes >= 0x80, so those callers see a single glyph slot per multi-byte
// codepoint - but this is a strict improvement over the previous
// "replace every non-ASCII byte with '?'" behavior.
//
// The byte-level logic lives in text_render_pure.h so the native test
// env can exercise it without pulling in Arduino headers.
inline String displayText(const String& value) {
  const std::string normalized = text_render::pure::displayText(
      std::string(value.c_str(), value.length()));
  return String(normalized.c_str());
}

// Word-wrap `source` into `lines[0..maxLines)` so each line fits in
// `maxWidth` pixels when drawn with the given font id. Overflow lines
// are elided with "..." on the last visible line. Returns the number of
// lines actually filled; always at least 1 (falling back to "xkcd" when
// the input trims to empty).
template <typename EPaper>
inline int wrapText(EPaper& epaper, const String& source, String* lines,
                    int maxLines, int maxWidth, uint8_t font) {
  String text = displayText(source);
  int lineCount = 0;
  int start = 0;

  while (start < static_cast<int>(text.length()) && lineCount < maxLines) {
    while (start < static_cast<int>(text.length()) && text[start] == ' ')
      ++start;
    if (start >= static_cast<int>(text.length())) break;

    String line;
    while (start < static_cast<int>(text.length())) {
      int end = text.indexOf(' ', start);
      if (end < 0) end = text.length();
      const String word = text.substring(start, end);
      const String candidate = line.isEmpty() ? word : line + " " + word;
      if (!line.isEmpty() &&
          epaper.textWidth(candidate, font) > maxWidth)
        break;
      line = candidate;
      start = end + 1;
      if (epaper.textWidth(line, font) > maxWidth) {
        line = ellipsize(epaper, line, maxWidth, font);
        break;
      }
    }

    if (lineCount == maxLines - 1 &&
        start < static_cast<int>(text.length())) {
      line = ellipsize(epaper, line + "...", maxWidth, font);
    }
    lines[lineCount++] = line;
  }

  if (lineCount == 0) {
    lines[0] = "xkcd";
    lineCount = 1;
  }
  return lineCount;
}

// Draw a small battery gauge (outlined rectangle + terminal knob +
// optional fill) at the given top-left coordinates. `batteryPct` in
// 0..100; a negative value draws just the empty outline. `outline` is
// the border thickness, `terminalW`/`terminalH` describe the small knob
// that sticks out on the right, `black` is the panel-black color the
// caller wants to use for the outline/fill. When `charging` is true a
// small lightning-bolt glyph is drawn centred inside the gauge with a
// white interior and a black border, so it reads clearly whether the
// area behind it is empty (panel background) or filled black by a high
// battery level.
template <typename EPaper>
inline void drawBatteryGauge(EPaper& epaper, int x, int y, int w, int h,
                             int batteryPct, int outline, int terminalW,
                             int terminalH, uint32_t black, uint32_t white,
                             bool charging = false) {
  const int gaugeCenterY = y + h / 2;
  for (int inset = 0; inset < outline; ++inset) {
    epaper.drawRect(x + inset, y + inset, w - 2 * inset, h - 2 * inset,
                    black);
  }
  epaper.fillRect(x + w, gaugeCenterY - terminalH / 2, terminalW,
                  terminalH, black);
  if (batteryPct >= 0) {
    const int innerX = x + outline + 1;
    const int innerY = y + outline + 1;
    const int innerWidth = max(0, w - 2 * (outline + 1));
    const int innerHeight = max(0, h - 2 * (outline + 1));
    const int fillWidth = (innerWidth * batteryPct + 50) / 100;
    if (fillWidth > 0) {
      epaper.fillRect(innerX, innerY, fillWidth, innerHeight, black);
    }
  }
  if (charging) {
    // Fit the bolt inside the gauge interior, leaving a 1-px margin so
    // its black outline never touches the gauge outline. Two right
    // triangles form a chunky "Z" that reads as a bolt at any panel
    // size; we paint an outer black bolt one pixel larger than the
    // inner white bolt to give it a legible border against both the
    // panel background and any black fill behind it.
    const int interiorH = max(0, h - 2 * outline - 2);
    const int boltH = max(4, interiorH);
    const int boltW = max(3, boltH * 3 / 8);
    const int bx = x + (w - boltW) / 2;
    const int by = y + (h - boltH) / 2;
    // Outer black bolt, expanded by 1 px on all sides for the border.
    const int obx = bx - 1;
    const int oby = by - 1;
    const int obW = boltW + 2;
    const int obH = boltH + 2;
    epaper.fillTriangle(obx + obW, oby,
                        obx, oby + obH / 2,
                        obx + obW, oby + obH / 2, black);
    epaper.fillTriangle(obx, oby + obH / 2,
                        obx + obW, oby + obH / 2,
                        obx, oby + obH, black);
    // Inner white bolt at the original bolt dimensions.
    epaper.fillTriangle(bx + boltW, by,
                        bx, by + boltH / 2,
                        bx + boltW, by + boltH / 2, white);
    epaper.fillTriangle(bx, by + boltH / 2,
                        bx + boltW, by + boltH / 2,
                        bx, by + boltH, white);
  }
}

// Fill a horizontal band of the panel with `background` and, when
// `dithered` is true, sprinkle `ditherColor` pixels through it using
// a 4x4 Bayer pattern so panels whose native palette lacks the exact
// gray needed for the status strip still get a smooth-looking fill.
// `top` and `height` are clamped to `[0, panelHeight)`.
template <typename EPaper>
inline void fillStatusBackground(EPaper& epaper, int top, int height,
                                 int panelWidth, int panelHeight,
                                 uint32_t background, bool dithered,
                                 uint32_t ditherColor,
                                 uint8_t ditherThreshold) {
  epaper.fillRect(0, top, panelWidth, height, background);
  if (!dithered) return;

  // Ordered neutral patterns provide intermediate shades when the panel
  // palette has no suitable native gray.
  static constexpr uint8_t bayer4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  const int bottom = min(panelHeight, top + height);
  for (int y = max(0, top); y < bottom; ++y) {
    for (int x = 0; x < panelWidth; ++x) {
      if (bayer4[y & 3][x & 3] < ditherThreshold) {
        epaper.drawPixel(x, y, ditherColor);
      }
    }
  }
}

}  // namespace text_render
