#pragma once

#include <Arduino.h>
#include <driver/gpio.h>

#include "board_pins.h"

namespace power_latch {

inline void holdOn() {
  if (board::PIN_POWER_HOLD < 0 || board::PIN_POWER_LOCK < 0) return;
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(static_cast<gpio_num_t>(board::PIN_POWER_HOLD));
  gpio_hold_dis(static_cast<gpio_num_t>(board::PIN_POWER_LOCK));
  pinMode(board::PIN_POWER_HOLD, OUTPUT);
  digitalWrite(board::PIN_POWER_HOLD, HIGH);
  pinMode(board::PIN_POWER_LOCK, OUTPUT);
  digitalWrite(board::PIN_POWER_LOCK, HIGH);
}

inline void holdDuringDeepSleep() {
  if (board::PIN_POWER_HOLD < 0 || board::PIN_POWER_LOCK < 0) return;
  gpio_hold_en(static_cast<gpio_num_t>(board::PIN_POWER_HOLD));
  gpio_hold_en(static_cast<gpio_num_t>(board::PIN_POWER_LOCK));
  gpio_deep_sleep_hold_en();
}

}  // namespace power_latch
