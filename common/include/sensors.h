#pragma once

#include <Adafruit_SHT4x.h>
#include <math.h>
#include <stdint.h>

// One-shot sensor read used at boot by every viewer app. Each app
// previously carried its own readSensors() that combined the shared
// battery::measureBatteryFromAdc and climate::readSht4x helpers into
// a single "populate my battery + SHT4x globals" step. This module
// consolidates that pattern into a single struct + call.
//
// The pin numbers and retry counts stay caller-provided because they
// come from the app's board_pins / config namespaces; common/ never
// looks at those directly.
namespace sensors {

// All values a viewer app typically reads from the on-board sensors.
// Defaults represent "no valid reading" so a struct can be logged /
// rendered before readAll() succeeds.
struct Readings {
  float batteryVoltage = NAN;
  int batteryPct = -1;
  bool batteryValid = false;
  bool climateValid = false;
  float temperatureC = NAN;
  float humidityPct = NAN;
  bool externalPowerValid = false;
  bool externalPower = false;
};

// Populate `out` with a fresh battery sample and, if reachable, an
// SHT4x temperature + humidity reading. Logs a one-line summary via
// LOG for parity with the historical readSensors() implementations.
// The SHT4x retry policy mirrors climate::readSht4x's own semantics.
void readAll(int batteryEnablePin, int batteryAdcPin,
             Adafruit_SHT4x& sht4, int sht4Attempts,
             uint32_t sht4RetryDelayMs, Readings& out);

}  // namespace sensors
