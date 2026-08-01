#include <Arduino.h>
#include <ArduinoJson.h>
#include <Ed25519.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <miniz.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_logger.h"
#include "app_logic.h"
#include "config.h"
#include "local_time.h"
#include "secrets.h"
#include "weather_config_runtime.h"
#include "weather_data.h"
#include "weather_provider.h"

// The QWeather secrets baked into secrets.h are the boot-time fallback
// path -- weather_config::runtime::qweatherHost() etc. return them when
// NVS has no override. Provide the same #ifndef safety net for any
// secrets.h that omits a field so the firmware still compiles.
#ifndef QWEATHER_API_HOST
#define QWEATHER_API_HOST "devapi.qweather.com"
#endif
#ifndef QWEATHER_PROJECT_ID
#define QWEATHER_PROJECT_ID ""
#endif
#ifndef QWEATHER_CREDENTIAL_ID
#define QWEATHER_CREDENTIAL_ID ""
#endif
#ifndef QWEATHER_PRIVATE_KEY_HEX
#define QWEATHER_PRIVATE_KEY_HEX ""
#endif

namespace weather_provider {

namespace {

String endpointUrl(const char* path) {
  String url = "https://";
  url += weather_config::runtime::qweatherHost();
  url += path;
  // QWeather accepts either a LocationID or "longitude,latitude"
  // (longitude first, comma-separated, at most two decimals). Reuse the
  // single runtime latitude / longitude that Open-Meteo also uses so both
  // providers refer to one canonical location.
  url += "?location=";
  url += String(weather_config::runtime::longitude(), 2);
  url += ",";
  url += String(weather_config::runtime::latitude(), 2);
  url += "&unit=m";
  url += "&lang=";
  url += weather_config::runtime::qweatherLang();
  return url;
}

// QWeather's paid hosts return gzip-compressed bodies regardless of the
// client's Accept-Encoding header (verified against pb4nmvpcrc.re.qweatherapi
// .com; also observed with error responses). Detect the gzip magic and
// inflate into a fresh String via miniz. Returns true when `body` was
// already plain text or was successfully decompressed in place.
bool decompressGzipIfNeeded(String& body) {
  const size_t inLen = body.length();
  const uint8_t* in = reinterpret_cast<const uint8_t*>(body.c_str());
  size_t deflateStart = 0;
  size_t deflateLen = 0;
  if (!app_logic::gzipDeflateSpan(in, inLen, &deflateStart, &deflateLen)) {
    // Not a valid gzip stream. If the magic bytes are absent the body is
    // already plain text and we should let the caller keep going. If the
    // magic bytes are there but framing is corrupt, fail loudly.
    if (inLen >= 2 && in[0] == 0x1fu && in[1] == 0x8bu) {
      LOG.println("[weather] gzip header present but framing is invalid");
      return false;
    }
    return true;
  }
  const uint32_t isize =
      static_cast<uint32_t>(in[inLen - 4]) |
      (static_cast<uint32_t>(in[inLen - 3]) << 8) |
      (static_cast<uint32_t>(in[inLen - 2]) << 16) |
      (static_cast<uint32_t>(in[inLen - 1]) << 24);
  // ISIZE is mod 2^32; cap the allocation so a bogus header can't blow up
  // the heap. Weather envelopes are well under 256 KB in practice.
  size_t outCapacity = isize;
  constexpr size_t kMaxInflatedBytes = 256u * 1024u;
  if (outCapacity == 0 || outCapacity > kMaxInflatedBytes) {
    outCapacity = kMaxInflatedBytes;
  }
  uint8_t* out = static_cast<uint8_t*>(
      heap_caps_malloc(outCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (out == nullptr) out = static_cast<uint8_t*>(malloc(outCapacity));
  if (out == nullptr) {
    LOG.printf("[weather] could not allocate %u bytes for gzip inflate\n",
               static_cast<unsigned>(outCapacity));
    return false;
  }
  const size_t written = tinfl_decompress_mem_to_mem(
      out, outCapacity, in + deflateStart, deflateLen, 0);
  if (written == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
    free(out);
    LOG.println("[weather] gzip inflate failed");
    return false;
  }
  body = "";
  body.reserve(written + 1);
  body.concat(reinterpret_cast<const char*>(out), written);
  free(out);
  LOG.printf("[weather] gzip inflated %u -> %u bytes\n",
             static_cast<unsigned>(inLen), static_cast<unsigned>(written));
  return true;
}

// QWeather timestamps arrive in "YYYY-MM-DDTHH:MM+HH:MM" form. The
// offset is the observation location's, not the device's. Parse it as
// UTC epoch via local_time::parseIso8601Utc. Returns 0 on failure so
// downstream code can use `!= 0` as "have this timestamp".
static time_t parseQweatherTimestamp(const char* raw) {
  time_t out = 0;
  if (raw == nullptr || raw[0] == '\0') return 0;
  return local_time::parseIso8601Utc(raw, out) ? out : 0;
}

float parseFloat(const char* text) {
  if (text == nullptr || *text == '\0') return NAN;
  char* end = nullptr;
  const float value = strtof(text, &end);
  if (end == text) return NAN;
  return value;
}

int parseInt(const char* text, int fallback) {
  if (text == nullptr || *text == '\0') return fallback;
  char* end = nullptr;
  const long value = strtol(text, &end, 10);
  if (end == text) return fallback;
  return static_cast<int>(value);
}

// Build a QWeather JWT (EdDSA / Ed25519) suitable for the Authorization
// Bearer header. Reads QWEATHER_CREDENTIAL_ID, QWEATHER_PROJECT_ID, and
// QWEATHER_PRIVATE_KEY_HEX from the compilation environment.
bool buildJwt(String& jwt, String& failureReason) {
  jwt = "";
  const char* kid = weather_config::runtime::qweatherCredentialId();
  const char* sub = weather_config::runtime::qweatherProjectId();
  const char* privateKeyHex = weather_config::runtime::qweatherPrivateKeyHex();
  if (kid[0] == '\0' || sub[0] == '\0' || privateKeyHex[0] == '\0') {
    failureReason = "QWeather credentials are not configured";
    LOG.println(
        "[weather] QWeather selected but PROJECT_ID / CREDENTIAL_ID / "
        "PRIVATE_KEY_HEX is empty");
    return false;
  }
  uint8_t privateKey[32];
  // Users often paste the openssl `priv:` output with ':' separators or
  // whitespace from the PEM dump; normalise before decoding so the firmware
  // accepts the same inputs the test_credentials.py tester accepts.
  char cleanedHex[128];
  const size_t cleanedLen = app_logic::normalizeHexDigits(
      privateKeyHex, cleanedHex, sizeof(cleanedHex));
  if (cleanedLen == static_cast<size_t>(-1)) {
    failureReason = "QWeather private key contains non-hex characters";
    LOG.println(
        "[weather] QWeather private key contains characters that are "
        "not hex digits, whitespace, or ':'");
    return false;
  }
  const size_t decoded = app_logic::decodeHex(
      cleanedHex, cleanedLen, privateKey, sizeof(privateKey));
  if (decoded != sizeof(privateKey)) {
    failureReason = "QWeather private key is not 32 bytes of hex";
    LOG.printf(
        "[weather] QWeather private key is not 64 hex chars "
        "(cleaned length=%u)\n", static_cast<unsigned>(cleanedLen));
    return false;
  }

  const time_t now = time(nullptr);
  if (now < 1700000000) {
    failureReason = "Clock is not set; cannot sign JWT";
    LOG.println("[weather] JWT would use a clock that is not NTP-synced");
    return false;
  }
  const int64_t lifetime = app_logic::clampJwtLifetime(2 * 60 * 60);  // 2 h
  // Back-date iat by 5 minutes so a device clock that has drifted ahead of
  // QWeather's server (empirically the tighter side of their acceptance
  // window: even +30s of "iat in the future" gets rejected) still signs a
  // valid token. Combined with the 2h exp this tolerates roughly -2h to
  // +5min of drift relative to real time.
  const int64_t iat = static_cast<int64_t>(now) - 300;
  const int64_t exp = iat + lifetime;

  String headerJson;
  headerJson.reserve(64);
  headerJson = "{\"alg\":\"EdDSA\",\"kid\":\"";
  headerJson += kid;
  headerJson += "\"}";
  String payloadJson;
  payloadJson.reserve(80);
  payloadJson = "{\"sub\":\"";
  payloadJson += sub;
  payloadJson += "\",\"iat\":";
  payloadJson += String(static_cast<long long>(iat));
  payloadJson += ",\"exp\":";
  payloadJson += String(static_cast<long long>(exp));
  payloadJson += "}";

  char encodedHeader[128] = {};
  char encodedPayload[128] = {};
  const size_t headerLen = app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>(headerJson.c_str()),
      headerJson.length(), encodedHeader, sizeof(encodedHeader));
  const size_t payloadLen = app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>(payloadJson.c_str()),
      payloadJson.length(), encodedPayload, sizeof(encodedPayload));
  if (headerLen == 0 || payloadLen == 0) {
    failureReason = "JWT encoding failed";
    LOG.println("[weather] base64url encoding overflowed a JWT buffer");
    return false;
  }

  String signingInput;
  signingInput.reserve(headerLen + 1 + payloadLen);
  signingInput = encodedHeader;
  signingInput += ".";
  signingInput += encodedPayload;

  uint8_t publicKey[32];
  Ed25519::derivePublicKey(publicKey, privateKey);
  uint8_t signature[64];
  Ed25519::sign(signature, privateKey, publicKey,
                reinterpret_cast<const uint8_t*>(signingInput.c_str()),
                signingInput.length());

  char encodedSignature[128] = {};
  const size_t sigLen = app_logic::encodeBase64Url(
      signature, sizeof(signature), encodedSignature,
      sizeof(encodedSignature));
  if (sigLen == 0) {
    failureReason = "JWT signature encoding failed";
    return false;
  }

  jwt.reserve(signingInput.length() + 1 + sigLen);
  jwt = signingInput;
  jwt += ".";
  jwt += encodedSignature;
  if (config::DEBUG_LOG_JWT) {
    LOG.printf("[debug] JWT iat=%lld exp=%lld\n",
               static_cast<long long>(iat), static_cast<long long>(exp));
    LOG.printf("[debug] JWT header=%s\n", encodedHeader);
    LOG.printf("[debug] JWT payload=%s\n", encodedPayload);
    LOG.printf("[debug] JWT signature=%s\n", encodedSignature);
    LOG.printf("[debug] JWT full=%s\n", jwt.c_str());
  }
  return true;
}

// Perform one QWeather HTTP GET and return the raw response body. The
// HTTPClient is reset per call because reusing it across TLS hosts can leak
// state; three sequential calls fit comfortably in the timeout budget.
bool fetchEndpoint(const String& url, const String& bearerToken, String& body,
                   String& failureReason, bool bypassHttpCache) {
  body = "";
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    LOG.println("[weather] could not start QWeather request");
    failureReason = "Could not start weather request";
    return false;
  }
  LOG.printf("[weather] QWeather GET %s\n", url.c_str());
  // Register the response headers we want to read after GET() -- otherwise
  // HTTPClient::header() returns "" even when the server sent the header.
  static const char* kResponseHeaders[] = {"Content-Encoding"};
  http.collectHeaders(kResponseHeaders,
                      sizeof(kResponseHeaders) / sizeof(kResponseHeaders[0]));
  http.addHeader("Authorization", "Bearer " + bearerToken);
  http.addHeader("Accept-Encoding", "gzip, identity");
  if (bypassHttpCache) {
    http.addHeader("Cache-Control", "no-cache, no-store");
    http.addHeader("Pragma", "no-cache");
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[weather] QWeather HTTP GET -> %d\n", status);
    http.end();
    failureReason = "Weather service returned an error";
    return false;
  }
  constexpr int kMaxResponseBytes = 128 * 1024;
  const int declaredSize = http.getSize();
  if (declaredSize > kMaxResponseBytes) {
    LOG.printf("[weather] response too large: %d bytes\n", declaredSize);
    http.end();
    failureReason = "Weather response is too large";
    return false;
  }
  body = http.getString();
  const String contentEncoding = http.header("Content-Encoding");
  http.end();
  if (static_cast<int>(body.length()) > kMaxResponseBytes) {
    LOG.printf("[weather] streamed response too large: %u bytes\n",
               static_cast<unsigned>(body.length()));
    body = "";
    failureReason = "Weather response is too large";
    return false;
  }
  // QWeather's paid hosts always gzip regardless of the request headers,
  // and even the free host may compress on some accounts, so decode based
  // on the actual response header (or the gzip magic bytes) rather than
  // trusting what we asked for.
  if (!decompressGzipIfNeeded(body)) {
    body = "";
    failureReason = "Weather response could not be decompressed";
    return false;
  }
  if (contentEncoding.length() > 0 &&
      !contentEncoding.equalsIgnoreCase("identity") &&
      !contentEncoding.equalsIgnoreCase("gzip")) {
    LOG.printf("[weather] unexpected Content-Encoding: %s\n",
               contentEncoding.c_str());
  }
  return true;
}

}  // namespace

