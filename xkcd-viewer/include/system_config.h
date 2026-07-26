#pragma once

// System-level configuration for the xkcd viewer. These constants describe
// the reTerminal hardware, timing budgets, PSRAM budgets, dither/render
// tuning, and on-flash cache layout.
//
// This file is included from the bottom of config.h. Do not include it
// directly -- always include config.h instead.

#include <Arduino.h>

namespace config {

// --- Hardware model (build-time) --------------------------------------------
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

// Convert a coordinate authored for the E1001 panel (800x480) into the
// equivalent number of pixels on the active panel.
constexpr int ui(int e1001Pixels) {
  return (e1001Pixels * UI_SCALE_NUMERATOR + UI_SCALE_DENOMINATOR / 2) /
         UI_SCALE_DENOMINATOR;
}

// --- Timeouts ---------------------------------------------------------------
constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t HTTP_TIMEOUT_MS = 25000;
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 1000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t NTP_REFRESH_SECONDS = 6UL * 60UL * 60UL;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 10000;

// --- Sensors and buttons ----------------------------------------------------
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr uint32_t SCREENSHOT_LONG_PRESS_MS = 1500;
constexpr uint32_t BUTTON_RELEASE_DEBOUNCE_MS = 40;

// --- Comic and image budget -------------------------------------------------
constexpr size_t MAX_IMAGE_BYTES = 6U * 1024U * 1024U;
constexpr size_t MAX_LIVE_IMAGE_BYTES = 2U * 1024U * 1024U;
// Reject comics whose decoded source or rendered target would blow past a
// safe pixel budget. RGB888 decode uses w*h*3 bytes, resize target uses
// w*h bytes; both compete for the 8 MB PSRAM. 2.5 M source pixels caps
// the decode buffer at ~7.5 MB. The render cap is the full panel -- the
// layout math never intentionally exceeds it, so anything larger is a
// bug or pathological aspect ratio and we'd rather skip than allocate.
constexpr size_t MAX_DECODED_PIXELS = 2500000U;
constexpr size_t MAX_RENDER_PIXELS =
    static_cast<size_t>(PANEL_WIDTH) * static_cast<size_t>(PANEL_HEIGHT);
constexpr uint8_t MAX_COMIC_ATTEMPTS = 8;
constexpr uint8_t MIN_COMICS_FOR_CACHE_ONLY = 10;
constexpr uint32_t ARCHIVE_REFRESH_SECONDS = 6UL * 60UL * 60UL;
constexpr uint8_t ARCHIVE_OLD_COMICS_PER_REFRESH = 10;
constexpr uint32_t ARCHIVE_MAINTENANCE_DEADLINE_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t ARCHIVE_CANCEL_POLL_TIMEOUT_MS = 2000;

// --- Legibility and rendering -----------------------------------------------
// Extremely narrow results can still be illegible on high-resolution panels
// even when little or no downscaling is required.
constexpr int MIN_RENDERED_WIDTH = PANEL_WIDTH / 4;
constexpr float DITHER_GAMMA = 1.0f;

constexpr int CONTENT_MARGIN_X = ui(10);
constexpr int CONTENT_TOP = ui(50);
constexpr int FOOTER_BOTTOM = PANEL_HEIGHT - ui(12);
constexpr int FOOTER_MAX_LINES = 3;
constexpr int FOOTER_LINE_HEIGHT = ui(22);
constexpr int FOOTER_VERTICAL_PADDING = ui(8);

// --- SD-card cache layout ---------------------------------------------------
constexpr char CACHE_DIR[] = "/xkcd";
constexpr char LATEST_CACHE[] = "/xkcd/latest.json";
constexpr char CACHE_INDEX[] = "/xkcd/index.json";
constexpr char CACHE_INDEX_LEGACY_TXT[] = "/xkcd/index.txt";
constexpr uint32_t CACHE_INDEX_VERSION = 3;
constexpr uint32_t MAX_CACHE_INDEX_ENTRIES = 10000;
constexpr char XKCD_LATEST_URL[] = "https://xkcd.com/info.0.json";

}  // namespace config
