#include "driver.h"

#if RETERMINAL_MODEL == 1005

#include "e1005_fast_refresh.h"

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "board_pins.h"

namespace {

constexpr int kDisplayWidth = 480;
constexpr int kDisplayHeight = 800;
constexpr int kDisplayStrideBytes = kDisplayHeight / 8;
// The SSD1677 write limit is 20 MHz; Seeed's Sticky driver uses 10 MHz on
// the SPI bus shared with the SD slot.
constexpr uint32_t kPanelSpiHz = 10000000;
constexpr uint32_t kBusyAssertTimeoutUs = 20000;
constexpr uint32_t kRefreshTimeoutUs = 10000000;

uint8_t reverseBits(uint8_t value) {
  value = static_cast<uint8_t>((value >> 4) | (value << 4));
  value = static_cast<uint8_t>(((value & 0xCCU) >> 2) |
                               ((value & 0x33U) << 2));
  return static_cast<uint8_t>(((value & 0xAAU) >> 1) |
                              ((value & 0x55U) << 1));
}

}  // namespace

E1005FastRefresh::E1005FastRefresh(EPaper& display) : display_(display) {}

E1005FastRefresh::~E1005FastRefresh() {
  end();
}

E1005FastRefresh::Result E1005FastRefresh::begin() {
  ready_ = false;
  if (!allocateBuffers()) return Result::AllocationFailed;

  const auto* framebuffer =
      static_cast<const uint8_t*>(display_.getPointer());
  if (!framebuffer) return Result::NoFramebuffer;

  display_.wake();
  buildDisplayPlane(framebuffer, previousPlane_);
  std::memcpy(nextPlane_, previousPlane_, kFramebufferSize);
  seedBaseline(previousPlane_);
  ready_ = true;
  return Result::Ok;
}

E1005FastRefresh::Result E1005FastRefresh::refresh(
    const Region& requestedRegion, Timing& timing) {
  timing = {};
  if (!ready_) return Result::NotReady;

  const int left = std::max(0, requestedRegion.x);
  const int top = std::max(0, requestedRegion.y);
  const int right =
      std::min(kDisplayWidth, requestedRegion.x + requestedRegion.width);
  const int bottom =
      std::min(kDisplayHeight, requestedRegion.y + requestedRegion.height);
  if (right <= left || bottom <= top) return Result::InvalidRegion;

  const auto* framebuffer =
      static_cast<const uint8_t*>(display_.getPointer());
  if (!framebuffer) return Result::NoFramebuffer;

  const uint32_t refreshStartedUs = micros();
  buildDisplayPlane(framebuffer, nextPlane_);
  const uint32_t preparedUs = micros();
  timing.prepareUs = preparedUs - refreshStartedUs;

  const Region clipped = {left, top, right - left, bottom - top};
  const NativeRegion native = nativeRegion(clipped);
  const uint8_t internalTemperature = 0x80;
  const uint8_t fastBorderWaveform = 0x80;
  const uint8_t fastUpdateSequence = 0xFF;

  writePanelCommand(0x18, &internalTemperature, 1);
  writePanelCommand(0x3C, &fastBorderWaveform, 1);
  setPartialWindow(native);
  writeDisplayPlaneWindow(0x26, previousPlane_, native);
  setPartialWindow(native);
  writeDisplayPlaneWindow(0x24, nextPlane_, native);
  setPartialWindow(native);
  writePanelCommand(0x22, &fastUpdateSequence, 1);
  const uint32_t panelStartedUs = micros();
  writePanelCommand(0x20);
  timing.transferUs = micros() - preparedUs;

  while (digitalRead(TFT_BUSY) == LOW &&
         micros() - panelStartedUs < kBusyAssertTimeoutUs) {
    delayMicroseconds(50);
  }
  if (digitalRead(TFT_BUSY) == LOW) {
    timing.panelUs = micros() - panelStartedUs;
    timing.totalUs = micros() - refreshStartedUs;
    return Result::BusyNotAsserted;
  }

  while (digitalRead(TFT_BUSY) == HIGH) {
    if (micros() - panelStartedUs >= kRefreshTimeoutUs) {
      timing.panelUs = micros() - panelStartedUs;
      timing.totalUs = micros() - refreshStartedUs;
      return Result::TimedOut;
    }
    delay(1);
  }

  const uint32_t completedUs = micros();
  timing.panelUs = completedUs - panelStartedUs;
  setPartialWindow(native);
  writeDisplayPlaneWindow(0x24, nextPlane_, native);
  setPartialWindow(native);
  writeDisplayPlaneWindow(0x26, nextPlane_, native);
  const uint32_t reseededUs = micros();
  timing.reseedUs = reseededUs - completedUs;
  timing.totalUs = reseededUs - refreshStartedUs;
  std::swap(previousPlane_, nextPlane_);
  return Result::Ok;
}

