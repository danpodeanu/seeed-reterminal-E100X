#pragma once

#include <algorithm>
#include <stdint.h>

namespace calendar_render_geometry {

struct Rect {
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
};

struct HeaderGroup {
  int left = 0;
  int width = 0;
  int iconLeft = 0;
  int textLeft = 0;
};

inline HeaderGroup centeredHeaderGroup(int surfaceWidth, int iconWidth,
                                       int gap, int textWidth) {
  HeaderGroup result;
  result.width =
      std::max(0, iconWidth) + std::max(0, gap) + std::max(0, textWidth);
  result.left = (surfaceWidth - result.width) / 2;
  result.iconLeft = result.left;
  result.textLeft = result.left + std::max(0, iconWidth) + std::max(0, gap);
  return result;
}

inline Rect gridCellInterior(int left, int top, int width, int height,
                             int horizontalInset) {
  const int inset = std::max(0, horizontalInset);
  return {
      left + inset,
      top + 1,
      std::max(0, width - inset * 2),
      std::max(0, height - 1),
  };
}

inline int gridDayLabelTop(int cellTop, int baseOffset, int weekOffset,
                           bool monthView) {
  return cellTop + baseOffset + (monthView ? 0 : weekOffset);
}

inline Rect agendaBand(int cardLeft, int rowTop, int cardWidth, int rowHeight,
                       int preferredRowHeight, int scaledTwoPixels,
                       int scaledThreePixels, bool upcoming) {
  const int horizontalInset =
      upcoming ? std::max(1, scaledThreePixels - 1)
               : std::max(1, scaledTwoPixels);
  const int rowCompression = std::max(0, preferredRowHeight - rowHeight);
  const int baseVerticalGap =
      upcoming ? scaledTwoPixels : std::max(1, scaledTwoPixels - 1);
  const int verticalGap =
      std::max(1, baseVerticalGap - rowCompression / 2);
  return {
      cardLeft + horizontalInset,
      rowTop + verticalGap,
      std::max(0, cardWidth - horizontalInset * 2),
      std::max(0, rowHeight - verticalGap * 2),
  };
}

inline Rect footerBadge(int panelHeight, int badgeWidth, int badgeHeight) {
  return {
      0,
      panelHeight - badgeHeight,
      std::max(0, badgeWidth),
      std::max(0, badgeHeight),
  };
}

template <typename Surface>
inline void fillPlainFooterBackground(Surface& surface, const Rect& rect,
                                      uint32_t panelWhite) {
  surface.fillRect(rect.left, rect.top, rect.width, rect.height, panelWhite);
}

}  // namespace calendar_render_geometry