bool parseQWeather(const String& body, WeatherData& weather) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    LOG.printf("[weather] JSON: %s\n", error.c_str());
    return false;
  }
  JsonObject nowEnv = document["now_env"];
  JsonObject dailyEnv = document["daily_env"];
  JsonObject hourlyEnv = document["hourly_env"];
  if (nowEnv.isNull() || dailyEnv.isNull() || hourlyEnv.isNull()) {
    LOG.println("[weather] QWeather envelope missing");
    return false;
  }
  const char* nowCode = nowEnv["code"] | "";
  const char* dailyCode = dailyEnv["code"] | "";
  const char* hourlyCode = hourlyEnv["code"] | "";
  if (!app_logic::qweatherResponseOk(nowCode) ||
      !app_logic::qweatherResponseOk(dailyCode) ||
      !app_logic::qweatherResponseOk(hourlyCode)) {
    LOG.printf("[weather] QWeather codes now=%s daily=%s hourly=%s\n",
               nowCode, dailyCode, hourlyCode);
    return false;
  }

  JsonObject now = nowEnv["now"];
  JsonArray daily = dailyEnv["daily"];
  JsonArray hourly = hourlyEnv["hourly"];
  if (now.isNull() || daily.size() < config::FORECAST_DAYS) {
    LOG.println("[weather] QWeather payload missing fields");
    return false;
  }

  weather.temperatureC = parseFloat(now["temp"] | "");
  weather.apparentC = parseFloat(now["feelsLike"] | "");
  weather.humidityPct = parseFloat(now["humidity"] | "");
  weather.windKmh = parseFloat(now["windSpeed"] | "");
  const int nowIcon = parseInt(now["icon"] | "", -1);
  weather.weatherCode = app_logic::qweatherIconToWmoCode(nowIcon);
  weather.isDay = !app_logic::qweatherIconIsNight(nowIcon);
  weather.updateTime = parseQweatherTimestamp(now["obsTime"] | "");
  weather.rainTimingAvailable = false;
  weather.rainExpected = false;
  weather.nextRainTime = 0;
  weather.nextRainMm = NAN;
  weather.nextRainProbability = -1;

  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    JsonObject day = daily[i];
    weather.days[i].date = String(day["fxDate"] | "");
    const int iconDay = parseInt(day["iconDay"] | "", -1);
    weather.days[i].weatherCode = app_logic::qweatherIconToWmoCode(iconDay);
    weather.days[i].maximumC = parseFloat(day["tempMax"] | "");
    weather.days[i].minimumC = parseFloat(day["tempMin"] | "");
    weather.days[i].uvMaximum = parseFloat(day["uvIndex"] | "");
    // 7d does not expose a probability field; leave it as unknown.
    weather.days[i].precipitationProbability = -1;
  }

  if (hourly.size() > 0) {
    weather.rainTimingAvailable = true;
    const size_t limit =
        min(static_cast<size_t>(config::RAIN_FORECAST_HOURS), hourly.size());
    for (size_t i = 0; i < limit; ++i) {
      JsonObject slot = hourly[i];
      const time_t slotTime = parseQweatherTimestamp(slot["fxTime"] | "");
      // Skip past the observation instant. Both timestamps are UTC
      // epoch, so an integer compare is unambiguous.
      if (slotTime == 0 ||
          (weather.updateTime != 0 && slotTime <= weather.updateTime)) {
        continue;
      }
      const float precipitation = parseFloat(slot["precip"] | "0");
      const int probability = parseInt(slot["pop"] | "", -1);
      if (app_logic::rainSlotQualifies(
              precipitation, probability, config::RAIN_START_THRESHOLD_MM,
              config::RAIN_PROBABILITY_THRESHOLD)) {
        weather.rainExpected = true;
        weather.nextRainTime = slotTime;
        weather.nextRainMm = precipitation;
        weather.nextRainProbability = probability;
        break;
      }
    }
  }

  // Severe-weather alerts. warning_env is optional (older cache files or a
  // failed warning fetch leave it null / with an empty list). We pick a single
  // alert with the highest severity rank; ties keep the first occurrence, so
  // the display is deterministic across refreshes with the same alert set.
  weather.alertTitle = "";
  weather.alertSeverity = "";
  weather.alertOtherCount = 0;
  JsonObject warningEnv = document["warning_env"];
  if (!warningEnv.isNull()) {
    const char* warningCode = warningEnv["code"] | "";
    if (!app_logic::qweatherResponseOk(warningCode)) {
      LOG.printf("[weather] QWeather warning code=%s (ignored)\n",
                 warningCode);
    } else {
      JsonArray warnings = warningEnv["warning"];
      int total = 0;
      int bestRank = -1;
      String bestTitle;
      String bestSeverity;
      for (JsonObject alert : warnings) {
        const char* title = alert["title"] | "";
        if (title[0] == '\0') continue;
        ++total;
        const char* severity = alert["severity"] | "";
        const int rank = app_logic::qweatherAlertSeverityRank(severity);
        if (rank > bestRank) {
          bestRank = rank;
          bestTitle = title;
          bestSeverity = severity;
        }
      }
      if (total > 0 && !bestTitle.isEmpty()) {
        weather.alertTitle = bestTitle;
        weather.alertSeverity = bestSeverity;
        weather.alertOtherCount = total - 1;
        LOG.printf("[weather] alert (%s): %s%s\n",
                   weather.alertSeverity.isEmpty()
                       ? "unknown"
                       : weather.alertSeverity.c_str(),
                   weather.alertTitle.c_str(),
                   weather.alertOtherCount > 0
                       ? (String(" (+") +
                          String(weather.alertOtherCount) + " more)")
                             .c_str()
                       : "");
      }
    }
  }

  weather.valid = isfinite(weather.temperatureC) &&
                  isfinite(weather.apparentC) &&
                  isfinite(weather.humidityPct) &&
                  weather.weatherCode >= 0;
  if (!weather.valid) {
    LOG.println("[weather] QWeather values are invalid");
    return false;
  }
  LOG.printf("[weather] %.1fC, feels %.1fC, %.0f%% RH, code=%d\n",
             weather.temperatureC, weather.apparentC, weather.humidityPct,
             weather.weatherCode);
  {
    char buf[24];
    local_time::formatLocalIso(weather.updateTime, buf, sizeof(buf));
    LOG.printf("[weather] QWeather obsTime=%lld (local %s)\n",
               static_cast<long long>(weather.updateTime), buf);
  }
  if (weather.rainExpected) {
    char buf[24];
    local_time::formatLocalIso(weather.nextRainTime, buf, sizeof(buf));
    LOG.printf("[weather] next rain around %s, %.1fmm, probability=%d%%\n",
               buf, weather.nextRainMm, weather.nextRainProbability);
  } else if (weather.rainTimingAvailable) {
    LOG.printf("[weather] no qualifying rain in the next %u hours\n",
               config::RAIN_FORECAST_HOURS);
  } else {
    LOG.println("[weather] hourly rain timing unavailable");
  }
  return true;
}

