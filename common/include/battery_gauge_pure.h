#pragma once

#include <stdint.h>

namespace battery {
namespace pure {

inline constexpr uint8_t BQ27220_VOLTAGE_REGISTER = 0x08;
inline constexpr uint8_t BQ27220_AVERAGE_CURRENT_REGISTER = 0x14;
inline constexpr uint8_t BQ27220_STATE_OF_CHARGE_REGISTER = 0x2C;

constexpr bool adcPinsUsable(int enablePin, int adcPin) {
  return enablePin >= 0 && adcPin >= 0;
}

constexpr uint16_t littleEndianWord(uint8_t low, uint8_t high) {
  return static_cast<uint16_t>(low) |
         (static_cast<uint16_t>(high) << 8);
}

constexpr int16_t signedWord(uint16_t value) {
  return static_cast<int16_t>(value);
}

constexpr int16_t littleEndianSignedWord(uint8_t low, uint8_t high) {
  return signedWord(littleEndianWord(low, high));
}

// BQ27220 current is positive while discharging and negative while charging.
constexpr bool chargingFromAverageCurrent(int16_t currentMa) {
  return currentMa < 0;
}

constexpr bool stateOfChargeValid(uint16_t percent) {
  return percent <= 100;
}

}  // namespace pure
}  // namespace battery
