#pragma once

#ifndef USB_SCREEN_CAPTURE_ENABLED
#define USB_SCREEN_CAPTURE_ENABLED 1
#endif

#include <Arduino.h>

#if USB_SCREEN_CAPTURE_ENABLED
#include <string.h>

#include "screen_capture_bmp.h"
#include "usb_screen_capture_protocol.h"
#endif

namespace usb_screen_capture {

class Server {
 public:
  void begin(Stream& stream) {
#if USB_SCREEN_CAPTURE_ENABLED
    stream_ = &stream;
#else
    (void)stream;
#endif
  }

  template <typename EPaper>
  bool poll(EPaper& epaper, uint32_t width, uint32_t height) {
#if USB_SCREEN_CAPTURE_ENABLED
    return pollCommand([this, &epaper, width, height]() {
      capture(epaper, width, height);
    });
#else
    (void)epaper;
    (void)width;
    (void)height;
    return false;
#endif
  }

  bool pollUnavailable() {
#if USB_SCREEN_CAPTURE_ENABLED
    return pollCommand([this]() {
      writeText(kUnavailableResponse);
    });
#else
    return false;
#endif
  }

  template <typename EPaper>
  bool serveFor(EPaper& epaper, uint32_t width, uint32_t height,
                uint32_t waitMs = 1000) {
#if USB_SCREEN_CAPTURE_ENABLED
    const uint32_t startedAt = millis();
    do {
      if (poll(epaper, width, height)) return true;
      delay(1);
    } while (static_cast<uint32_t>(millis() - startedAt) < waitMs);
    return false;
#else
    (void)epaper;
    (void)width;
    (void)height;
    (void)waitMs;
    return false;
#endif
  }

  bool serveUnavailableFor(uint32_t waitMs = 1000) {
#if USB_SCREEN_CAPTURE_ENABLED
    const uint32_t startedAt = millis();
    do {
      if (pollUnavailable()) return true;
      delay(1);
    } while (static_cast<uint32_t>(millis() - startedAt) < waitMs);
    return false;
#else
    (void)waitMs;
    return false;
#endif
  }

 private:
#if USB_SCREEN_CAPTURE_ENABLED
  inline static constexpr char kUnavailableResponse[] =
      "RETERMINAL_SCREEN_CAPTURE_V1 ERROR framebuffer-unavailable\n";

  class CrcWriter {
   public:
    explicit CrcWriter(Stream& stream) : stream_(stream) {}

    size_t write(const uint8_t* bytes, size_t length) {
      const size_t written = stream_.write(bytes, length);
      crc_ = updateCrc32(crc_, bytes, written);
      return written;
    }

    uint32_t checksum() const { return crc_ ^ 0xFFFFFFFFUL; }

   private:
    Stream& stream_;
    uint32_t crc_ = 0xFFFFFFFFUL;
  };

  Stream* stream_ = nullptr;
  char command_[kMaximumCommandLength + 1] = {};
  size_t commandLength_ = 0;
  bool commandOverflow_ = false;

  template <typename Handler>
  bool pollCommand(Handler handler) {
    if (stream_ == nullptr) return false;
    while (stream_->available() > 0) {
      const int next = stream_->read();
      if (next < 0) break;
      const char character = static_cast<char>(next);
      if (character == '\r') continue;
      if (character != '\n') {
        if (commandLength_ < kMaximumCommandLength) {
          command_[commandLength_++] = character;
        } else {
          commandOverflow_ = true;
        }
        continue;
      }

      command_[commandLength_] = '\0';
      if (!commandOverflow_ && strcmp(command_, kRequest) == 0) {
        commandLength_ = 0;
        commandOverflow_ = false;
        handler();
        // A waiting host may have repeated its request while the framebuffer
        // was being streamed. Discard those duplicates as one transaction.
        while (stream_->available() > 0) stream_->read();
        return true;
      }
      commandLength_ = 0;
      commandOverflow_ = false;
    }
    return false;
  }

  bool writeText(const char* text) {
    return stream_ != nullptr &&
           screen_capture_bmp::writeBytes(
               *stream_, reinterpret_cast<const uint8_t*>(text), strlen(text));
  }

  template <typename EPaper>
  void capture(EPaper& epaper, uint32_t width, uint32_t height) {
    const screen_capture_bmp::Layout bmp =
        screen_capture_bmp::layout(width, height);
    char header[96];
    snprintf(header, sizeof(header),
             "RETERMINAL_SCREEN_CAPTURE_V1 OK %lu %lu %lu\n",
             static_cast<unsigned long>(width),
             static_cast<unsigned long>(height),
             static_cast<unsigned long>(bmp.fileSize));
    if (!writeText(header)) return;

    CrcWriter writer(*stream_);
    if (!screen_capture_bmp::write(epaper, width, height, writer)) return;

    char trailer[64];
    snprintf(trailer, sizeof(trailer),
             "RETERMINAL_SCREEN_CAPTURE_V1 END %08lX\n",
             static_cast<unsigned long>(writer.checksum()));
    writeText(trailer);
  }
#endif
};

}  // namespace usb_screen_capture
