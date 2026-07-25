#pragma once

// Platform-level GPIO/bus helpers shared by every viewer app. These call
// into board_pins so all pin literals stay in one place.
namespace hardware {

// Drive the on-board status LED. Active-low on the reTerminal E-series.
void setStatusLed(bool on);

// Short chirp on the piezo buzzer (1 kHz for 100 ms).
void beep();

// Lazily initialise the I2C bus on the board's I2C_SDA/I2C_SCL pins at
// 100 kHz. Repeat calls are idempotent. Returns false only if Wire.begin
// fails; in that case an error is written to LOG.
bool ensureI2cBus();

// Force the next `ensureI2cBus()` call to re-initialise the bus. Useful
// after `Wire.end()` in a sensor retry loop.
void resetI2cBus();

// Configure a GPIO as an RTC wake source with pull-up, so it survives
// deep sleep. Returns true only if every underlying rtc_gpio_* call
// succeeded. Extracted from photo-viewer's prepareWakePin().
bool configureWakePin(int pin);

}  // namespace hardware
