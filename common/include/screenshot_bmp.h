#pragma once

#include <Arduino.h>
#include <SD.h>
#include <stdint.h>

#include "app_logger.h"
#include "screen_capture_bmp.h"
#include "sd_card.h"

// SD wrapper around the shared framebuffer-to-BMP encoder.

namespace screenshot {

template <typename EPaper>
inline bool saveScreenshotBmp(EPaper& epaper, uint32_t width, uint32_t height,
                              const char* screenshotPath = "/screenshot.bmp",
                              const char* temporaryPath = "/screenshot.bmp.part") {
  sd_card::removeFile(temporaryPath);
  File file = sd_card::openForWrite(temporaryPath);
  if (!file) {
    LOG.println("[screenshot] could not create temporary BMP");
    return false;
  }

  const bool ok = screen_capture_bmp::write(epaper, width, height, file);

  file.flush();
  file.close();

  if (!ok) {
    LOG.println("[screenshot] BMP write failed");
    sd_card::removeFile(temporaryPath);
    return false;
  }

  sd_card::removeFile(screenshotPath);
  if (!sd_card::renameFile(temporaryPath, screenshotPath)) {
    LOG.printf("[screenshot] could not install %s\n", screenshotPath);
    sd_card::removeFile(temporaryPath);
    return false;
  }

  const uint32_t fileSize =
      screen_capture_bmp::layout(width, height).fileSize;
  LOG.printf("[screenshot] saved %s (%lu bytes)\n", screenshotPath,
             static_cast<unsigned long>(fileSize));
  return true;
}

}  // namespace screenshot
