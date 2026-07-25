#include "rtc_sync.h"

#include <Arduino.h>
#include <Wire.h>

#include "app_logger.h"
#include "hardware.h"
#include "pcf8563_utc.h"

namespace rtc_sync {

bool readAndLog(pcf8563::Reading& stored) {
  if (!hardware::ensureI2cBus()) return false;
  String error;
  if (!pcf8563::readUtc(Wire, stored, error)) {
    LOG.printf("[rtc] PCF8563 read failed: %s\n", error.c_str());
    return false;
  }
  LOG.printf("[rtc] PCF8563 stored UTC=%s, %s\n",
             pcf8563::format(stored).c_str(),
             stored.voltageLow ? "VL set - stored time is unreliable"
                               : "VL clear - stored time is valid");
  return true;
}

void saveTime(time_t now) {
  if (!hardware::ensureI2cBus()) return;
  String error;
  if (!pcf8563::writeUtc(Wire, now, error)) {
    LOG.printf("[rtc] PCF8563 NTP update failed: %s\n", error.c_str());
    return;
  }
  LOG.println("[rtc] PCF8563 updated from NTP");
  pcf8563::Reading verified;
  readAndLog(verified);
}

bool restoreSystemClock() {
  pcf8563::Reading stored;
  if (!readAndLog(stored)) return false;
  String error;
  if (!pcf8563::setSystemClock(stored, error)) {
    LOG.printf("[rtc] PCF8563 fallback rejected: %s\n", error.c_str());
    return false;
  }
  LOG.printf("[rtc] restored ESP32 clock from PCF8563 UTC=%s\n",
             pcf8563::format(stored).c_str());
  return true;
}

}  // namespace rtc_sync
