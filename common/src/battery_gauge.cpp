#include "battery_gauge.h"

#include <Arduino.h>

namespace battery {

float percentForVoltage(float voltage) {
  static constexpr float volts[] = {
      3.27f, 3.30f, 3.41f, 3.49f, 3.58f, 3.68f,
      3.75f, 3.80f, 3.85f, 3.91f, 3.96f, 4.15f};
  static constexpr float percents[] = {
      0.0f, 5.0f, 10.0f, 20.0f, 30.0f, 40.0f,
      50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f};
  constexpr size_t count = sizeof(volts) / sizeof(volts[0]);
  if (voltage <= volts[0]) return 0.0f;
  if (voltage >= volts[count - 1]) return 100.0f;
  for (size_t i = 1; i < count; ++i) {
    if (voltage <= volts[i]) {
      const float fraction =
          (voltage - volts[i - 1]) / (volts[i] - volts[i - 1]);
      return percents[i - 1] + fraction * (percents[i] - percents[i - 1]);
    }
  }
  return 0.0f;
}

void measureBatteryFromAdc(int enablePin, int adcPin, float& voltage,
                           int& percent) {
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  delay(200);
  analogReadResolution(12);
  analogSetPinAttenuation(adcPin, ADC_11db);
  uint32_t totalMv = 0;
  for (int i = 0; i < 16; ++i) {
    totalMv += analogReadMilliVolts(adcPin);
    delay(4);
  }
  voltage = (totalMv / 16.0f) * 2.0f / 1000.0f;
  percent = constrain(
      static_cast<int>(percentForVoltage(voltage) + 0.5f), 0, 100);
}

}  // namespace battery
