#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

// Helpers for reading and writing UTC time to a PCF8563 hardware RTC over
// I2C. The BCD/civil-date helpers are kept inline (constexpr-friendly and
// used by unit tests); the Wire-backed transactions live in the .cpp.
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

String format(const Reading& value);
bool readUtc(TwoWire& wire, Reading& value, String& error);
bool writeUtc(TwoWire& wire, time_t epoch, String& error);
bool setSystemClock(const Reading& value, String& error);

}  // namespace pcf8563
