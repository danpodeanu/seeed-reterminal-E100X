#pragma once

#include <Arduino.h>

#include "board_pins.h"

// GPIO controlling the shared SD/e-paper peripheral rail. Keeping this
// separate from either driver prevents panel power from becoming an
// accidental side effect of mounting the SD card.
namespace peripheral_power {

inline void setEnabled(bool enabled) {
  pinMode(board::PIN_PERIPHERAL_ENABLE, OUTPUT);
  digitalWrite(board::PIN_PERIPHERAL_ENABLE, enabled ? HIGH : LOW);
}

inline void enable() { setEnabled(true); }
inline void disable() { setEnabled(false); }

}  // namespace peripheral_power
