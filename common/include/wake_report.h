#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <stdint.h>

// Wake-cause reporting shared by every viewer app.
namespace wake_report {

// Human-readable label describing why the ESP32 woke up. Used in [wake]
// log lines and boot summaries.
String wakeReason(esp_sleep_wakeup_cause_t cause, uint64_t wakePins);

// Log a single "[wake] time=... reason=..." line to LOG. When the wall
// clock isn't valid yet the line is either skipped (returning false) or,
// with logUnsynchronized=true, prints "time=unavailable" so callers can
// still trace the wake without a timestamp.
bool logWakeEvent(esp_sleep_wakeup_cause_t cause, uint64_t wakePins,
                  bool logUnsynchronized);

}  // namespace wake_report
