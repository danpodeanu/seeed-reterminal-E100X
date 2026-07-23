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

// Edit these values for the forecast location.
constexpr char LOCATION_NAME[] = "Suzhou";
constexpr double LATITUDE = 31.31361;
constexpr double LONGITUDE = 120.69167;

constexpr uint8_t FORECAST_DAYS = 3;
constexpr uint8_t RAIN_FORECAST_HOURS = 48;
constexpr float RAIN_START_THRESHOLD_MM = 0.1f;
constexpr uint8_t RAIN_PROBABILITY_THRESHOLD = 30;
constexpr uint64_t SLEEP_SECONDS = 30ULL * 60ULL;
constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t HTTP_TIMEOUT_MS = 25000;
// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8.
constexpr char TIMEZONE[] = "CST-8";
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr uint32_t SCREENSHOT_LONG_PRESS_MS = 1500;
constexpr uint32_t BUTTON_RELEASE_DEBOUNCE_MS = 40;
constexpr char CACHE_DIR[] = "/weather";
constexpr char FORECAST_CACHE[] = "/weather/forecast.json";

}  // namespace config
