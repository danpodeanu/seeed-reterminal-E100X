#include "calendar_provider.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/version.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "app_logger.h"
#include "calendar_config_runtime.h"
#include "calendar_http.h"
#include "calendar_logic.h"
#include "config.h"
#include "google_credentials.h"
#include "local_time.h"
#include "trusted_client.h"
#include "weather_app_logic.h"

namespace calendar_provider {
namespace {

struct GoogleCalendar {
  String id;
  String name;
  uint32_t color = 0x4A6FA5;
};

void scrub(String& value) {
  for (size_t i = 0; i < value.length(); ++i) value.setCharAt(i, '\0');
  value = "";
}

String base64Url(const String& input) {
  const size_t capacity = ((input.length() + 2U) / 3U) * 4U + 1U;
  char* buffer = static_cast<char*>(malloc(capacity));
  if (buffer == nullptr) return "";
  const size_t written = app_logic::encodeBase64Url(
      reinterpret_cast<const uint8_t*>(input.c_str()), input.length(),
      buffer, capacity);
  String result = written == 0 ? String() : String(buffer);
  memset(buffer, 0, capacity);
  free(buffer);
  return result;
}

String base64Url(const uint8_t* input, size_t length) {
  const size_t capacity = ((length + 2U) / 3U) * 4U + 1U;
  char* buffer = static_cast<char*>(malloc(capacity));
  if (buffer == nullptr) return "";
  const size_t written =
      app_logic::encodeBase64Url(input, length, buffer, capacity);
  String result = written == 0 ? String() : String(buffer);
  memset(buffer, 0, capacity);
  free(buffer);
  return result;
}

bool buildServiceAccountJwt(const google_credentials::Credentials& credentials,
                            String& jwt, String& failureReason) {
  jwt = "";
  const time_t now = time(nullptr);
  if (now < 1700000000) {
    failureReason = "Clock is not synchronized; Google authentication cannot run";
    return false;
  }

  JsonDocument headerDoc;
  headerDoc["alg"] = "RS256";
  headerDoc["typ"] = "JWT";
  headerDoc["kid"] = credentials.privateKeyId;
  String headerJson;
  serializeJson(headerDoc, headerJson);

  JsonDocument claimsDoc;
  claimsDoc["iss"] = credentials.clientEmail;
  claimsDoc["scope"] = "https://www.googleapis.com/auth/calendar.readonly";
  claimsDoc["aud"] = "https://oauth2.googleapis.com/token";
  claimsDoc["iat"] = static_cast<int64_t>(now) - 30;
  claimsDoc["exp"] = static_cast<int64_t>(now) + 3570;
  const char* delegatedUser = calendar_config::runtime::googleDelegatedUser();
  if (delegatedUser != nullptr && delegatedUser[0] != '\0') {
    claimsDoc["sub"] = delegatedUser;
  }
  String claimsJson;
  serializeJson(claimsDoc, claimsJson);

  String signingInput = base64Url(headerJson);
  const String encodedClaims = base64Url(claimsJson);
  scrub(headerJson);
  scrub(claimsJson);
  if (signingInput.isEmpty() || encodedClaims.isEmpty()) {
    failureReason = "Could not encode the Google authentication JWT";
    return false;
  }
  signingInput += '.';
  signingInput += encodedClaims;

  uint8_t digest[32] = {};
  const mbedtls_md_info_t* sha256 =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (sha256 == nullptr ||
      mbedtls_md(sha256,
                 reinterpret_cast<const unsigned char*>(signingInput.c_str()),
                 signingInput.length(), digest) != 0) {
    scrub(signingInput);
    failureReason = "Could not hash the Google authentication JWT";
    return false;
  }

  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_pk_context privateKey;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  mbedtls_pk_init(&privateKey);
  const char personalization[] = "reterminal-calendar-google";
  int result = mbedtls_ctr_drbg_seed(
      &random, mbedtls_entropy_func, &entropy,
      reinterpret_cast<const unsigned char*>(personalization),
      sizeof(personalization) - 1);
  if (result == 0) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    result = mbedtls_pk_parse_key(
        &privateKey,
        reinterpret_cast<const unsigned char*>(credentials.privateKey.c_str()),
        credentials.privateKey.length() + 1, nullptr, 0,
        mbedtls_ctr_drbg_random, &random);
#else
    result = mbedtls_pk_parse_key(
        &privateKey,
        reinterpret_cast<const unsigned char*>(credentials.privateKey.c_str()),
        credentials.privateKey.length() + 1, nullptr, 0);
#endif
  }

