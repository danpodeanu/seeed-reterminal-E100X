#include "climate_sensor.h"

#include <Adafruit_SHT4x.h>
#include <Arduino.h>
#include <Wire.h>

#include "app_logger.h"
#include "hardware.h"

namespace climate {

bool readSht4x(Adafruit_SHT4x& sht4,
               float& tempC, float& humidityPct,
               uint8_t attempts, uint32_t retryDelayMs) {
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
    if (attempt > 0) {
      // A sensor left powered across deep sleep can occasionally miss
      // the first transaction. Reset the ESP32 I2C peripheral before
      // retrying.
      Wire.end();
      hardware::resetI2cBus();
      delay(retryDelayMs);
    }
    hardware::ensureI2cBus();
    if (sht4.begin(&Wire)) {
      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sensors_event_t humidity;
      sensors_event_t temperature;
      if (sht4.getEvent(&humidity, &temperature)) {
        tempC = temperature.temperature;
        humidityPct = humidity.relative_humidity;
        LOG.printf("[sensor] %.1fC %.0f%% RH (attempt %u)\n",
                   tempC, humidityPct, attempt + 1);
        return true;
      }
    }
    LOG.printf("[sensor] SHT4x attempt %u/%u failed\n", attempt + 1,
               attempts);
  }
  return false;
}

}  // namespace climate
