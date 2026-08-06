// Weather background - a Chinese ink-wash landscape painted behind the
// weather render so the header, forecast strip, and body text sit over
// a soft mountain scene instead of a plain white panel.
//
// Four themes (sunny, cloudy, rainy, snowy) are keyed off the current
// WMO weather code so the picture roughly agrees with the hero icon.
// Every model uses a pre-baked payload selected by WMO condition. E1005's
// portrait and landscape payloads are faded and dithered to monochrome.
//
// One data file per image-backed model lives next to this blitter; each is guarded
// with `#if RETERMINAL_MODEL == NNNN` so the linker only picks up the
// payload matching the active build. Regenerate everything via
// `python tools/embed_weather_background.py --all` after updating the
// source PNGs or the target model list.
#pragma once

#include <TFT_eSPI.h>
#include <stdint.h>
#include <stddef.h>

namespace weather_background {

// Ordering must match tools/embed_weather_background.py::ALL_THEMES so
// the emitted kThemeData[] pointer table is indexed correctly.
enum class Theme : uint8_t {
  SUNNY  = 0,
  CLOUDY = 1,
  RAINY  = 2,
  SNOWY  = 3,
};
constexpr uint8_t kThemeEnumCount = 4;

extern const uint16_t kWidth;
extern const uint16_t kHeight;
extern const uint8_t  kBitsPerPixel;   // 1 or 2, MSB-first packing
extern const uint8_t  kLevels;         // number of gray levels stored
extern const uint8_t  kScale;          // blitter upscale factor (1 = 1:1)
extern const uint8_t  kThemeCount;     // number of distinct payloads bundled
extern const size_t   kThemeDataLen;   // bytes per payload
extern const uint8_t* const kThemeData[kThemeEnumCount];
#if RETERMINAL_MODEL == 1005
extern const uint16_t kLandscapeWidth;
extern const uint16_t kLandscapeHeight;
extern const size_t kLandscapeThemeDataLen;
extern const uint8_t* const kLandscapeThemeData[kThemeEnumCount];
#endif

// Collapse a WMO weather code to the broad theme bucket the background
// should reflect. Mirrors weather_quotes::wmoToBucketIndex so the
// background and the footer proverb agree on the current condition.
// Unknown / off-list codes fall back to CLOUDY (a neutral misty scene).
Theme themeForWmoCode(int wmoCode);

// Blit the weather background for `theme` across the whole sprite.
// Per-model palette mapping lives inside the .cpp so callers don't need
// to know whether the current panel is 4-gray, 16-gray, or Spectra-6 BW.
void draw(TFT_eSPI& epaper, Theme theme);

}  // namespace weather_background