  uint8_t signature[512] = {};
  size_t signatureLength = 0;
  if (result == 0 && !mbedtls_pk_can_do(&privateKey, MBEDTLS_PK_RSA)) {
    result = MBEDTLS_ERR_PK_TYPE_MISMATCH;
  }
  if (result == 0) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    result = mbedtls_pk_sign(
        &privateKey, MBEDTLS_MD_SHA256, digest, sizeof(digest),
        signature, sizeof(signature), &signatureLength,
        mbedtls_ctr_drbg_random, &random);
#else
    result = mbedtls_pk_sign(
        &privateKey, MBEDTLS_MD_SHA256, digest, sizeof(digest),
        signature, &signatureLength, mbedtls_ctr_drbg_random, &random);
#endif
  }
  mbedtls_pk_free(&privateKey);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  memset(digest, 0, sizeof(digest));

  if (result != 0 || signatureLength == 0) {
    memset(signature, 0, sizeof(signature));
    scrub(signingInput);
    failureReason = "The uploaded Google private key could not sign a JWT";
    return false;
  }
  const String encodedSignature = base64Url(signature, signatureLength);
  memset(signature, 0, sizeof(signature));
  if (encodedSignature.isEmpty()) {
    scrub(signingInput);
    failureReason = "Could not encode the Google JWT signature";
    return false;
  }
  jwt = signingInput + "." + encodedSignature;
  scrub(signingInput);
  return true;
}

bool googleRequest(const String& url, const String& bearerToken,
                   String& body, String& failureReason) {
  body = "";
  tls_client::DefaultRootClient client;
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    failureReason = "Could not start a Google Calendar request";
    return false;
  }
  http.addHeader("Authorization", "Bearer " + bearerToken);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    failureReason = "Google Calendar returned HTTP " + String(status);
    http.end();
    return false;
  }
  constexpr size_t kMaximumResponse = 512U * 1024U;
  const bool bodyRead = calendar_http::readBody(
      http, kMaximumResponse, config::HTTP_TIMEOUT_MS, body, failureReason);
  http.end();
  return bodyRead;
}

bool exchangeAccessToken(const google_credentials::Credentials& credentials,
                         String& accessToken, String& failureReason) {
  String jwt;
  if (!buildServiceAccountJwt(credentials, jwt, failureReason)) return false;

  tls_client::DefaultRootClient client;
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, credentials.tokenUri)) {
    scrub(jwt);
    failureReason = "Could not start the Google token request";
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Accept-Encoding", "identity");
  String form =
      "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=";
  form += jwt;
  scrub(jwt);
  const int status = http.POST(form);
  scrub(form);
  if (status != HTTP_CODE_OK) {
    failureReason = "Google authentication returned HTTP " + String(status);
    http.end();
    return false;
  }
  String response;
  const bool bodyRead = calendar_http::readBody(
      http, 64U * 1024U, config::HTTP_TIMEOUT_MS, response, failureReason);
  http.end();
  if (!bodyRead) {
    scrub(response);
    return false;
  }
  JsonDocument document;
  if (deserializeJson(document, response)) {
    scrub(response);
    failureReason = "Google authentication returned invalid JSON";
    return false;
  }
  accessToken = String(document["access_token"] | "");
  const String errorDescription =
      String(document["error_description"] | "");
  document.clear();
  scrub(response);
  if (accessToken.isEmpty()) {
    failureReason = errorDescription.isEmpty()
                        ? "Google authentication returned no access token"
                        : errorDescription;
    return false;
  }
  return true;
}

String urlEncode(const String& value) {
  static const char kHex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += kHex[c >> 4];
      encoded += kHex[c & 0x0F];
    }
  }
  return encoded;
}

String utcTimestamp(time_t value) {
  struct tm utc = {};
  char buffer[25] = {};
  if (gmtime_r(&value, &utc) == nullptr) return "";
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buffer);
}

std::vector<String> configuredIds() {
  std::vector<String> result;
  String configured = calendar_config::runtime::googleCalendarIds();
  int start = 0;
  while (start <= static_cast<int>(configured.length())) {
    int end = configured.indexOf(',', start);
    if (end < 0) end = configured.length();
    String item = configured.substring(start, end);
    item.trim();
    if (!item.isEmpty()) result.push_back(item);
    if (end >= static_cast<int>(configured.length())) break;
    start = end + 1;
  }
  return result;
}

