#pragma once

#include <stddef.h>
#include <stdint.h>

namespace game_ranking {

constexpr uint32_t nextPlayCount(uint32_t count) {
  return count == UINT32_MAX ? UINT32_MAX : count + 1U;
}

template <size_t Count>
void rankByPlayCount(const uint32_t (&counts)[Count],
                     uint8_t (&ranking)[Count]) {
  for (size_t index = 0; index < Count; ++index) {
    ranking[index] = static_cast<uint8_t>(index);
  }
  for (size_t index = 1; index < Count; ++index) {
    const uint8_t candidate = ranking[index];
    size_t position = index;
    while (position > 0 &&
           counts[candidate] > counts[ranking[position - 1]]) {
      ranking[position] = ranking[position - 1];
      --position;
    }
    ranking[position] = candidate;
  }
}

}  // namespace game_ranking
