#include "battery_gauge.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "battery_gauge_pure.h"
#include "hardware.h"

namespace battery {
namespace {

constexpr uint8_t BQ27220_ADDRESS = 0x55;

bool readBq27220Word(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(BQ27220_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(BQ27220_ADDRESS, static_cast<uint8_t>(2)) != 2) {
    return false;
  }
  const uint8_t low = Wire.read();
  const uint8_t high = Wire.read();
  value = pure::littleEndianWord(low, high);
  return true;
}

}  // namespace

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

bool measureBatteryFromAdc(int enablePin, int adcPin, float& voltage,
                           int& percent) {
  voltage = NAN;
  percent = -1;
  if (!pure::adcPinsUsable(enablePin, adcPin)) return false;

  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(adcPin, ADC_11db);

  // The battery divider needs a short time for its filter cap to
  // settle after the enable switch closes. Rather than a blind
  // delay(200), give the cap a small minimum window and then poll
  // the ADC until two consecutive reads agree within a tight mV
  // threshold. A budget guards against a stuck ADC or an unusually
  // leaky divider so the measurement still returns in bounded time.
  constexpr uint32_t kSettleMinMs = 20;
  constexpr uint32_t kSettleBudgetMs = 250;
  constexpr uint32_t kSettlePollMs = 5;
  constexpr uint32_t kSettleToleranceMv = 10;
  delay(kSettleMinMs);
  uint32_t prev = analogReadMilliVolts(adcPin);
  const uint32_t startMs = millis();
  while ((millis() - startMs) < (kSettleBudgetMs - kSettleMinMs)) {
    delay(kSettlePollMs);
    const uint32_t now = analogReadMilliVolts(adcPin);
    const uint32_t delta = (now > prev) ? (now - prev) : (prev - now);
    prev = now;
    if (delta <= kSettleToleranceMv) break;
  }

  // Oversample to average out ADC noise. delay(1) is enough spacing
  // for noise decorrelation on the ESP32-S3 ADC; the prior delay(4)
  // was conservative and multiplied out to 64 ms per wake.
  uint32_t totalMv = 0;
  for (int i = 0; i < 16; ++i) {
    totalMv += analogReadMilliVolts(adcPin);
    delay(1);
  }
  voltage = (totalMv / 16.0f) * 2.0f / 1000.0f;
  percent = constrain(
      static_cast<int>(percentForVoltage(voltage) + 0.5f), 0, 100);
  return true;
}

FuelGaugeReading readBq27220() {
  FuelGaugeReading reading;
  if (!hardware::ensureI2cBus()) return reading;

  uint16_t voltageMv = 0;
  uint16_t stateOfCharge = 0;
  if (!readBq27220Word(pure::BQ27220_VOLTAGE_REGISTER, voltageMv) ||
      !readBq27220Word(pure::BQ27220_STATE_OF_CHARGE_REGISTER,
                       stateOfCharge) ||
      !pure::stateOfChargeValid(stateOfCharge)) {
    return reading;
  }

  reading.valid = true;
  reading.voltage = voltageMv / 1000.0f;
  reading.percent = static_cast<int>(stateOfCharge);

  uint16_t averageCurrent = 0;
  if (readBq27220Word(pure::BQ27220_AVERAGE_CURRENT_REGISTER,
                      averageCurrent)) {
    reading.currentValid = true;
    reading.averageCurrentMa = pure::signedWord(averageCurrent);
  }
  return reading;
}

}  // namespace battery
