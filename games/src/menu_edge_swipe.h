#pragma once

namespace menu_edge_swipe {

enum class Direction {
  None,
  Previous,
  Next,
};

constexpr bool startsAtEdge(int x, int screenWidth, int edgeWidth) {
  return x < edgeWidth || x >= screenWidth - edgeWidth;
}

constexpr int absolute(int value) { return value < 0 ? -value : value; }

constexpr Direction detect(int startX, int startY, int endX, int endY,
                           int screenWidth, int edgeWidth, int threshold) {
  const int dx = endX - startX;
  const int dy = endY - startY;
  if (absolute(dx) < threshold || absolute(dx) <= absolute(dy)) {
    return Direction::None;
  }
  if (startX < edgeWidth && dx > 0) return Direction::Previous;
  if (startX >= screenWidth - edgeWidth && dx < 0) return Direction::Next;
  return Direction::None;
}

}  // namespace menu_edge_swipe
