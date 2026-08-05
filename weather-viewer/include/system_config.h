#pragma once

// System-level configuration for the weather viewer. These constants
// describe the reTerminal hardware, timing budgets, on-flash cache paths,
// and internal implementation details.
//
// This file is included from the bottom of config.h and relies on the
// user constants declared there (e.g. SLEEP_SECONDS). Do not include it
// directly -- always include config.h instead.

#include <Arduino.h>

// --- Hardware model (build-time) --------------------------------------------
#ifndef RETERMINAL_MODEL
#define RETERMINAL_MODEL 1001
#endif

#include "panel_traits.h"

namespace config {

constexpr int MODEL = panel_traits::MODEL;
constexpr int PANEL_WIDTH = panel_traits::WIDTH;
constexpr int PANEL_HEIGHT = panel_traits::HEIGHT;

// Convert a coordinate authored for the E1001 panel (800x480) into the
// equivalent number of pixels on the active panel.
constexpr int ui(int e1001Pixels) {
  return panel_traits::scaleUi(e1001Pixels);
}

// --- Cache freshness --------------------------------------------------------
// How stale a cached forecast may be before we ignore it and either fetch
// live or report "no data". Semantically distinct from SLEEP_SECONDS -- they
// happen to coincide today because we refresh once per sleep cycle, but a
// change to one should not silently change the other.
constexpr uint64_t CACHE_MAX_AGE_SECONDS = SLEEP_SECONDS;
// When a live weather fetch fails, keep displaying the last saved forecast
// for up to this long instead of showing the "weather unavailable" screen.
constexpr uint64_t FAILURE_CACHE_MAX_AGE_SECONDS = 60ULL * 60ULL;
// When live weather is unavailable and no acceptable cache exists, retry
// automatically after this interval instead of waiting for a button press.
constexpr uint64_t FAILURE_RETRY_SECONDS = 15ULL * 60ULL;

// --- Timeouts ---------------------------------------------------------------
constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t HTTP_TIMEOUT_MS = 25000;
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 1000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t NTP_REFRESH_SECONDS = 6UL * 60UL * 60UL;

// --- Sensors and buttons ----------------------------------------------------
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr uint32_t SCREENSHOT_LONG_PRESS_MS = 1500;
constexpr uint32_t BUTTON_RELEASE_DEBOUNCE_MS = 40;

// --- SD-card cache layout ---------------------------------------------------
constexpr char CACHE_DIR[] = "/weather";
constexpr char FORECAST_CACHE[] = "/weather/forecast.json";

}  // namespace config
