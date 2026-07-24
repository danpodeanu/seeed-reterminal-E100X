#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

namespace pcf8563 {

constexpr uint8_t ADDRESS = 0x51;
constexpr uint8_t TIME_REGISTER = 0x02;

struct Reading {
  time_t epoch = 0;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  bool voltageLow = true;
};

inline uint8_t fromBcd(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0f));
}

inline uint8_t toBcd(int value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

inline bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

inline int daysInMonth(int year, int month) {
  static constexpr uint8_t days[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && leapYear(year) ? 29 : days[month - 1];
}

inline int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 +
         static_cast<int64_t>(dayOfEra) - 719468;
}

inline String format(const Reading& value) {
  char text[24] = {};
  snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
           value.year, value.month, value.day, value.hour, value.minute,
           value.second);
  return String(text);
}

inline bool readUtc(TwoWire& wire, Reading& value, String& error) {
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

inline bool writeUtc(TwoWire& wire, time_t epoch, String& error) {
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

inline bool setSystemClock(const Reading& value, String& error) {
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
