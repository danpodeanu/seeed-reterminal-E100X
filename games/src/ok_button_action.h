#pragma once

#include <stdint.h>

namespace ok_button {

constexpr uint32_t kLanguageHoldMinMs = 2000;
constexpr uint32_t kLanguageHoldMaxMs = 5000;

enum class Action : uint8_t {
  DeepSleep,
  LanguageSelection,
  None,
};

constexpr Action actionForHold(uint32_t heldMs) {
  if (heldMs < kLanguageHoldMinMs) return Action::DeepSleep;
  if (heldMs <= kLanguageHoldMaxMs) return Action::LanguageSelection;
  return Action::None;
}

}  // namespace ok_button
