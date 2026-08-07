#pragma once

#include <stdint.h>

#include <utility>

#include "app_logic_core.h"

namespace app_logic {

constexpr int photoDirection(bool previousButtonWake) {
  return previousButtonWake ? -1 : 1;
}

constexpr int failedPhotoAdvance(int requestedDirection) {
  return requestedDirection == 0 ? 1 : requestedDirection;
}

constexpr int32_t normalizePhotoIndex(int32_t index, uint32_t count) {
  if (count == 0) return -1;
  const int32_t signedCount = static_cast<int32_t>(count);
  const int32_t remainder = index % signedCount;
  return remainder < 0 ? remainder + signedCount : remainder;
}

// Fisher-Yates in-place shuffle. `randomBelow` is invoked as
// `randomBelow(i)` for descending values of i starting at list.size(); it
// must return a size_t in [0, i) — matching how esp_random() is used on
// the device via `esp_random() % i`.
template <typename Container, typename RngFn>
void shuffleInPlace(Container& list, RngFn randomBelow) {
  using std::swap;
  for (size_t i = list.size(); i > 1; --i) {
    const size_t j = randomBelow(i);
    if (j != i - 1) swap(list[i - 1], list[j]);
  }
}

}  // namespace app_logic
