#pragma once

#include <Arduino.h>

#include "board_pins.h"

// GPIO controlling the shared SD/e-paper peripheral rail. Keeping this
// separate from either driver prevents panel power from becoming an
// accidental side effect of mounting the SD card.
namespace peripheral_power {

inline void setPinEnabled(int pin, bool enabled) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, enabled ? HIGH : LOW);
}

inline void setEnabled(bool enabled) {
  setPinEnabled(board::PIN_PERIPHERAL_ENABLE, enabled);
}

inline void enable() { setEnabled(true); }
inline void disable() { setEnabled(false); }

inline void setSdEnabled(bool enabled) {
  setPinEnabled(board::PIN_SD_ENABLE, enabled);
}

inline void enableSd() { setSdEnabled(true); }
inline void disableSd() { setSdEnabled(false); }

}  // namespace peripheral_power
