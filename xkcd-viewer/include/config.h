#pragma once

// User-tweakable configuration for the xkcd viewer. Everything in this
// header is intended to be edited when setting up a new device or when
// changing personal preferences (refresh cadence, quiet hours, minimum
// display scale, debug knobs).
//
// Hardware, timing budgets, PSRAM budgets, dither/render tuning, and
// on-flash cache layout live in system_config.h, which is included from
// the bottom of this file.

#include <Arduino.h>

namespace config {

// --- Refresh cadence --------------------------------------------------------
// How long the device sleeps between automatic refreshes. Shorter = fresher
// comics but noticeably more battery drain and more panel wear.
constexpr uint64_t SLEEP_SECONDS = 15ULL * 60ULL;

// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8.
constexpr char TIMEZONE[] = "CST-8";

// --- Quiet hours ------------------------------------------------------------
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

// --- NTP servers ------------------------------------------------------------
// Primary and secondary NTP servers. The DHCP-advertised server is tried
// first regardless; these are the fall-backs when DHCP does not offer one
// or the DHCP server fails.
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";

// --- Legibility floor -------------------------------------------------------
// Preserve the previous server's rule: do not make a comic illegible merely
// to squeeze it onto the panel. Comics that would need to shrink below this
// scale are skipped and another is picked.
constexpr float MIN_DISPLAY_SCALE = 0.65f;

// --- Debug knobs ------------------------------------------------------------
// Show the diagnostic badges in the top-right corner (last-refresh
// timestamp for both apps, plus the cached-comic count for xkcd).
// Handy while iterating on cache/refresh behaviour; turn off for a
// cleaner day-to-day display.
constexpr bool DEBUG_SHOW_STATUS_BADGES = true;

// Debug: when set to a positive comic number, the very next cold-boot
// selection short-circuits random picking and loads that specific comic
// straight from the local cache. Intended for reproducing a render-path
// hang on a known-bad comic; leave at 0 for normal operation.
constexpr int DEBUG_FORCE_COMIC = 0;

// --- Date format ------------------------------------------------------------
// The comic's publication date (year, month, day) is drawn at the bottom-
// right corner of the image area, just above the footer band. Choose which
// order the three components appear in; the separator is always '-' and
// day/month are zero-padded to two digits.
enum class DateLocale {
  DMY,  // 14-03-2025 (European default)
  MDY,  // 03-14-2025 (US)
  YMD,  // 2025-03-14 (ISO / East Asia)
};
constexpr DateLocale DATE_LOCALE = DateLocale::DMY;

// Flip to `true` to tee every serial log line into a rolling file on
// the SD card (/logs/current.log, with the previous boot preserved as
// /logs/previous.log). Off by default -- fsync-per-line adds several
// seconds of SD I/O to each refresh, so only enable while
// troubleshooting a specific misbehaviour.
constexpr bool LOG_TO_SD = true;

}  // namespace config

// System-level constants (hardware model, timeouts, cache layout, image
// budgets, layout math). Included after the user constants above so it
// can reference them.
#include "system_config.h"