E1005FastRefresh::Result
E1005FastRefresh::refreshWithGray4Waveform(Timing& timing) {
  timing = {};
  end();

  const auto* framebuffer =
      static_cast<const uint8_t*>(display_.getPointer());
  if (!framebuffer) return Result::NoFramebuffer;

  const uint32_t refreshStartedUs = micros();
  timing.prepareUs = micros() - refreshStartedUs;

  display_.wake();
  const uint8_t grayBorderWaveform = 0x00;
  const uint8_t internalTemperature = 0x80;
  const uint8_t grayWaveformTemperature[] = {0x67, 0x00};
  const uint8_t grayUpdateSequence = 0xD7;
  const NativeRegion fullPanel = {0, 0, kDisplayHeight, kDisplayWidth};

  writePanelCommand(0x3C, &grayBorderWaveform, 1);
  writePanelCommand(0x18, &internalTemperature, 1);
  writePanelCommand(0x1A, grayWaveformTemperature,
                    sizeof(grayWaveformTemperature));
  setPartialWindow(fullPanel);
  writeBinaryGray4Plane(0x24, framebuffer);
  setPartialWindow(fullPanel);
  writeBinaryGray4Plane(0x26, framebuffer);
  writePanelCommand(0x22, &grayUpdateSequence, 1);
  const uint32_t panelStartedUs = micros();
  writePanelCommand(0x20);
  timing.transferUs = micros() - refreshStartedUs - timing.prepareUs;

  while (digitalRead(TFT_BUSY) == LOW &&
         micros() - panelStartedUs < kBusyAssertTimeoutUs) {
    delayMicroseconds(50);
  }
  if (digitalRead(TFT_BUSY) == LOW) {
    timing.panelUs = micros() - panelStartedUs;
    timing.totalUs = micros() - refreshStartedUs;
    return Result::BusyNotAsserted;
  }

  while (digitalRead(TFT_BUSY) == HIGH) {
    if (micros() - panelStartedUs >= kRefreshTimeoutUs) {
      timing.panelUs = micros() - panelStartedUs;
      timing.totalUs = micros() - refreshStartedUs;
      return Result::TimedOut;
    }
    delay(1);
  }

  timing.panelUs = micros() - panelStartedUs;
  timing.totalUs = micros() - refreshStartedUs;
  display_.sleep();
  return Result::Ok;
}

void E1005FastRefresh::end() {
  ready_ = false;
  free(previousPlane_);
  free(nextPlane_);
  previousPlane_ = nullptr;
  nextPlane_ = nullptr;
}

const char* E1005FastRefresh::resultMessage(Result result) {
  switch (result) {
    case Result::Ok:
      return "ok";
    case Result::NotReady:
      return "fast refresh is not initialized";
    case Result::InvalidRegion:
      return "refresh region is outside the display";
    case Result::NoFramebuffer:
      return "display framebuffer is unavailable";
    case Result::AllocationFailed:
      return "cannot allocate display planes";
    case Result::BusyNotAsserted:
      return "panel did not assert BUSY";
    case Result::TimedOut:
      return "panel refresh timed out";
  }
  return "unknown fast-refresh result";
}

bool E1005FastRefresh::allocateBuffers() {
  if (!previousPlane_) {
    previousPlane_ = static_cast<uint8_t*>(malloc(kFramebufferSize));
  }
  if (!nextPlane_) {
    nextPlane_ = static_cast<uint8_t*>(malloc(kFramebufferSize));
  }
  if (previousPlane_ && nextPlane_) return true;

  free(previousPlane_);
  free(nextPlane_);
  previousPlane_ = nullptr;
  nextPlane_ = nullptr;
  return false;
}

E1005FastRefresh::NativeRegion E1005FastRefresh::nativeRegion(
    const Region& region) {
  const int unalignedLeft = kDisplayHeight - region.y - region.height;
  const int unalignedRight = unalignedLeft + region.height;
  const int left = unalignedLeft & ~7;
  const int right = (unalignedRight + 7) & ~7;
  return {left, region.x, right - left, region.width};
}

void E1005FastRefresh::buildDisplayPlane(const uint8_t* framebuffer,
                                         uint8_t* destination) {
  for (int row = 0; row < kDisplayWidth; ++row) {
    const uint8_t* source = framebuffer + row * kDisplayStrideBytes;
    for (int column = kDisplayStrideBytes - 1; column >= 0; --column) {
      *destination++ = reverseBits(source[column]);
    }
  }
}

