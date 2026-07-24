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
// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8.
constexpr char TIMEZONE[] = "CST-8";
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 4000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t NTP_REFRESH_SECONDS = 24UL * 60UL * 60UL;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 10000;
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr uint32_t SCREENSHOT_LONG_PRESS_MS = 1500;
constexpr uint32_t BUTTON_RELEASE_DEBOUNCE_MS = 40;
constexpr size_t MAX_IMAGE_BYTES = 6U * 1024U * 1024U;
constexpr size_t MAX_LIVE_IMAGE_BYTES = 2U * 1024U * 1024U;
constexpr uint8_t MAX_COMIC_ATTEMPTS = 8;
constexpr uint8_t MIN_COMICS_FOR_CACHE_ONLY = 10;
constexpr uint32_t ARCHIVE_REFRESH_SECONDS = 6UL * 60UL * 60UL;
constexpr uint8_t ARCHIVE_OLD_COMICS_PER_REFRESH = 10;
constexpr uint32_t ARCHIVE_MAINTENANCE_DEADLINE_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t ARCHIVE_CANCEL_POLL_TIMEOUT_MS = 2000;

// Suppress automatic refreshes overnight. A cold boot or any front-button
// wake still refreshes immediately, then sleeps until the configured end.
constexpr bool QUIET_HOURS_ENABLED = true;
constexpr uint8_t QUIET_START_HOUR = 1;
constexpr uint8_t QUIET_START_MINUTE = 0;
constexpr uint8_t QUIET_END_HOUR = 7;
constexpr uint8_t QUIET_END_MINUTE = 0;
static_assert(QUIET_START_HOUR < 24 && QUIET_END_HOUR < 24,
              "Quiet-hour values must be between 0 and 23");
static_assert(QUIET_START_MINUTE < 60 && QUIET_END_MINUTE < 60,
              "Quiet-minute values must be between 0 and 59");

// Preserve the previous server's rule: do not make a comic illegible merely
// to squeeze it onto the panel.
constexpr float MIN_DISPLAY_SCALE = 0.65f;
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

constexpr char CACHE_DIR[] = "/xkcd";
constexpr char LATEST_CACHE[] = "/xkcd/latest.json";
constexpr char XKCD_LATEST_URL[] = "https://xkcd.com/info.0.json";

}  // namespace config