bool fetchQWeather(WeatherData& weather, String& responseBody,
                   String& failureReason, bool bypassHttpCache) {
  responseBody = "";
  failureReason = "";
  String bearerToken;
  if (!buildJwt(bearerToken, failureReason)) {
    return false;
  }
  if (bypassHttpCache) {
    LOG.println("[weather] button wake: forcing live API refresh");
  }
  String nowBody;
  String dailyBody;
  String hourlyBody;
  if (!fetchEndpoint(endpointUrl("/v7/weather/now"), bearerToken, nowBody,
                     failureReason, bypassHttpCache)) {
    return false;
  }
  if (!fetchEndpoint(endpointUrl("/v7/weather/7d"), bearerToken, dailyBody,
                     failureReason, bypassHttpCache)) {
    return false;
  }
  if (!fetchEndpoint(endpointUrl("/v7/weather/24h"), bearerToken, hourlyBody,
                     failureReason, bypassHttpCache)) {
    return false;
  }
  // Warnings are best-effort: an alert fetch failure must never fail the whole
  // weather refresh (many locations simply have no active alerts and QWeather
  // still returns HTTP 200 with an empty `warning` array). We stitch a
  // placeholder envelope so the parser can rely on `warning_env` being present.
  String warningBody;
  if (weather_config::runtime::qweatherAlertsEnabled()) {
    String warningFailureReason;
    if (!fetchEndpoint(endpointUrl("/v7/warning/now"), bearerToken, warningBody,
                       warningFailureReason, bypassHttpCache)) {
      LOG.printf("[weather] warning fetch failed (continuing): %s\n",
                 warningFailureReason.c_str());
      warningBody = "{\"code\":\"200\",\"warning\":[]}";
    }
  } else {
    warningBody = "{\"code\":\"200\",\"warning\":[]}";
  }
  responseBody.reserve(nowBody.length() + dailyBody.length() +
                       hourlyBody.length() + warningBody.length() + 96);
  responseBody = "{\"now_env\":";
  responseBody += nowBody;
  responseBody += ",\"daily_env\":";
  responseBody += dailyBody;
  responseBody += ",\"hourly_env\":";
  responseBody += hourlyBody;
  responseBody += ",\"warning_env\":";
  responseBody += warningBody;
  responseBody += "}";
  constexpr int kMaxEnvelopeBytes = 384 * 1024;
  if (static_cast<int>(responseBody.length()) > kMaxEnvelopeBytes) {
    LOG.printf("[weather] stitched envelope too large: %u bytes\n",
               static_cast<unsigned>(responseBody.length()));
    responseBody = "";
    failureReason = "Weather response is too large";
    return false;
  }
  LOG.printf("[weather] received %u bytes from QWeather (stitched)\n",
             static_cast<unsigned>(responseBody.length()));
  weather.fromCache = false;
  if (!parseQWeather(responseBody, weather)) {
    failureReason = "Weather service returned invalid data";
    return false;
  }
  return true;
}

}  // namespace weather_provider
