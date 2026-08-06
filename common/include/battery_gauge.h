#pragma once

#include <Arduino.h>
#include <math.h>

// Shared battery gauge for all three viewer apps. The lookup table matches
// the discharge curve of the reTerminal E-series' lithium-ion cell (nominal
// 3.7 V, ~4.15 V full, ~3.27 V empty).
namespace battery {

// Convert a measured cell voltage to a 0..100 % state-of-charge using a
// piecewise-linear discharge curve. Values outside the table clamp to 0/100.
float percentForVoltage(float voltage);

// Enables the battery regulator via enablePin, waits for the rail to
// settle, and averages 16 ADC samples. Assumes a 1:1 divider on adcPin,
// so the raw reading is doubled to recover the cell voltage. Returns false
// and leaves the outputs unavailable when either pin is absent.
bool measureBatteryFromAdc(int enablePin, int adcPin, float& voltage,
                           int& percent);

struct FuelGaugeReading {
  bool valid = false;
  bool currentValid = false;
  float voltage = NAN;
  int percent = -1;
  int averageCurrentMa = 0;
};

// Read the BQ27220 fuel gauge used by E1005. Voltage and state of charge are
// mandatory; average current is separate so callers can infer charging.
FuelGaugeReading readBq27220();

}  // namespace battery
