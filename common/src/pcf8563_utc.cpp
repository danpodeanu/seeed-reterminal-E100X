#include "pcf8563_utc.h"

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

namespace pcf8563 {

String format(const Reading& value) {
  char text[24] = {};
  snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
           value.year, value.month, value.day, value.hour, value.minute,
           value.second);
  return String(text);
}

bool readUtc(TwoWire& wire, Reading& value, String& error) {
  error = "";
  wire.beginTransmission(ADDRESS);
  wire.write(TIME_REGISTER);
  const uint8_t status = wire.endTransmission(false);
  if (status != 0) {
    error = "I2C error " + String(status);
    return false;
  }

  constexpr uint8_t registerCount = 7;
  if (wire.requestFrom(ADDRESS, registerCount) != registerCount) {
    error = "short RTC register read";
    return false;
  }

  const uint8_t secondsRegister = wire.read();
  value.voltageLow = (secondsRegister & 0x80) != 0;
  value.second = fromBcd(secondsRegister & 0x7f);
  value.minute = fromBcd(wire.read() & 0x7f);
  value.hour = fromBcd(wire.read() & 0x3f);
  value.day = fromBcd(wire.read() & 0x3f);
  wire.read();  // weekday
  value.month = fromBcd(wire.read() & 0x1f);
  value.year = 2000 + fromBcd(wire.read());

  if (value.year < 2000 || value.year > 2099 || value.second > 59 ||
      value.minute > 59 || value.hour > 23 ||
      value.month < 1 || value.month > 12 || value.day < 1 ||
      value.day > daysInMonth(value.year, value.month)) {
    error = "invalid RTC register values";
    return false;
  }

  value.epoch = static_cast<time_t>(
      daysFromCivil(value.year, value.month, value.day) * 86400LL +
      value.hour * 3600LL + value.minute * 60LL + value.second);
  return true;
}

bool writeUtc(TwoWire& wire, time_t epoch, String& error) {
  error = "";
  struct tm utc = {};
  if (epoch <= 0 || gmtime_r(&epoch, &utc) == nullptr ||
      utc.tm_year + 1900 < 2000 || utc.tm_year + 1900 > 2099) {
    error = "UTC time is outside the PCF8563 range";
    return false;
  }

  wire.beginTransmission(ADDRESS);
  wire.write(TIME_REGISTER);
  wire.write(toBcd(utc.tm_sec));  // Writing bit 7 clear also clears VL.
  wire.write(toBcd(utc.tm_min));
  wire.write(toBcd(utc.tm_hour));
  wire.write(toBcd(utc.tm_mday));
  wire.write(toBcd(utc.tm_wday));
  wire.write(toBcd(utc.tm_mon + 1));
  wire.write(toBcd(utc.tm_year + 1900 - 2000));
  const uint8_t status = wire.endTransmission();
  if (status != 0) {
    error = "I2C error " + String(status);
    return false;
  }
  return true;
}

bool setSystemClock(const Reading& value, String& error) {
  error = "";
  if (value.voltageLow) {
    error = "VL is set";
    return false;
  }
  struct timeval current = {value.epoch, 0};
  if (settimeofday(&current, nullptr) != 0) {
    error = "settimeofday failed";
    return false;
  }
  return true;
}

}  // namespace pcf8563
