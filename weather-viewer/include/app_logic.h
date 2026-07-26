#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_logic_core.h"
#include "secrets_normalise.h"

namespace app_logic {

// Decode one hex character to its 0..15 value, or -1 if not a hex digit.
constexpr int hexNibble(char c) {
  return secrets_normalise::hexNibble(c);
}

// The hex-digit normaliser lives in common/include/secrets_normalise.h so
// it can be shared with any future app that consumes a hex-encoded secret,
// and so common/tools/secrets_normalise.py can enforce firmware/tester
// parity from a single source of truth. app_logic keeps the historical
// name available as a thin re-export -- callers and tests need not care
// where the implementation lives.
using secrets_normalise::normalizeHexDigits;

// Decode a hexadecimal string (2 chars per byte). Returns the number of
// bytes written on success, or 0 if the input is malformed (odd length,
// non-hex characters, buffer too small). The buffer is only mutated when
// the input is well-formed, so a failed call leaves the buffer alone.
inline size_t decodeHex(const char* text, size_t textLength, uint8_t* out,
                        size_t outCapacity) {
  if (text == nullptr || out == nullptr) return 0;
  if ((textLength % 2u) != 0u) return 0;
  const size_t byteCount = textLength / 2u;
  if (byteCount > outCapacity) return 0;
  for (size_t i = 0; i < byteCount; ++i) {
    const int hi = hexNibble(text[2 * i]);
    const int lo = hexNibble(text[2 * i + 1]);
    if (hi < 0 || lo < 0) return 0;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return byteCount;
}

// Encode `length` bytes with the URL-safe base64 alphabet and no padding
// (as required by JWT / RFC 7515). Writes at most `outCapacity` characters
// plus a NUL terminator to `out` and returns the number of characters
// written excluding the NUL, or 0 if the output would not fit.
inline size_t encodeBase64Url(const uint8_t* in, size_t length, char* out,
                              size_t outCapacity) {
  if (out == nullptr || outCapacity == 0) return 0;
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  const size_t fullTriples = length / 3u;
  const size_t remainder = length - (fullTriples * 3u);
  size_t outLen = fullTriples * 4u;
  if (remainder == 1u) outLen += 2u;
  else if (remainder == 2u) outLen += 3u;
  if (outLen + 1u > outCapacity) return 0;
  size_t o = 0;
  for (size_t i = 0; i < fullTriples; ++i) {
    const uint8_t a = in[3 * i];
    const uint8_t b = in[3 * i + 1];
    const uint8_t c = in[3 * i + 2];
    out[o++] = kAlphabet[a >> 2];
    out[o++] = kAlphabet[((a & 0x03u) << 4) | (b >> 4)];
    out[o++] = kAlphabet[((b & 0x0Fu) << 2) | (c >> 6)];
    out[o++] = kAlphabet[c & 0x3Fu];
  }
  if (remainder == 1u) {
    const uint8_t a = in[fullTriples * 3u];
    out[o++] = kAlphabet[a >> 2];
    out[o++] = kAlphabet[(a & 0x03u) << 4];
  } else if (remainder == 2u) {
    const uint8_t a = in[fullTriples * 3u];
    const uint8_t b = in[fullTriples * 3u + 1u];
    out[o++] = kAlphabet[a >> 2];
    out[o++] = kAlphabet[((a & 0x03u) << 4) | (b >> 4)];
    out[o++] = kAlphabet[(b & 0x0Fu) << 2];
  }
  out[o] = '\0';
  return o;
}

// A QWeather-friendly JWT lifetime clamp: RFC 7519 permits any exp/iat
// pair, but QWeather rejects tokens whose lifetime exceeds 24 hours and
// tokens whose iat is more than a few seconds in the future. Clamp the
// requested lifetime to [60 seconds, 24 hours] and back-date iat by
// backdateSeconds to tolerate small clock skew.
constexpr int64_t clampJwtLifetime(int64_t requestedSeconds) {
  constexpr int64_t kMin = 60;
  constexpr int64_t kMax = 24 * 60 * 60;
  if (requestedSeconds < kMin) return kMin;
  if (requestedSeconds > kMax) return kMax;
  return requestedSeconds;
}

constexpr bool cachedDataFresh(bool clockValid, int64_t now,
                               int64_t dataTimestamp,
                               uint64_t maximumAgeSeconds) {
  return clockValid && dataTimestamp > 0 && now >= dataTimestamp &&
         static_cast<uint64_t>(now - dataTimestamp) <= maximumAgeSeconds;
}

constexpr int64_t roundedAgeMinutes(int64_t now, int64_t dataTimestamp) {
  if (dataTimestamp <= 0 || now < dataTimestamp) return -1;
  constexpr int64_t FIVE_MINUTES_SECONDS = 5 * 60;
  constexpr int64_t HALF_FIVE_MINUTES_SECONDS = FIVE_MINUTES_SECONDS / 2;
  return ((now - dataTimestamp + HALF_FIVE_MINUTES_SECONDS) /
          FIVE_MINUTES_SECONDS) *
         5;
}

constexpr bool suppressForQuietHours(bool coldBoot, bool buttonWake,
                                     bool ntpDue, bool clockValid,
                                     bool quietActive) {
  return !coldBoot && !buttonWake && !ntpDue && clockValid && quietActive;
}

// A precipitation slot ("this hour will have rain") counts as an actual
// rain event only when the recorded liquid-water amount clears the minimum
// threshold AND the forecast probability clears its minimum. A negative
// probability means the field was missing from the response — treat that
// as passing so slots without a `pop` value are not silently skipped.
constexpr bool rainSlotQualifies(float liquidMm, int probabilityPct,
                                 float minLiquidMm,
                                 int minProbabilityPct) {
  return liquidMm >= minLiquidMm &&
         (probabilityPct < 0 || probabilityPct >= minProbabilityPct);
}

// QWeather returns an integer "code" as a decimal string. "200" means the
// response is usable; anything else (including a missing / null code) is a
// failure that must not be parsed as data.
constexpr bool qweatherResponseOk(const char* code) {
  return code != nullptr && code[0] == '2' && code[1] == '0' &&
         code[2] == '0' && code[3] == '\0';
}

// Rank a QWeather warning severity string so we can pick the "worst" active
// alert in a list. Higher is more severe; unknown / null / empty severities
// map to 0 so they never win over a labelled entry.
inline int qweatherAlertSeverityRank(const char* severity) {
  if (severity == nullptr || severity[0] == '\0') return 0;
  if (strcmp(severity, "Extreme") == 0) return 4;
  if (strcmp(severity, "Severe") == 0) return 3;
  if (strcmp(severity, "Moderate") == 0) return 2;
  if (strcmp(severity, "Minor") == 0) return 1;
  return 0;
}

// Map a QWeather icon code (see https://dev.qweather.com/docs/resource/icons/)
// to the WMO-style weather code produced by Open-Meteo, so downstream code
// (conditionName / icon selection) can stay provider-agnostic. Unknown or
// out-of-range codes return -1 so callers can flag invalid data.
constexpr int qweatherIconToWmoCode(int icon) {
  // 1xx: cloud cover (100-104 daytime, 150-154 nighttime variants).
  if (icon == 100 || icon == 150) return 0;   // clear
  if (icon == 101 || icon == 151) return 1;   // mostly clear
  if (icon == 102 || icon == 152) return 2;   // partly cloudy
  if (icon == 103 || icon == 153) return 2;   // more clouds
  if (icon == 104 || icon == 154) return 3;   // overcast

  // 3xx: rain / thunderstorms.
  if (icon == 300 || icon == 301 || icon == 350 || icon == 351) return 80;
  if (icon == 302 || icon == 303) return 95;   // thunderstorm
  if (icon == 304) return 96;                  // thunderstorm with hail
  if (icon == 305 || icon == 309) return 51;   // light rain / drizzle
  if (icon == 306 || icon == 314) return 61;   // moderate rain
  if (icon == 307 || icon == 315) return 63;   // heavy rain
  if (icon == 308 || icon == 310 || icon == 311 ||
      icon == 312)
    return 65;                                 // very heavy rain / storm
  if (icon == 313) return 66;                  // freezing rain
  if (icon == 316 || icon == 317) return 81;   // moderate showers
  if (icon == 318) return 82;                  // violent showers
  if (icon == 399) return 63;                  // rain (unspecified)

  // 4xx: snow.
  if (icon == 400 || icon == 408 || icon == 457) return 71;  // light snow
  if (icon == 401 || icon == 409) return 73;                 // moderate snow
  if (icon == 402 || icon == 403 || icon == 410) return 75;  // heavy / blizzard
  if (icon == 404 || icon == 405 || icon == 456) return 66;  // sleet / rain-snow
  if (icon == 406 || icon == 407) return 85;                 // shower snow
  if (icon == 499) return 73;                                // snow (unspecified)

  // 5xx: fog / haze / dust.
  if (icon == 500 || icon == 501 || icon == 509 || icon == 510 ||
      icon == 514)
    return 45;                                 // mist / fog
  if (icon == 502 || icon == 511 || icon == 512 || icon == 513)
    return 48;                                 // haze
  if (icon >= 503 && icon <= 508) return 48;   // dust / sand (closest bucket)

  return -1;
}

// QWeather cloud-cover icons in [150, 200) mark night-time variants (150
// night clear, 151 few clouds night, etc.). Everything else is treated as a
// daytime icon since rain/snow/fog codes are not day/night paired.
constexpr bool qweatherIconIsNight(int icon) {
  return icon >= 150 && icon < 200;
}

// When a live fetch fails, should the caller render the last-known forecast
// instead of the "weather unavailable" error screen? The cache is acceptable
// only when the clock is valid (so its recorded timestamp can be trusted)
// and the recorded age is within the failure window (typically 1 hour).
constexpr bool useCachedForecastOnFailure(bool liveFetchSucceeded,
                                          bool clockValid,
                                          bool cacheAvailable,
                                          bool cacheWithinFailureWindow) {
  return !liveFetchSucceeded && clockValid && cacheAvailable &&
         cacheWithinFailureWindow;
}

// Map an Open-Meteo WMO weather code (or a QWeather icon that has already been
// funnelled through qweatherIconToWmoCode) to the short English label the UI
// prints under each forecast panel. Unknown codes fall back to
// "Mixed weather" so the display always shows something.
inline const char* conditionName(int wmoCode) {
  if (wmoCode == 0) return "Clear";
  if (wmoCode == 1 || wmoCode == 2) return "Partly cloudy";
  if (wmoCode == 3) return "Overcast";
  if (wmoCode == 45 || wmoCode == 48) return "Fog";
  if (wmoCode >= 51 && wmoCode <= 57) return "Drizzle";
  if ((wmoCode >= 61 && wmoCode <= 67) ||
      (wmoCode >= 80 && wmoCode <= 82))
    return "Rain";
  if ((wmoCode >= 71 && wmoCode <= 77) ||
      (wmoCode >= 85 && wmoCode <= 86))
    return "Snow";
  if (wmoCode >= 95) return "Thunderstorm";
  return "Mixed weather";
}

// Compute the deflate-stream offset and length inside a gzip byte buffer,
// without actually inflating anything. The gzip framing (RFC 1952) is:
//   0-1  : magic 0x1f 0x8b
//   2    : compression method (must be 8 = deflate)
//   3    : flags (FTEXT/FHCRC/FEXTRA/FNAME/FCOMMENT)
//   4-9  : mtime + extra flags + os
//   10-  : optional FEXTRA (2-byte length + N bytes)
//   ...  : optional FNAME (NUL-terminated)
//   ...  : optional FCOMMENT (NUL-terminated)
//   ...  : optional FHCRC (2 bytes)
//   ...  : raw deflate stream (this is what we want)
//   -8   : CRC32
//   -4   : ISIZE (uncompressed length mod 2^32)
//
// On success writes the deflate stream's byte offset to *outStart and its
// length to *outLength, and returns true. Returns false when the buffer is
// too small, the magic bytes are wrong, the method is not deflate, or the
// declared header extensions run past the buffer.
inline bool gzipDeflateSpan(const uint8_t* in, size_t inLen,
                            size_t* outStart, size_t* outLength) {
  if (in == nullptr || outStart == nullptr || outLength == nullptr) {
    return false;
  }
  if (inLen < 18u) return false;  // 10-byte header + 8-byte trailer minimum
  if (in[0] != 0x1fu || in[1] != 0x8bu) return false;
  if (in[2] != 0x08u) return false;
  const uint8_t flags = in[3];
  size_t offset = 10u;
  if (flags & 0x04u) {
    if (offset + 2u > inLen) return false;
    const size_t xlen = static_cast<size_t>(in[offset]) |
                        (static_cast<size_t>(in[offset + 1]) << 8);
    offset += 2u + xlen;
    if (offset > inLen) return false;
  }
  if (flags & 0x08u) {
    while (offset < inLen && in[offset] != 0) ++offset;
    if (offset >= inLen) return false;
    ++offset;
  }
  if (flags & 0x10u) {
    while (offset < inLen && in[offset] != 0) ++offset;
    if (offset >= inLen) return false;
    ++offset;
  }
  if (flags & 0x02u) offset += 2u;
  if (offset + 8u > inLen) return false;
  *outStart = offset;
  *outLength = inLen - offset - 8u;
  return true;
}

}  // namespace app_logic
