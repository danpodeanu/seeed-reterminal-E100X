#pragma once

#include <TFT_eSPI.h>

#include <string>

namespace calendar_latin_font {

enum class Size : uint8_t {
  Grid,
  Agenda,
};

int textWidth(const String& text, Size size);
String ellipsize(const std::string& text, int maximumWidth, Size size);
void drawLeftMiddle(EPaper& epaper, const String& text, int left, int middleY,
                    Size size, uint32_t color);
const GFXfont* uiFont(int pixelSize);

}  // namespace calendar_latin_font
