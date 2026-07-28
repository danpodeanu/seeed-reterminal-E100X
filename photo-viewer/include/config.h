#pragma once

// User-tweakable configuration for the photo viewer. Everything in this
// header is intended to be edited when setting up a new device or when
// changing personal preferences (refresh cadence, photo directory,
// rotation order, quiet hours).
//
// Hardware, timing budgets, sensor tuning, and dither internals live in
// system_config.h, which is included from the bottom of this file.

#include <Arduino.h>

namespace config {

// --- Refresh cadence --------------------------------------------------------
// Six hours gives an ambient frame four automatic photos per day while
// keeping expensive color e-paper refreshes and radio usage low.
constexpr uint64_t SLEEP_SECONDS = 6ULL * 60ULL * 60ULL;

// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8.
constexpr char TIMEZONE[] = "CST-8";

// --- Photo library ----------------------------------------------------------
// Directory on the SD card that the viewer scans for photos. All supported
// files (prepared 4-bit BMP + fallback JPEG/PNG/BMP) directly under this
// directory are candidates for display.
constexpr char PHOTO_DIR[] = "/photos";

// Photo rotation order. When true, the enumeration is shuffled at each boot
// so successive photos feel random; when false, files are sorted
// alphabetically so rotation order is deterministic across boots.
constexpr bool PHOTO_ORDER_RANDOM = true;

// --- Quiet hours ------------------------------------------------------------
// Automatic refreshes are suppressed overnight. The current photo remains on
// the e-paper panel without any sleep message or overlay. Any user button may
// still wake the frame and change the photo.
constexpr bool QUIET_HOURS_ENABLED = true;
constexpr uint8_t QUIET_START_HOUR = 1;
constexpr uint8_t QUIET_START_MINUTE = 0;
constexpr uint8_t QUIET_END_HOUR = 7;
constexpr uint8_t QUIET_END_MINUTE = 0;
static_assert(QUIET_START_HOUR < 24 && QUIET_END_HOUR < 24,
              "Quiet-hour values must be between 0 and 23");
static_assert(QUIET_START_MINUTE < 60 && QUIET_END_MINUTE < 60,
              "Quiet-minute values must be between 0 and 59");

// --- NTP servers ------------------------------------------------------------
// Primary and secondary NTP servers. The DHCP-advertised server is tried
// first regardless; these are the fall-backs when DHCP does not offer one
// or the DHCP server fails.
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";

// Flip to `true` to tee every serial log line into a rolling file on
// the SD card (/logs/current.log, with the previous boot preserved as
// /logs/previous.log). Off by default -- fsync-per-line adds several
// seconds of SD I/O to each refresh, so only enable while
// troubleshooting a specific misbehaviour.
constexpr bool LOG_TO_SD = false;

}  // namespace config

// System-level constants (hardware model, timeouts, sensor tuning,
// dither internals). Included after the user constants above so it can
// reference them.
#include "system_config.h"
