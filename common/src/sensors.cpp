#include "sensors.h"

#include "app_logger.h"
#include "battery_gauge.h"
#include "board_pins.h"
#include "charger.h"
#include "climate_sensor.h"

namespace sensors {

void readAll(int batteryEnablePin, int batteryAdcPin,
             Adafruit_SHT4x& sht4, int sht4Attempts,
             uint32_t sht4RetryDelayMs, Readings& out) {
#if RETERMINAL_MODEL == 1005
  const battery::FuelGaugeReading gauge = battery::readBq27220();
  out.batteryVoltage = gauge.voltage;
  out.batteryPct = gauge.percent;
  out.batteryValid = gauge.valid;
  pinMode(board::PIN_EXTERNAL_POWER, INPUT);
  out.externalPowerValid = true;
  out.externalPower = digitalRead(board::PIN_EXTERNAL_POWER) == HIGH;
  if (gauge.valid) {
    LOG.printf("[sensor] BQ27220 battery %.3fV -> %d%%, current=%dmA, "
               "external_power=%s\n",
               out.batteryVoltage, out.batteryPct,
               gauge.currentValid ? gauge.averageCurrentMa : 0,
               out.externalPower ? "yes" : "no");
  } else {
    LOG.println("[sensor] BQ27220 battery gauge unavailable");
  }
#else
  const bool adcValid =
      battery::measureBatteryFromAdc(batteryEnablePin, batteryAdcPin,
                                     out.batteryVoltage, out.batteryPct);
  LOG.printf("[sensor] battery %.3fV -> %d%%\n", out.batteryVoltage,
             out.batteryPct);
  const charger::Status chargerStatus = charger::readSy6974b();
  out.batteryValid = adcValid && chargerStatus.valid;
  out.externalPowerValid = chargerStatus.valid;
  out.externalPower = chargerStatus.state == charger::State::Connected;
#endif
  out.climateValid =
      climate::readSht4x(sht4, out.temperatureC, out.humidityPct,
                         sht4Attempts, sht4RetryDelayMs);
  if (!out.climateValid) {
    LOG.println("[sensor] SHT4x unavailable after retries");
  }
}

}  // namespace sensors
