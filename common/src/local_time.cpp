#include "local_time.h"

#include <stdlib.h>
#include <time.h>

#include "app_logic_core.h"

namespace local_time {

void configureTimezone(const char* tz) {
  setenv("TZ", tz, 1);
  tzset();
}

bool clockIsValid() { return time(nullptr) >= 1700000000; }

bool localClock(struct tm& out) {
  const time_t now = time(nullptr);
  return clockIsValid() && localtime_r(&now, &out) != nullptr;
}

bool refreshDue(bool coldBoot, time_t lastSync, uint32_t intervalSeconds) {
  const time_t now = time(nullptr);
  return app_logic::refreshDue(coldBoot, clockIsValid(), now, lastSync,
                               intervalSeconds);
}

}  // namespace local_time
