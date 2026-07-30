#pragma once

#include <Arduino.h>
#include <FS.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

// A hardware-serial logger that timestamps every line and (optionally)
// tees each line to a File on the SD card for post-mortem debugging.
//
// The SD sink is off by default. Wire it up by calling attachSdSink()
// with a file handle opened elsewhere (see common/log_sd_sink.h for a
// helper that handles /logs/ mkdir and rotation) -- this class only
// owns the write path so it stays independent of the SD stack.
//
// Crash safety when the SD sink is attached: every write ends with
// File::flush(), which under the ESP-IDF SD library calls f_sync and
// persists both data blocks and the FAT/directory entry before
// returning. A subsequent power loss can only lose bytes that were
// still in-flight; every line the logger has already returned from is
// guaranteed to be on disk.
class TimestampedLogger {
 public:
  explicit TimestampedLogger(HardwareSerial& serial) : serial_(serial) {}

  void begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) {
    serial_.begin(baud, config, rxPin, txPin);
  }

  void flush() {
    serial_.flush();
    if (sdFile_) sdFile_.flush();
  }

  // Take ownership of an already-opened SD file. Any previously
  // attached sink is closed first. Pass an invalid/closed File to
  // detach (equivalent to detachSdSink()).
  void attachSdSink(fs::File file) {
    if (sdFile_) sdFile_.close();
    sdFile_ = std::move(file);
  }

  void detachSdSink() {
    if (sdFile_) sdFile_.close();
    sdFile_ = fs::File();
  }

  size_t println() {
    writePrefix();
    const size_t written = serial_.println();
    if (sdFile_) {
      sdFile_.println();
      sdFile_.flush();
    }
    return written;
  }

  template <typename T>
  size_t println(const T& value) {
    writePrefix();
    const size_t written = serial_.println(value);
    if (sdFile_) {
      sdFile_.println(value);
      sdFile_.flush();
    }
    return written;
  }

  size_t printf(const char* format, ...) {
    writePrefix();
    va_list arguments;
    va_start(arguments, format);
    va_list argumentsCopy;
    va_copy(argumentsCopy, arguments);
    const size_t written = serial_.vprintf(format, arguments);
    va_end(arguments);
    if (sdFile_) {
      // vprintf on a File is not part of the Arduino Stream API, so
      // format into a stack buffer and print that. 256 bytes matches
      // the longest lines the apps currently emit (per-image chunk
      // reports); longer messages are truncated in the SD copy only
      // and still make it to serial in full.
      char buffer[256];
      const int formatted =
          vsnprintf(buffer, sizeof(buffer), format, argumentsCopy);
      if (formatted > 0) {
        sdFile_.write(reinterpret_cast<const uint8_t*>(buffer),
                      formatted < static_cast<int>(sizeof(buffer))
                          ? static_cast<size_t>(formatted)
                          : sizeof(buffer) - 1);
        sdFile_.flush();
      }
    }
    va_end(argumentsCopy);
    return written;
  }

 private:
  HardwareSerial& serial_;
  fs::File sdFile_;

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
    if (sdFile_) {
      // Same format as serial; flush is deferred to the caller's write
      // so we only fsync once per full line rather than twice.
      sdFile_.printf("[%s.%03ld] ", timestamp,
                     static_cast<long>(currentTime.tv_usec / 1000));
    }
  }
};
