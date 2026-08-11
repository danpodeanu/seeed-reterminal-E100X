#pragma once

#include <stdint.h>

namespace ok_button {

constexpr uint32_t kDeepSleepHoldMs = 2000;

enum class Action : uint8_t {
  ShortPress,
  DeepSleep,
};

constexpr Action actionForHold(uint32_t heldMs) {
  return heldMs < kDeepSleepHoldMs ? Action::ShortPress : Action::DeepSleep;
}

}  // namespace ok_button
