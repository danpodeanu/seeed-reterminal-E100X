#pragma once

// Canonical log tag constants for the three viewer apps. Log lines take the
// form "[tag] message"; keeping the tags in one place prevents typos and
// documents which subsystem may emit them. Callers can either embed the raw
// string literal ("[cache] ..." remains valid) or use string concatenation
// with LOG_TAG_* constants when composing log lines dynamically:
//
//   LOG.printf("[" LOG_TAG_WEATHER "] %s\n", message);
//
// New tags MUST be added here and reviewed against the existing set before
// use so we don't accumulate near-synonyms (e.g. "[net]" vs "[http]").
//
// The Python script `common/tools/check_log_tags.py` scans every firmware
// source file and fails loudly on any tag used in code that is not declared
// below. Run it locally, or wire it into CI, to enforce this contract.

namespace log_tags {

// Boot / lifecycle
inline constexpr char BOOT[]     = "boot";
inline constexpr char WAKE[]     = "wake";
inline constexpr char SLEEP[]    = "sleep";
inline constexpr char QUIET[]    = "quiet";
inline constexpr char RENDER[]   = "render";
inline constexpr char DISPLAY[]  = "display";
inline constexpr char MEM[]      = "mem";

// Time / RTC / NTP
inline constexpr char RTC[]      = "rtc";
inline constexpr char NTP[]      = "ntp";

// Storage / cache
inline constexpr char SD[]       = "sd";
inline constexpr char CACHE[]    = "cache";
inline constexpr char JSON[]     = "json";

// Networking
inline constexpr char WIFI[]     = "wifi";
inline constexpr char HTTP[]     = "http";

// App-specific
inline constexpr char WEATHER[]  = "weather";
inline constexpr char PHOTO[]    = "photo";
inline constexpr char XKCD[]     = "xkcd";
inline constexpr char COMIC[]    = "comic";
inline constexpr char PRECACHE[] = "precache";

// UI / IO
inline constexpr char BUTTON[]     = "button";
inline constexpr char LAYOUT[]     = "layout";
inline constexpr char SCREENSHOT[] = "screenshot";
inline constexpr char DEBUG[]      = "debug";

// Sensors
inline constexpr char SENSOR[]   = "sensor";
inline constexpr char BATTERY[]  = "battery";

// Firmware / OTA
inline constexpr char OTA[]      = "ota";

}  // namespace log_tags
