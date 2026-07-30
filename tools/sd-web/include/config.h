#pragma once

#include <stdint.h>

// User-tweakable configuration for the SD-card Wi-Fi portal. Keep this
// header small on purpose: any deployment tweak that is likely to
// differ between users should live here, everything else should be a
// build-time detail in main.cpp.

namespace config {

// URL that the small third QR code links to. Aim for a page that a
// visitor can read on their phone right after connecting to the AP -
// e.g. the tool's README on GitHub. Kept as a compile-time constant so
// the QR data can be baked into flash.
constexpr char HELP_URL[] =
    "https://github.com/danpodeanu/seeed-reterminal-E100X/tree/main/tools/sd-web";

// Caption printed on the panel above the help QR code. Short - two or
// three words is enough since the QR itself is smaller than the other
// two.
constexpr char HELP_CAPTION[] = "Help";

// --- Time synchronisation ---------------------------------------------------
// POSIX TZ notation uses the opposite sign: CST-8 means UTC+8. Only
// affects the mtime column in the file listing.
constexpr char TIMEZONE[] = "CST-8";

// NTP servers. The tool briefly joins the STA network in secrets.h at
// boot, queries these servers, then tears down the STA session and
// starts its own AP. If both servers fail we fall back to the PCF8563
// RTC (which is battery-backed on the reTerminal).
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";

// Timeouts for the boot-time NTP step. Kept short since the AP is the
// primary function of this tool - if NTP takes too long, we'd rather
// come up with an unsynced clock and get the portal running.
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 1000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 8000;

}  // namespace config
