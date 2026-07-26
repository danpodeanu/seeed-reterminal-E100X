#pragma once

// System-level configuration for the photo viewer. These constants
// describe the reTerminal hardware, timing budgets, sensor tuning, and
// dither internals.
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
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 1000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t NTP_REFRESH_SECONDS = 6UL * 60UL * 60UL;

// --- Sensors and buttons ----------------------------------------------------
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr uint32_t BUTTON_RELEASE_DEBOUNCE_MS = 40;

// --- Photo rotation ---------------------------------------------------------
constexpr uint8_t MAX_PHOTO_ATTEMPTS = 8;

// --- Dither -----------------------------------------------------------------
// Used only for unprepared JPEG/PNG/BMP compatibility. Prepared 4-bit BMPs
// are already panel-dithered by tools/prepare_photos.py and bypass this path.
constexpr float FALLBACK_DITHER_GAMMA = 1.0f;

// Dither algorithm version. Bumped in lockstep with any change to the
// Python quantiser (tools/prepare_photos.py) or the C++ dither library. If
// the manifest on the SD card carries a different value the firmware logs a
// warning; the photos still display, but the user should re-run
// prepare_photos.py to freshen them.
constexpr char DITHER_VERSION[] = "v1";

}  // namespace config
