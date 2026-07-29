#pragma once

// Random-per-refresh weather-themed proverb for the footer.
//
// The full proverb list lives in weather-viewer/data/quotes.txt,
// bucketed by weather condition (CLEAR, RAIN, SNOW, ...). A build-time
// generator (tools/generate_weather_quotes.py) emits the packed table
// into generated/weather_quotes_data.h. This header exposes the tiny
// runtime picker on top.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class TFT_eSPI;

namespace weather_quotes {

// Pick a proverb for `wmoCode` whose rendered width at the currently
// selected font fits within `availPx` pixels on `epaper`. Randomised
// per call via `seed` -- the caller passes something fresh (e.g.
// esp_random() or millis()) so consecutive renders don't repeat.
//
// Selection strategy:
//   1. Look up the primary bucket for `wmoCode` (see wmoToBucket).
//   2. Walk that bucket in a seeded pseudo-random order, returning the
//      first quote whose textWidth() <= availPx.
//   3. If none fit, walk the UNIVERSAL fallback bucket the same way.
//   4. If still none, return nullptr (caller draws nothing).
//
// The picker doesn't change any TFT_eSPI state -- it uses textWidth()
// which is read-only against the currently selected font. Load the
// footer font before calling.
const char* pickForWmo(int wmoCode, uint32_t seed, TFT_eSPI& epaper,
                       int availPx);

}  // namespace weather_quotes