void E1005FastRefresh::writePanelCommand(uint8_t command,
                                         const uint8_t* data, size_t length) {
  SPIClass& panelSpi = display_.getSPIinstance();
  panelSpi.beginTransaction(SPISettings(kPanelSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(board::PIN_SD_CS, HIGH);
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  panelSpi.transfer(command);
  if (data && length > 0) {
    digitalWrite(TFT_DC, HIGH);
    panelSpi.writeBytes(data, length);
  }
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);
  panelSpi.endTransaction();
}

void E1005FastRefresh::writeDisplayPlaneWindow(
    uint8_t command, const uint8_t* data, const NativeRegion& region) {
  const int left = kDisplayHeight - region.left - region.width;
  const int leftByte = left / 8;
  const size_t rowBytes = static_cast<size_t>(region.width / 8);

  SPIClass& panelSpi = display_.getSPIinstance();
  panelSpi.beginTransaction(SPISettings(kPanelSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(board::PIN_SD_CS, HIGH);
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  panelSpi.transfer(command);
  digitalWrite(TFT_DC, HIGH);
  for (int row = region.top; row < region.top + region.height; ++row) {
    panelSpi.writeBytes(data + row * kDisplayStrideBytes + leftByte,
                        rowBytes);
  }
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);
  panelSpi.endTransaction();
}

void E1005FastRefresh::writeBinaryGray4Plane(
    uint8_t command, const uint8_t* framebuffer) {
  uint8_t rowBuffer[kDisplayStrideBytes];
  SPIClass& panelSpi = display_.getSPIinstance();
  panelSpi.beginTransaction(SPISettings(kPanelSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(board::PIN_SD_CS, HIGH);
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  panelSpi.transfer(command);
  digitalWrite(TFT_DC, HIGH);
  for (int row = 0; row < kDisplayWidth; ++row) {
    const uint8_t* source = framebuffer + row * kDisplayStrideBytes;
    for (int column = kDisplayStrideBytes - 1; column >= 0; --column) {
      // The 1-bpp buffer stores white as 1; Seeed's Gray4 endpoint encoding
      // stores white as 00 and black as 11 across the two controller planes.
      rowBuffer[kDisplayStrideBytes - 1 - column] =
          static_cast<uint8_t>(~reverseBits(source[column]));
    }
    panelSpi.writeBytes(rowBuffer, sizeof(rowBuffer));
  }
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);
  panelSpi.endTransaction();
}

void E1005FastRefresh::setPartialWindow(const NativeRegion& region) {
  const uint16_t left =
      static_cast<uint16_t>(kDisplayHeight - region.left - region.width);
  const uint16_t right =
      static_cast<uint16_t>(kDisplayHeight - region.left - 1);
  const uint16_t top = static_cast<uint16_t>(region.top);
  const uint16_t bottom =
      static_cast<uint16_t>(region.top + region.height - 1);

  const uint8_t xRange[] = {
      static_cast<uint8_t>(left & 0xFFU),
      static_cast<uint8_t>(left >> 8),
      static_cast<uint8_t>(right & 0xFFU),
      static_cast<uint8_t>(right >> 8),
  };
  const uint8_t yRange[] = {
      static_cast<uint8_t>(top & 0xFFU),
      static_cast<uint8_t>(top >> 8),
      static_cast<uint8_t>(bottom & 0xFFU),
      static_cast<uint8_t>(bottom >> 8),
  };
  const uint8_t xCounter[] = {
      static_cast<uint8_t>(left & 0xFFU),
      static_cast<uint8_t>(left >> 8),
  };
  const uint8_t yCounter[] = {
      static_cast<uint8_t>(top & 0xFFU),
      static_cast<uint8_t>(top >> 8),
  };

  writePanelCommand(0x44, xRange, sizeof(xRange));
  writePanelCommand(0x45, yRange, sizeof(yRange));
  writePanelCommand(0x4E, xCounter, sizeof(xCounter));
  writePanelCommand(0x4F, yCounter, sizeof(yCounter));
}

void E1005FastRefresh::seedBaseline(const uint8_t* plane) {
  const NativeRegion fullPanel = {0, 0, kDisplayHeight, kDisplayWidth};
  setPartialWindow(fullPanel);
  writeDisplayPlaneWindow(0x24, plane, fullPanel);
  setPartialWindow(fullPanel);
  writeDisplayPlaneWindow(0x26, plane, fullPanel);
}

#endif
