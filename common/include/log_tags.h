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
inline constexpr char PRECACHE[] = "precache";

// Sensors
inline constexpr char SENSOR[]   = "sensor";

}  // namespace log_tags
