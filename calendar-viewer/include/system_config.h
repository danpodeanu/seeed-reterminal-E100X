#pragma once

#include <stdint.h>

#ifndef RETERMINAL_MODEL
#define RETERMINAL_MODEL 1001
#endif

#if RETERMINAL_MODEL < 1001 || RETERMINAL_MODEL > 1004
#error "calendar-viewer supports reTerminal E1001, E1002, E1003, and E1004"
#endif

#include "panel_traits.h"

namespace config {

constexpr int MODEL = panel_traits::MODEL;
constexpr int PANEL_WIDTH = panel_traits::WIDTH;
constexpr int PANEL_HEIGHT = panel_traits::HEIGHT;

constexpr int ui(int e1001Pixels) {
  return panel_traits::scaleUi(e1001Pixels);
}

constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t HTTP_TIMEOUT_MS = 25000;
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 6000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 10000;
constexpr uint32_t NTP_REFRESH_SECONDS = 6UL * 60UL * 60UL;
constexpr uint8_t SENSOR_READ_ATTEMPTS = 4;
constexpr uint32_t SENSOR_RETRY_DELAY_MS = 75;
constexpr uint64_t FAILURE_RETRY_SECONDS = 5ULL * 60ULL;
constexpr uint64_t WEATHER_CACHE_MAX_AGE_SECONDS = 6ULL * 60ULL * 60ULL;
constexpr size_t MAX_ICAL_BYTES = 512U * 1024U;
constexpr size_t MAX_CALENDAR_EVENTS = 128;
constexpr size_t MAX_GOOGLE_CALENDARS = 12;
constexpr size_t MAX_GOOGLE_CREDENTIAL_BYTES = 8192;

}  // namespace config
