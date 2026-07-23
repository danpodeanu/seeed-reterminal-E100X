#pragma once

#include <Arduino.h>

namespace config {

#ifndef RETERMINAL_MODEL
#define RETERMINAL_MODEL 1001
#endif

constexpr int MODEL = RETERMINAL_MODEL;

#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
constexpr int PANEL_WIDTH = 800;
constexpr int PANEL_HEIGHT = 480;
constexpr int UI_SCALE_NUMERATOR = 1;
constexpr int UI_SCALE_DENOMINATOR = 1;
#elif RETERMINAL_MODEL == 1003
constexpr int PANEL_WIDTH = 1872;
constexpr int PANEL_HEIGHT = 1404;
constexpr int UI_SCALE_NUMERATOR = 9;
constexpr int UI_SCALE_DENOMINATOR = 4;
#elif RETERMINAL_MODEL == 1004
constexpr int PANEL_WIDTH = 1200;
constexpr int PANEL_HEIGHT = 1600;
constexpr int UI_SCALE_NUMERATOR = 3;
constexpr int UI_SCALE_DENOMINATOR = 2;
#else
#error "Unsupported RETERMINAL_MODEL"
#endif

constexpr int ui(int e1001Pixels) {
  return (e1001Pixels * UI_SCALE_NUMERATOR + UI_SCALE_DENOMINATOR / 2) /
         UI_SCALE_DENOMINATOR;
}

constexpr uint64_t SLEEP_SECONDS = 15ULL * 60ULL;
constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t HTTP_TIMEOUT_MS = 25000;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 10000;
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr size_t MAX_IMAGE_BYTES = 6U * 1024U * 1024U;
constexpr size_t MAX_LIVE_IMAGE_BYTES = 2U * 1024U * 1024U;
constexpr uint8_t MAX_COMIC_ATTEMPTS = 8;
constexpr uint32_t LATEST_CHECK_CYCLES = 24;  // 24 x 15 minutes = 6 hours

// Preserve the previous server's rule: do not make a comic illegible merely
// to squeeze it onto the panel.
constexpr float MIN_DISPLAY_SCALE = 0.65f;
constexpr float DITHER_GAMMA = 1.0f;

constexpr int CONTENT_MARGIN_X = ui(10);
constexpr int CONTENT_TOP = ui(50);
constexpr int FOOTER_BOTTOM = PANEL_HEIGHT - ui(12);
constexpr int FOOTER_MAX_LINES = 3;
constexpr int FOOTER_LINE_HEIGHT = ui(22);

constexpr char CACHE_DIR[] = "/xkcd";
constexpr char LATEST_CACHE[] = "/xkcd/latest.json";
constexpr char XKCD_LATEST_URL[] = "https://xkcd.com/info.0.json";

}  // namespace config
