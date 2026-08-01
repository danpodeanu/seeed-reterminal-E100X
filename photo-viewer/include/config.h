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
// One hour gives an ambient frame 24 automatic photos per day. E-paper
// refresh cost is modest on this cadence and the frame feels alive.
constexpr uint64_t SLEEP_SECONDS = 60ULL * 60ULL;

// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8.
// London (GMT/BST) with EU-style DST rules is the default.
constexpr char TIMEZONE[] = "GMT0BST,M3.5.0/1,M10.5.0/2";

// --- Photo library ----------------------------------------------------------
// Directory on the SD card that the viewer scans for photos. All supported
// files (prepared 4-bit BMP + ordinary PNG) directly under this directory
// are candidates for display. JPEG is intentionally not scanned here; use
// the browser uploader at /upload-photo for JPEG so it transcodes into the
// prepared 4-bit BMP layout without blowing the on-device RGB buffer.
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

// --- SD Wi-Fi portal --------------------------------------------------------
// When either arrow button is pressed the viewer flips into an SD-card
// Wi-Fi portal mode: it stops advancing photos, brings up an open Wi-Fi
// AP with these settings, and serves a browser-based file manager for
// the /photos directory on the SD card (see tools/sd-web). Pressing
// either arrow again restarts the device back into photo mode.
constexpr char PORTAL_SSID_PREFIX[] = "ReTerminal ";
constexpr char* PORTAL_PASSWORD = nullptr;   // nullptr = open network
constexpr uint16_t PORTAL_HTTP_PORT = 80;
constexpr uint8_t PORTAL_MAX_CONNECTIONS = 4;
// URL that the small third QR code links to (README on GitHub).
constexpr char PORTAL_HELP_URL[] =
    "https://github.com/danpodeanu/seeed-reterminal-E100X/tree/main/tools/sd-web";
constexpr char PORTAL_HELP_CAPTION[] = "Help";

}  // namespace config

// System-level constants (hardware model, timeouts, sensor tuning,
// dither internals). Included after the user constants above so it can
// reference them.
#include "system_config.h"
