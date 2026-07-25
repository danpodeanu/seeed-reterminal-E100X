#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

class TimestampedLogger {
 public:
  explicit TimestampedLogger(HardwareSerial& serial) : serial_(serial) {}

  void begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) {
    serial_.begin(baud, config, rxPin, txPin);
  }

  void flush() {
    serial_.flush();
  }

  size_t println() {
    writePrefix();
    return serial_.println();
  }

  template <typename T>
  size_t println(const T& value) {
    writePrefix();
    return serial_.println(value);
  }

  size_t printf(const char* format, ...) {
    writePrefix();
    va_list arguments;
    va_start(arguments, format);
    const size_t written = serial_.vprintf(format, arguments);
    va_end(arguments);
    return written;
  }

 private:
  HardwareSerial& serial_;

  void writePrefix() {
    struct timeval currentTime = {};
    gettimeofday(&currentTime, nullptr);
    struct tm localTime = {};
    localtime_r(&currentTime.tv_sec, &localTime);
    char timestamp[24] = {};
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
             &localTime);
    serial_.printf("[%s.%03ld] ", timestamp,
                   static_cast<long>(currentTime.tv_usec / 1000));
  }
};
