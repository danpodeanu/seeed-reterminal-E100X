#pragma once

#include <stdint.h>

class Adafruit_SHT4x;

// SHT4x climate-sensor readout shared by every viewer app. The retry
// loop that each app was repeating (bring I2C up, sht4.begin, getEvent,
// on failure reset the bus and try again) lives here.
namespace climate {

// Take a single temperature/humidity reading, retrying up to `attempts`
// times with `retryDelayMs` between attempts. On success returns true
// and writes to both output refs; on failure returns false and the refs
// are unchanged. Uses hardware::ensureI2cBus / hardware::resetI2cBus
// internally so the caller doesn't need to touch the I2C driver.
bool readSht4x(Adafruit_SHT4x& sht4,
               float& tempC, float& humidityPct,
               uint8_t attempts, uint32_t retryDelayMs);

}  // namespace climate
