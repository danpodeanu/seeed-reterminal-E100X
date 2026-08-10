#pragma once

#include <Arduino.h>
#include <SD.h>
#include <stdint.h>

#include "app_logger.h"
#include "screen_capture_png.h"
#include "sd_card.h"

// SD wrapper around the shared framebuffer-to-PNG encoder.

namespace screenshot {

template <typename EPaper>
inline bool saveScreenshotPng(EPaper& epaper, uint32_t width, uint32_t height,
                              const char* screenshotPath = "/screenshot.png",
                              const char* temporaryPath =
                                  "/screenshot.png.part") {
  sd_card::removeFile(temporaryPath);
  File file = sd_card::openForWrite(temporaryPath);
  if (!file) {
    LOG.println("[screenshot] could not create temporary PNG");
    return false;
  }

  const bool ok = screen_capture_png::write(epaper, width, height, file);

  file.flush();
  file.close();

  if (!ok) {
    LOG.println("[screenshot] PNG write failed");
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
      screen_capture_png::layout(width, height).fileSize;
  LOG.printf("[screenshot] saved %s (%lu bytes)\n", screenshotPath,
             static_cast<unsigned long>(fileSize));
  return true;
}

}  // namespace screenshot
