#pragma once

#include <Arduino.h>

// Shared battery gauge for all three viewer apps. The lookup table matches
// the discharge curve of the reTerminal E-series' lithium-ion cell (nominal
// 3.7 V, ~4.15 V full, ~3.27 V empty).
namespace battery {

// Convert a measured cell voltage to a 0..100 % state-of-charge using a
// piecewise-linear discharge curve. Values outside the table clamp to 0/100.
float percentForVoltage(float voltage);

// Enables the battery regulator via enablePin, waits for the rail to
// settle, and averages 16 ADC samples. Assumes a 1:1 divider on adcPin,
// so the raw reading is doubled to recover the cell voltage.
void measureBatteryFromAdc(int enablePin, int adcPin, float& voltage,
                           int& percent);

}  // namespace battery
