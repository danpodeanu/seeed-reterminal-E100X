#pragma once

// low_battery -- shared "please recharge" decision for the three viewers.
//
// Each viewer already calls sensors::readAll() on every wake, which
// populates sensors::Readings::{batteryVoltage, batteryPct, batteryValid,
// externalPower}. This module turns that into a single yes/no verdict.
//
// On E1001-E1004, "battery sensor present" is inferred from the SY6974B
// PMIC responding on I2C on newer E1001/E1002/E1003/E1004 revisions and
// silently drops off older E1001/E1002 boards that shipped with an
// ETA6003 (not I2C-addressable). On those older boards the battery ADC
// still reads a value, but we have no independent cross-check that a
// battery is even wired in, so we skip the warning rather than
// false-positive on a bare-USB power path. E1005 gets validity directly
// from its BQ27220 fuel gauge.
//
// The threshold defaults to 5% and can be overridden at build time via
// -DLOW_BATTERY_THRESHOLD_PCT=<n>. Useful for testing without draining
// a real battery: build with -DLOW_BATTERY_THRESHOLD_PCT=100 to make
// every wake fire the warning.

namespace low_battery {

#ifndef LOW_BATTERY_THRESHOLD_PCT
#define LOW_BATTERY_THRESHOLD_PCT 5
#endif

constexpr int kThresholdPct = LOW_BATTERY_THRESHOLD_PCT;

// Returns true when the viewer should render the recharge screen and
// bail out of the normal refresh path.
//
// Skipped if:
//   - the app's config knob is off (enabled=false)
//   - no reliable battery gauge is available
//   - USB power is connected (externalPower=true; device is charging)
//   - batteryPct is out of range (defensive; sensors::readAll always
//     writes 0..100, but a fresh Readings default is -1)
inline bool shouldWarn(bool enabled, bool batteryValid, bool externalPower,
                       int batteryPct, int thresholdPct = kThresholdPct) {
  if (!enabled) return false;
  if (!batteryValid) return false;
  if (externalPower) return false;
  if (batteryPct < 0) return false;
  return batteryPct < thresholdPct;
}

}  // namespace low_battery
