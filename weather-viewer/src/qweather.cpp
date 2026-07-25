#include <Arduino.h>
#include <ArduinoJson.h>
#include <Ed25519.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "app_logger.h"
#include "app_logic.h"
#include "config.h"
#include "secrets.h"
#include "weather_data.h"
#include "weather_provider.h"

// Provide safe defaults for QWeather secrets so the firmware still compiles
// when the user's secrets.h omits them (they are only needed when
// config::WEATHER_PROVIDER is set to QWeather). Missing credentials are
// caught at runtime with a descriptive log line.
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
  url += QWEATHER_API_HOST;
  url += path;
  // QWeather accepts either a LocationID or "longitude,latitude"
  // (longitude first, comma-separated, at most two decimals). Reuse the
  // single config::LATITUDE / LONGITUDE that Open-Meteo also uses so both
  // providers refer to one canonical location.
  url += "?location=";
  url += String(config::LONGITUDE, 2);
  url += ",";
  url += String(config::LATITUDE, 2);
  url += "&unit=m";
  return url;
}

// QWeather returns "YYYY-MM-DDTHH:MM+HH:MM" timestamps (no seconds field).
// Normalize to "YYYY-MM-DDTHH:MM:SS" so parseLocalTimestamp() accepts them
// and slot-vs-observation string comparisons stay consistent.
String normalizeTimestamp(const char* raw) {
  if (raw == nullptr) return String();
  String value(raw);
  const int plus = value.indexOf('+');
  const int minus = value.lastIndexOf('-');
  int cut = -1;
  if (plus >= 10) {
    cut = plus;                 // drop "+HH:MM"
  } else if (minus > 10) {
    cut = minus;                // drop "-HH:MM" (do not truncate date dashes)
  } else if (value.endsWith("Z")) {
    cut = value.length() - 1;
  }
  if (cut > 0) value.remove(cut);
  // If seconds are missing (length is exactly "YYYY-MM-DDTHH:MM"), append.
  if (value.length() == 16 && value.charAt(13) == ':') {
    value += ":00";
  }
  return value;
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
  const char* kid = QWEATHER_CREDENTIAL_ID;
  const char* sub = QWEATHER_PROJECT_ID;
  const char* privateKeyHex = QWEATHER_PRIVATE_KEY_HEX;
  if (kid[0] == '\0' || sub[0] == '\0' || privateKeyHex[0] == '\0') {
    failureReason = "QWeather credentials are not configured";
    LOG.println(
        "[weather] QWeather selected but PROJECT_ID / CREDENTIAL_ID / "
        "PRIVATE_KEY_HEX is empty");
    return false;
  }
  uint8_t privateKey[32];
  const size_t decoded = app_logic::decodeHex(
      privateKeyHex, strlen(privateKeyHex), privateKey, sizeof(privateKey));
  if (decoded != sizeof(privateKey)) {
    failureReason = "QWeather private key is not 32 bytes of hex";
    LOG.println("[weather] QWEATHER_PRIVATE_KEY_HEX is not 64 hex chars");
    return false;
  }

  const time_t now = time(nullptr);
  if (now < 1700000000) {
    failureReason = "Clock is not set; cannot sign JWT";
    LOG.println("[weather] JWT would use a clock that is not NTP-synced");
    return false;
  }
  const int64_t lifetime = app_logic::clampJwtLifetime(15 * 60);  // 15 min
  // Back-date iat by 30 seconds so a small clock skew relative to QWeather
  // does not cause "token not yet valid" rejections.
  const int64_t iat = static_cast<int64_t>(now) - 30;
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
  http.addHeader("Authorization", "Bearer " + bearerToken);
  http.addHeader("Accept-Encoding", "identity");
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
  http.end();
  if (static_cast<int>(body.length()) > kMaxResponseBytes) {
    LOG.printf("[weather] streamed response too large: %u bytes\n",
               static_cast<unsigned>(body.length()));
    body = "";
    failureReason = "Weather response is too large";
    return false;
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
  weather.updateTime = normalizeTimestamp(now["obsTime"] | "");
  weather.rainTimingAvailable = false;
  weather.rainExpected = false;
  weather.nextRainTime = "";
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
      const String slotTime = normalizeTimestamp(slot["fxTime"] | "");
      if (slotTime.isEmpty() ||
          (!weather.updateTime.isEmpty() &&
           strcmp(slotTime.c_str(), weather.updateTime.c_str()) <= 0)) {
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
  if (weather.rainExpected) {
    LOG.printf("[weather] next rain around %s, %.1fmm, probability=%d%%\n",
               weather.nextRainTime.c_str(), weather.nextRainMm,
               weather.nextRainProbability);
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
  responseBody.reserve(nowBody.length() + dailyBody.length() +
                       hourlyBody.length() + 64);
  responseBody = "{\"now_env\":";
  responseBody += nowBody;
  responseBody += ",\"daily_env\":";
  responseBody += dailyBody;
  responseBody += ",\"hourly_env\":";
  responseBody += hourlyBody;
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
