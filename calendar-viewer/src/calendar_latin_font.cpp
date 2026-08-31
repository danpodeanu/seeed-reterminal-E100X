#include <TFT_eSPI.h>

#include <pgmspace.h>

#include "calendar_latin_font.h"
#include "calendar_latin_font_data.h"
#include "calendar_latin_text.h"
#include "text_render.h"

namespace calendar_latin_font {
namespace {

struct FontData {
  const uint8_t* bitmaps;
  const uint16_t* bitmapOffsets;
  const uint8_t* widths;
  const uint8_t* heights;
  const uint8_t* advances;
  const int8_t* xOffsets;
  const int8_t* yOffsets;
  uint8_t ascent;
  uint8_t descent;
};

struct Glyph {
  uint16_t bitmapOffset = 0;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t advance = 0;
  int8_t xOffset = 0;
  int8_t yOffset = 0;
};

const FontData& fontData(Size size) {
  static const FontData grid{
      data::kGridBitmaps, data::kGridBitmapOffsets, data::kGridWidths,
      data::kGridHeights, data::kGridAdvances, data::kGridXOffsets,
      data::kGridYOffsets, data::kGridAscent, data::kGridDescent};
  static const FontData agenda{
      data::kAgendaBitmaps, data::kAgendaBitmapOffsets, data::kAgendaWidths,
      data::kAgendaHeights, data::kAgendaAdvances, data::kAgendaXOffsets,
      data::kAgendaYOffsets, data::kAgendaAscent, data::kAgendaDescent};
  return size == Size::Agenda ? agenda : grid;
}

int glyphIndex(uint32_t codepoint) {
  size_t low = 0;
  size_t high = data::kCodepointCount;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    const uint16_t candidate = pgm_read_word(&data::kCodepoints[middle]);
    if (candidate == codepoint) return static_cast<int>(middle);
    if (candidate < codepoint) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return -1;
}

Glyph readGlyph(const FontData& font, int index) {
  Glyph glyph;
  glyph.bitmapOffset = pgm_read_word(&font.bitmapOffsets[index]);
  glyph.width = pgm_read_byte(&font.widths[index]);
  glyph.height = pgm_read_byte(&font.heights[index]);
  glyph.advance = pgm_read_byte(&font.advances[index]);
  glyph.xOffset =
      static_cast<int8_t>(pgm_read_byte(&font.xOffsets[index]));
  glyph.yOffset =
      static_cast<int8_t>(pgm_read_byte(&font.yOffsets[index]));
  return glyph;
}

int resolvedGlyphIndex(uint32_t codepoint) {
  if (calendar_latin_text::isIgnorableCodepoint(codepoint)) return -1;
  const int index = glyphIndex(codepoint);
  return index >= 0 ? index : data::kReplacementGlyphIndex;
}

void drawGlyph(EPaper& epaper, const FontData& font, const Glyph& glyph,
               int cursorX, int baselineY, uint32_t color) {
  if (glyph.width == 0 || glyph.height == 0) return;

  const uint8_t* bitmap = font.bitmaps + glyph.bitmapOffset;
  uint32_t bitIndex = 0;
  for (int row = 0; row < glyph.height; ++row) {
    int runStart = -1;
    for (int column = 0; column < glyph.width; ++column, ++bitIndex) {
      const uint8_t bits = pgm_read_byte(&bitmap[bitIndex / 8]);
      const bool ink = (bits & (0x80 >> (bitIndex % 8))) != 0;
      if (ink && runStart < 0) runStart = column;
      if (!ink && runStart >= 0) {
        epaper.drawFastHLine(cursorX + glyph.xOffset + runStart,
                            baselineY + glyph.yOffset + row,
                            column - runStart, color);
        runStart = -1;
      }
    }
    if (runStart >= 0) {
      epaper.drawFastHLine(cursorX + glyph.xOffset + runStart,
                          baselineY + glyph.yOffset + row,
                          glyph.width - runStart, color);
    }
  }
}

void removeLastCodepoint(String& text) {
  if (text.isEmpty()) return;
  int start = static_cast<int>(text.length()) - 1;
  while (start > 0 && calendar_latin_text::isContinuationByte(
                          static_cast<uint8_t>(text[start]))) {
    --start;
  }
  text.remove(start);
}

}  // namespace

int textWidth(const String& text, Size size) {
  const FontData& font = fontData(size);
  const char* bytes = text.c_str();
  const size_t length = text.length();
  size_t offset = 0;
  int cursor = 0;
  int rightEdge = 0;
  while (offset < length) {
    const uint32_t codepoint =
        calendar_latin_text::nextCodepoint(bytes, length, offset);
    const int index = resolvedGlyphIndex(codepoint);
    if (index < 0) continue;
    const Glyph glyph = readGlyph(font, index);
    const int glyphRight = cursor + glyph.xOffset + glyph.width;
    if (glyphRight > rightEdge) rightEdge = glyphRight;
    cursor += glyph.advance;
  }
  return cursor > rightEdge ? cursor : rightEdge;
}

String ellipsize(const std::string& text, int maximumWidth, Size size) {
  String fitted = text_render::displayText(String(text.c_str()));
  if (textWidth(fitted, size) <= maximumWidth) return fitted;

  const String suffix = "...";
  while (!fitted.isEmpty() &&
         textWidth(fitted + suffix, size) > maximumWidth) {
    removeLastCodepoint(fitted);
  }
  fitted.trim();
  return fitted + suffix;
}

void drawLeftMiddle(EPaper& epaper, const String& text, int left, int middleY,
                    Size size, uint32_t color) {
  const FontData& font = fontData(size);
  const int baselineY = middleY + (font.ascent - font.descent) / 2;
  const char* bytes = text.c_str();
  const size_t length = text.length();
  size_t offset = 0;
  int cursorX = left;
  while (offset < length) {
    const uint32_t codepoint =
        calendar_latin_text::nextCodepoint(bytes, length, offset);
    const int index = resolvedGlyphIndex(codepoint);
    if (index < 0) continue;
    const Glyph glyph = readGlyph(font, index);
    drawGlyph(epaper, font, glyph, cursorX, baselineY, color);
    cursorX += glyph.advance;
  }
}

const GFXfont* uiFont(int pixelSize) {
  switch (pixelSize) {
    case 10:
      return &data::kUiFont10;
    case 16:
      return &data::kUiFont16;
    case 24:
      return &data::kUiFont24;
    case 36:
      return &data::kUiFont36;
    case 48:
      return &data::kUiFont48;
    default:
      return &data::kUiFont18;
  }
}

}  // namespace calendar_latin_font
