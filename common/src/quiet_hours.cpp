#include "quiet_hours.h"

#include <stdio.h>

#include "app_logic_core.h"

namespace quiet_hours {
namespace {

Config g_cfg = {false, 0, 0, 0, 0};

}  // namespace

void configure(const Config& cfg) { g_cfg = cfg; }

int secondsOfDay(const struct tm& localTime) {
  return app_logic::secondsOfDay(localTime.tm_hour, localTime.tm_min,
                                 localTime.tm_sec);
}

int startSecond() {
  return g_cfg.startHour * 3600 + g_cfg.startMinute * 60;
}

int endSecond() {
  return g_cfg.endHour * 3600 + g_cfg.endMinute * 60;
}

bool active(const struct tm& localTime) {
  return app_logic::quietHoursActive(g_cfg.enabled,
                                     secondsOfDay(localTime),
                                     startSecond(), endSecond());
}

uint64_t secondsUntilTimeOfDay(int targetSecond,
                               const struct tm& localTime) {
  return app_logic::secondsUntilTimeOfDay(targetSecond,
                                          secondsOfDay(localTime));
}

uint64_t secondsUntilEnd(const struct tm& localTime) {
  return secondsUntilTimeOfDay(endSecond(), localTime);
}

bool nextWakeFallsInside(const struct tm& localTime,
                         uint64_t normalSleepSeconds) {
  return app_logic::nextWakeFallsInQuietHours(
      g_cfg.enabled, secondsOfDay(localTime), startSecond(),
      endSecond(), normalSleepSeconds);
}

String endLabel() {
  char label[6] = {};
  snprintf(label, sizeof(label), "%02u:%02u",
           static_cast<unsigned>(g_cfg.endHour),
           static_cast<unsigned>(g_cfg.endMinute));
  return String(label);
}

}  // namespace quiet_hours