void mergeCalendarList(const String& body,
                       const std::vector<String>& requestedIds,
                       std::vector<GoogleCalendar>& calendars,
                       String& failureReason) {
  JsonDocument document;
  if (deserializeJson(document, body)) {
    failureReason = "Google calendar list returned invalid JSON";
    return;
  }
  for (JsonObject item : document["items"].as<JsonArray>()) {
    const String id = String(item["id"] | "");
    if (id.isEmpty()) continue;
    if (!requestedIds.empty() &&
        std::find(requestedIds.begin(), requestedIds.end(), id) ==
            requestedIds.end()) {
      continue;
    }
    GoogleCalendar calendar;
    calendar.id = id;
    calendar.name = String(item["summaryOverride"] |
                           (item["summary"] | id.c_str()));
    calendar.color =
        calendar_logic::parseRgb(item["backgroundColor"] | "", 0x4A6FA5);
    calendars.push_back(calendar);
    if (calendars.size() >= config::MAX_GOOGLE_CALENDARS) break;
  }
  for (const String& requested : requestedIds) {
    const auto found =
        std::find_if(calendars.begin(), calendars.end(),
                     [&](const GoogleCalendar& value) {
                       return value.id == requested;
                     });
    if (found == calendars.end() &&
        calendars.size() < config::MAX_GOOGLE_CALENDARS) {
      GoogleCalendar calendar;
      calendar.id = requested;
      calendar.name = requested;
      calendars.push_back(calendar);
    }
  }
}

void parseEventColors(const String& body, uint32_t colors[12]) {
  JsonDocument document;
  if (deserializeJson(document, body)) return;
  JsonObject eventColors = document["event"];
  for (JsonPair item : eventColors) {
    const int id = atoi(item.key().c_str());
    if (id < 1 || id > 11) continue;
    colors[id] = calendar_logic::parseRgb(
        item.value()["background"] | "", colors[id]);
  }
}

bool parseGoogleDate(JsonVariantConst value, time_t& timestamp,
                     bool& allDay) {
  const char* dateTime = value["dateTime"] | "";
  if (dateTime[0] != '\0') {
    allDay = false;
    return local_time::parseIso8601Utc(dateTime, timestamp);
  }
  const char* date = value["date"] | "";
  if (date[0] == '\0') return false;
  char localValue[24] = {};
  snprintf(localValue, sizeof(localValue), "%sT00:00:00", date);
  allDay = true;
  return local_time::parseIso8601Local(localValue, timestamp);
}

bool appendEvents(const String& body, const GoogleCalendar& source,
                  uint8_t sourceIndex, const uint32_t colors[12],
                  calendar::Data& out, String& nextPageToken,
                  size_t& validCandidates, String& failureReason) {
  JsonDocument document;
  if (deserializeJson(document, body)) {
    failureReason = "Google events returned invalid JSON";
    return false;
  }
  nextPageToken = String(document["nextPageToken"] | "");
  const auto earlier = [](const calendar::Event& left,
                          const calendar::Event& right) {
    if (left.start != right.start) return left.start < right.start;
    if (left.allDay != right.allDay) return left.allDay;
    if (left.end != right.end) return left.end < right.end;
    return left.title < right.title;
  };
  for (JsonObject item : document["items"].as<JsonArray>()) {
    if (String(item["status"] | "") == "cancelled") continue;
    time_t start = 0;
    time_t end = 0;
    bool allDay = false;
    bool endAllDay = false;
    if (!parseGoogleDate(item["start"], start, allDay) ||
        !parseGoogleDate(item["end"], end, endAllDay) || end <= start) {
      continue;
    }
    ++validCandidates;
    calendar::Event event;
    event.uid = std::string(String(item["iCalUID"] | (item["id"] | "")).c_str());
    event.title =
        std::string(String(item["summary"] | "Busy").c_str());
    event.location =
        std::string(String(item["location"] | "").c_str());
    event.start = start;
    event.end = end;
    event.allDay = allDay;
    event.sourceIndex = sourceIndex;
    const int colorId = atoi(String(item["colorId"] | "0").c_str());
    event.colorRgb =
        colorId >= 1 && colorId <= 11 ? colors[colorId] : source.color;
    if (out.events.size() < config::MAX_CALENDAR_EVENTS) {
      out.events.push_back(std::move(event));
      continue;
    }

    out.truncated = true;
    const auto latest =
        std::max_element(out.events.begin(), out.events.end(), earlier);
    if (latest != out.events.end() && earlier(event, *latest)) {
      *latest = std::move(event);
    }
  }
  return true;
}

}  // namespace

