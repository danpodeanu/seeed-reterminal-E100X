#pragma once

#ifndef RETERMINAL_MODEL
#error "RETERMINAL_MODEL must be defined before including panel_traits.h"
#endif

namespace panel_traits {

enum class SizeClass {
  CompactLandscape,
  CompactPortrait,
  LargeLandscape,
  LargePortrait,
};

inline constexpr int MODEL = RETERMINAL_MODEL;

#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
inline constexpr int WIDTH = 800;
inline constexpr int HEIGHT = 480;
inline constexpr int UI_SCALE_NUMERATOR = 1;
inline constexpr int UI_SCALE_DENOMINATOR = 1;
inline constexpr SizeClass SIZE_CLASS = SizeClass::CompactLandscape;
#elif RETERMINAL_MODEL == 1003
inline constexpr int WIDTH = 1872;
inline constexpr int HEIGHT = 1404;
inline constexpr int UI_SCALE_NUMERATOR = 9;
inline constexpr int UI_SCALE_DENOMINATOR = 4;
inline constexpr SizeClass SIZE_CLASS = SizeClass::LargeLandscape;
#elif RETERMINAL_MODEL == 1004
inline constexpr int WIDTH = 1200;
inline constexpr int HEIGHT = 1600;
inline constexpr int UI_SCALE_NUMERATOR = 3;
inline constexpr int UI_SCALE_DENOMINATOR = 2;
inline constexpr SizeClass SIZE_CLASS = SizeClass::LargePortrait;
#elif RETERMINAL_MODEL == 1005
inline constexpr int WIDTH = 480;
inline constexpr int HEIGHT = 800;
inline constexpr int UI_SCALE_NUMERATOR = 1;
inline constexpr int UI_SCALE_DENOMINATOR = 1;
inline constexpr SizeClass SIZE_CLASS = SizeClass::CompactPortrait;
#else
#error "Unsupported RETERMINAL_MODEL"
#endif

inline constexpr bool IS_COMPACT =
    SIZE_CLASS == SizeClass::CompactLandscape ||
    SIZE_CLASS == SizeClass::CompactPortrait;
inline constexpr bool IS_PORTRAIT =
    SIZE_CLASS == SizeClass::CompactPortrait ||
    SIZE_CLASS == SizeClass::LargePortrait;

constexpr int scaleUi(int e1001Pixels) {
  return (e1001Pixels * UI_SCALE_NUMERATOR + UI_SCALE_DENOMINATOR / 2) /
         UI_SCALE_DENOMINATOR;
}

}  // namespace panel_traits
