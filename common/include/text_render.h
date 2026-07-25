#pragma once

#include <Arduino.h>
#include <stdint.h>

// Text rendering helpers shared by every viewer app. Kept template-only
// so common/ doesn't need to depend on Seeed_GFX / TFT_eSPI headers -
// the caller's `EPaper` type just has to expose the textWidth() and
// draw{Rect,String,fillRect} methods each app already uses.
namespace text_render {

// Truncate `text` so it fits inside `maxWidth` pixels when rendered
// with the given TFT_eSPI font id, appending "..." when a truncation
// happens. When the text already fits it is returned unchanged.
template <typename EPaper>
inline String ellipsize(EPaper& epaper, String text, int maxWidth,
                        uint8_t font = 1) {
  if (epaper.textWidth(text, font) <= maxWidth) return text;
  const String suffix = "...";
  while (text.length() > 1 &&
         epaper.textWidth(text + suffix, font) > maxWidth) {
    text.remove(text.length() - 1);
  }
  text.trim();
  return text + suffix;
}

// Normalize an arbitrary Unicode string down to printable ASCII so the
// built-in TFT_eSPI fonts (which don't support Latin-1 or UTF-8) render
// something sensible. HTML entities that appear in xkcd feeds are
// unescaped; any run of whitespace collapses to a single space; every
// non-ASCII byte becomes a single '?'.
inline String displayText(String value) {
  value.replace("&quot;", "\"");
  value.replace("&apos;", "'");
  value.replace("&#39;", "'");
  value.replace("&lt;", "<");
  value.replace("&gt;", ">");
  value.replace("&amp;", "&");

  String ascii;
  ascii.reserve(value.length());
  bool lastWasSpace = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c >= 32 && c <= 126) {
      const bool isSpace = c == ' ' || c == '\t' || c == '\r' || c == '\n';
      if (isSpace) {
        if (!lastWasSpace) ascii += ' ';
      } else {
        ascii += static_cast<char>(c);
      }
      lastWasSpace = isSpace;
    } else if (!lastWasSpace) {
      ascii += '?';
      lastWasSpace = false;
    }
  }
  ascii.trim();
  return ascii;
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
// caller wants to use for the outline/fill.
template <typename EPaper>
inline void drawBatteryGauge(EPaper& epaper, int x, int y, int w, int h,
                             int batteryPct, int outline, int terminalW,
                             int terminalH, uint32_t black) {
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
