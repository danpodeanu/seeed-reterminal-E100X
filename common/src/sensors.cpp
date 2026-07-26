#include "sensors.h"

#include "app_logger.h"
#include "battery_gauge.h"
#include "charger.h"
#include "climate_sensor.h"

namespace sensors {

void readAll(int batteryEnablePin, int batteryAdcPin,
             Adafruit_SHT4x& sht4, int sht4Attempts,
             uint32_t sht4RetryDelayMs, Readings& out) {
  battery::measureBatteryFromAdc(batteryEnablePin, batteryAdcPin,
                                 out.batteryVoltage, out.batteryPct);
  LOG.printf("[sensor] battery %.3fV -> %d%%\n", out.batteryVoltage,
             out.batteryPct);
  const charger::Status chargerStatus = charger::readSy6974b();
  out.chargerValid = chargerStatus.valid;
  out.externalPower = chargerStatus.state == charger::State::Connected;
  out.climateValid =
      climate::readSht4x(sht4, out.temperatureC, out.humidityPct,
                         sht4Attempts, sht4RetryDelayMs);
  if (!out.climateValid) {
    LOG.println("[sensor] SHT4x unavailable after retries");
  }
}

}  // namespace sensors
