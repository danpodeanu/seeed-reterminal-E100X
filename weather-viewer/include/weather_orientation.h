#pragma once

#include <stdint.h>

namespace weather_orientation {

enum class Orientation : uint8_t {
  Portrait = 0,
  RotateCW = 1,
  RotateCCW = 2,
};

constexpr bool isLandscape(Orientation orientation) {
  return orientation != Orientation::Portrait;
}

constexpr int panelRotation(Orientation orientation) {
  switch (orientation) {
    case Orientation::RotateCW: return 0;
    case Orientation::RotateCCW: return 2;
    case Orientation::Portrait: return 1;
  }
  return 1;
}

constexpr int panelWidth(Orientation orientation) {
  return isLandscape(orientation) ? 800 : 480;
}

constexpr int panelHeight(Orientation orientation) {
  return isLandscape(orientation) ? 480 : 800;
}

}  // namespace weather_orientation