bool fetchGoogle(const calendar::Window& window, calendar::Data& out,
                 String& failureReason, bool bypassHttpCache) {
  (void)bypassHttpCache;
  failureReason = "";
  out = calendar::Data{};

  google_credentials::Credentials credentials;
  if (!google_credentials::load(credentials, failureReason)) return false;
  String accessToken;
  if (!exchangeAccessToken(credentials, accessToken, failureReason)) {
    scrub(credentials.privateKey);
    return false;
  }
  scrub(credentials.privateKey);

  const std::vector<String> requested = configuredIds();
  String response;
  const String listUrl =
      "https://www.googleapis.com/calendar/v3/users/me/calendarList"
      "?maxResults=250&minAccessRole=reader"
      "&fields=items(id,summary,summaryOverride,backgroundColor)";
  if (!googleRequest(listUrl, accessToken, response, failureReason)) {
    scrub(accessToken);
    return false;
  }
  std::vector<GoogleCalendar> calendars;
  mergeCalendarList(response, requested, calendars, failureReason);
  response = "";
  if (!failureReason.isEmpty()) {
    scrub(accessToken);
    return false;
  }
  if (calendars.empty()) {
    scrub(accessToken);
    failureReason =
        requested.empty()
            ? "No calendars are visible; share one with the service account"
            : "None of the configured Google calendar IDs are accessible";
    return false;
  }

  uint32_t eventColors[12] = {
      0, 0x7986CB, 0x33B679, 0x8E24AA, 0xE67C73, 0xF6BF26, 0xF4511E,
      0x039BE5, 0x616161, 0x3F51B5, 0x0B8043, 0xD50000};
  if (googleRequest("https://www.googleapis.com/calendar/v3/colors"
                    "?fields=event",
                    accessToken, response, failureReason)) {
    parseEventColors(response, eventColors);
  } else {
    LOG.printf("[calendar] Google colors unavailable: %s; using defaults\n",
               failureReason.c_str());
    failureReason = "";
  }
  response = "";

  for (size_t index = 0; index < calendars.size(); ++index) {
    calendar::Source source;
    source.id = std::string(calendars[index].id.c_str());
    source.name = std::string(calendars[index].name.c_str());
    source.colorRgb = calendars[index].color;
    out.sources.push_back(source);

    String pageToken;
    size_t validCandidates = 0;
    int pages = 0;
    do {
      const size_t remaining =
          config::MAX_CALENDAR_EVENTS - validCandidates;
      String url =
          "https://www.googleapis.com/calendar/v3/calendars/" +
          urlEncode(calendars[index].id) +
          "/events?singleEvents=true&showDeleted=false&orderBy=startTime"
          "&maxResults=" +
          String(static_cast<unsigned>(remaining)) +
          "&fields=nextPageToken,items(id,iCalUID,summary,location,status,"
          "colorId,start,end)&timeMin=" +
          urlEncode(utcTimestamp(window.start)) +
          "&timeMax=" + urlEncode(utcTimestamp(window.end));
      if (!pageToken.isEmpty()) {
        url += "&pageToken=" + urlEncode(pageToken);
      }
      if (!googleRequest(url, accessToken, response, failureReason) ||
          !appendEvents(response, calendars[index],
                       static_cast<uint8_t>(index), eventColors, out,
                       pageToken, validCandidates, failureReason)) {
        scrub(accessToken);
        return false;
      }
      response = "";
      ++pages;
      if (validCandidates >= config::MAX_CALENDAR_EVENTS) {
        if (!pageToken.isEmpty()) out.truncated = true;
        break;
      }
      if (!pageToken.isEmpty() && pages >= 32) {
        scrub(accessToken);
        failureReason = "Google events pagination exceeded the safe limit";
        return false;
      }
    } while (!pageToken.isEmpty());
  }
  scrub(accessToken);

  std::sort(out.events.begin(), out.events.end(),
            [](const calendar::Event& left, const calendar::Event& right) {
              if (left.start != right.start) return left.start < right.start;
              if (left.allDay != right.allDay) return left.allDay;
              if (left.end != right.end) return left.end < right.end;
              return left.title < right.title;
            });
  out.sourceLabel = calendars.size() == 1
                        ? std::string(calendars.front().name.c_str())
                        : "Google Calendar";
  out.fetchedAt = time(nullptr);
  LOG.printf("[calendar] loaded %u event(s) from %u Google calendar(s)%s\n",
             static_cast<unsigned>(out.events.size()),
             static_cast<unsigned>(out.sources.size()),
             out.truncated ? " (truncated)" : "");
  return true;
}

}  // namespace calendar_provider
