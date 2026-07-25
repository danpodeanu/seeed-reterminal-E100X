#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <time.h>

// Quiet-hours helpers used by every viewer app. Each app previously
// duplicated the same eight tiny functions that combine its own
// `config::QUIET_*` constants with `app_logic::` predicates and a
// local `struct tm`. This module centralises them behind a small
// runtime-configured struct so common/ doesn't need to know about
// any per-app config namespace.
//
// Typical use from main.cpp / setup():
//   quiet_hours::configure({
//       config::QUIET_HOURS_ENABLED,
//       config::QUIET_START_HOUR, config::QUIET_START_MINUTE,
//       config::QUIET_END_HOUR,   config::QUIET_END_MINUTE,
//   });
// after which every helper below reads that configuration. The
// configuration lives in normal RAM, so it must be re-applied on
// each wake (setup() already runs after deep-sleep reset).
namespace quiet_hours {

// Configuration copied in via configure(). Hours are 0..23, minutes
// 0..59; the pair defines a possibly-wrapping window during which
// helpers report the device is inside quiet hours.
struct Config {
  bool enabled;
  int startHour;
  int startMinute;
  int endHour;
  int endMinute;
};

// Install the runtime configuration. Safe to call more than once.
void configure(const Config& cfg);

// Seconds elapsed since local midnight for `localTime`.
int secondsOfDay(const struct tm& localTime);

// Seconds from midnight for the configured quiet-start moment.
int startSecond();

// Seconds from midnight for the configured quiet-end moment.
int endSecond();

// True when quiet hours are enabled and `localTime` falls inside the
// configured window.
bool active(const struct tm& localTime);

// Seconds from `localTime` until the next occurrence of
// `targetSecond` (also expressed as seconds past local midnight).
uint64_t secondsUntilTimeOfDay(int targetSecond,
                               const struct tm& localTime);

// Seconds from `localTime` until the configured quiet-hours end.
uint64_t secondsUntilEnd(const struct tm& localTime);

// True when sleeping `normalSleepSeconds` from `localTime` would
// wake the device inside the quiet window (so the caller should
// stretch the sleep to the quiet-end boundary instead).
bool nextWakeFallsInside(const struct tm& localTime,
                         uint64_t normalSleepSeconds);

// "HH:MM" label for the configured quiet-end moment, useful for UI
// strings like "sleeping until 07:00".
String endLabel();

}  // namespace quiet_hours
