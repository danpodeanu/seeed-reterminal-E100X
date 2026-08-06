#include "wake_report.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>

#include "app_logger.h"
#include "board_pins.h"
#include "local_time.h"

namespace wake_report {
String wakeReason(esp_sleep_wakeup_cause_t cause, uint64_t wakePins) {
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) return "cold boot/reset";
  if (cause == ESP_SLEEP_WAKEUP_TIMER) return "scheduled timer";
  if (cause != ESP_SLEEP_WAKEUP_EXT1) {
    return "wake cause " + String(static_cast<int>(cause));
  }

  String buttons;
  if (wakePins & (1ULL << board::PIN_BUTTON_0)) {
    buttons += "GPIO" + String(board::PIN_BUTTON_0);
  }
  if (wakePins & (1ULL << board::PIN_BUTTON_1)) {
    if (!buttons.isEmpty()) buttons += "+";
    buttons += "GPIO" + String(board::PIN_BUTTON_1);
  }
  if (wakePins & (1ULL << board::PIN_BUTTON_2)) {
    if (!buttons.isEmpty()) buttons += "+";
    buttons += "GPIO" + String(board::PIN_BUTTON_2);
  }
  return buttons.isEmpty() ? "front button" : "front button " + buttons;
}

bool logWakeEvent(esp_sleep_wakeup_cause_t cause, uint64_t wakePins,
                  bool logUnsynchronized) {
  const String reason = wakeReason(cause, wakePins);
  struct tm localTime = {};
  if (!local_time::localClock(localTime)) {
    if (logUnsynchronized) {
      LOG.printf("[wake] time=unavailable reason=%s\n", reason.c_str());
    }
    return false;
  }
  char formatted[40] = {};
  strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z",
           &localTime);
  LOG.printf("[wake] time=%s reason=%s\n", formatted, reason.c_str());
  return true;
}

}  // namespace wake_report
