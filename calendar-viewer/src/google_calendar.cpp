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

constexpr const char* kCalendarReadOnlyScope =
    "https://www.googleapis.com/auth/calendar.readonly";
constexpr const char* kConfiguredCalendarScopes =
    "https://www.googleapis.com/auth/calendar.readonly "
    "https://www.googleapis.com/auth/calendar.calendarlist";

enum class GoogleRequestMethod {
  Get,
  Post,
};

void scrub(String& value) {
  for (size_t i = 0; i < value.length(); ++i) value.setCharAt(i, '\0');
  value = "";
}

String sanitizeGoogleMessage(String value) {
  constexpr size_t kMaximumDiagnosticLength = 240;
  if (value.length() > kMaximumDiagnosticLength) {
    value.remove(kMaximumDiagnosticLength);
    value += "...";
  }
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t character = static_cast<uint8_t>(value[i]);
    if (character < 0x20 || character == 0x7F) value.setCharAt(i, ' ');
  }
  value.trim();
  return value;
}

String googleErrorMessage(const String& body) {
  if (body.isEmpty()) return "";
  JsonDocument document;
  const DeserializationError parseError = deserializeJson(document, body);
  if (parseError) {
    return "Google returned an unreadable error response (" +
           String(parseError.c_str()) + ")";
  }

  const String description = String(document["error_description"] | "");
  const String errorCode =
      document["error"].is<const char*>()
          ? String(document["error"].as<const char*>())
          : String();
  if (!description.isEmpty()) {
    return sanitizeGoogleMessage(
        errorCode.isEmpty() ? description
                            : description + " (" + errorCode + ")");
  }
  if (document["error"].is<const char*>()) {
    return sanitizeGoogleMessage(errorCode);
  }

  const String message = String(document["error"]["message"] | "");
  const String reason =
      String(document["error"]["errors"][0]["reason"] | "");
  if (message.isEmpty()) return sanitizeGoogleMessage(reason);
  if (reason.isEmpty() || message.indexOf(reason) >= 0) {
    return sanitizeGoogleMessage(message);
  }
  return sanitizeGoogleMessage(message + " (" + reason + ")");
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
                            const char* scope, String& jwt,
                            String& failureReason) {
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
  claimsDoc["scope"] = scope;
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
    LOG.printf("[google] private-key/JWT signing failed with code %d\n",
               result);
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

bool googleRequest(GoogleRequestMethod method, const String& operation,
                   const String& url, const String& bearerToken,
                   const String& requestBody, String& responseBody,
                   String& failureReason, int* responseStatus) {
  responseBody = "";
  if (responseStatus != nullptr) *responseStatus = 0;
  const char* methodName =
      method == GoogleRequestMethod::Get ? "GET" : "POST";
  LOG.printf("[google] %s %s\n", methodName, operation.c_str());
  tls_client::DefaultRootClient client;
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    failureReason = "Could not start the " + operation + " request";
    LOG.printf("[google] %s\n", failureReason.c_str());
    return false;
  }
  http.addHeader("Authorization", "Bearer " + bearerToken);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  if (method == GoogleRequestMethod::Post) {
    http.addHeader("Content-Type", "application/json");
  }
  const int status = method == GoogleRequestMethod::Get
                         ? http.GET()
                         : http.POST(requestBody);
  if (responseStatus != nullptr) *responseStatus = status;
  if (status <= 0) {
    const String detail = HTTPClient::errorToString(status);
    failureReason = operation + " request failed";
    if (!detail.isEmpty()) failureReason += ": " + detail;
    LOG.printf("[google] %s\n", failureReason.c_str());
    http.end();
    return false;
  }

  const size_t maximumResponse =
      status == HTTP_CODE_OK ? 512U * 1024U : 64U * 1024U;
  const bool bodyRead = calendar_http::readBody(
      http, maximumResponse, config::HTTP_TIMEOUT_MS, responseBody,
      failureReason);
  http.end();
  if (!bodyRead) {
    failureReason = operation + " response failed: " + failureReason;
    LOG.printf("[google] %s\n", failureReason.c_str());
    return false;
  }
  LOG.printf("[google] %s -> HTTP %d, %u byte(s)\n", operation.c_str(),
             status, static_cast<unsigned>(responseBody.length()));
  if (status == HTTP_CODE_OK) return true;

  const String detail = googleErrorMessage(responseBody);
  failureReason = operation + " returned HTTP " + String(status);
  if (!detail.isEmpty()) failureReason += ": " + detail;
  LOG.printf("[google] %s\n", failureReason.c_str());
  responseBody = "";
  return false;
}

bool googleGet(const String& operation, const String& url,
               const String& bearerToken, String& responseBody,
               String& failureReason, int* responseStatus = nullptr) {
  const String requestBody;
  return googleRequest(GoogleRequestMethod::Get, operation, url, bearerToken,
                       requestBody, responseBody, failureReason,
                       responseStatus);
}

bool googlePostJson(const String& operation, const String& url,
                    const String& bearerToken, const String& requestBody,
                    String& responseBody, String& failureReason,
                    int* responseStatus = nullptr) {
  return googleRequest(GoogleRequestMethod::Post, operation, url, bearerToken,
                       requestBody, responseBody, failureReason,
                       responseStatus);
}

bool exchangeAccessToken(const google_credentials::Credentials& credentials,
                         const char* scope, String& accessToken,
                         String& failureReason) {
  String jwt;
  if (!buildServiceAccountJwt(credentials, scope, jwt, failureReason)) {
    LOG.printf("[google] JWT creation failed: %s\n", failureReason.c_str());
    return false;
  }

  LOG.printf("[google] POST %s (service account %s, scope %s%s)\n",
             credentials.tokenUri.c_str(), credentials.clientEmail.c_str(),
             scope,
             calendar_config::runtime::googleDelegatedUser()[0] == '\0'
                 ? ""
                 : ", delegated");
  tls_client::DefaultRootClient client;
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, credentials.tokenUri)) {
    scrub(jwt);
    failureReason = "Could not start the Google token request";
    LOG.printf("[google] %s\n", failureReason.c_str());
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
  if (status <= 0) {
    const String detail = HTTPClient::errorToString(status);
    failureReason = "Google authentication request failed";
    if (!detail.isEmpty()) failureReason += ": " + detail;
    LOG.printf("[google] %s\n", failureReason.c_str());
    http.end();
    return false;
  }

  LOG.printf("[google] token endpoint -> HTTP %d\n", status);
  String response;
  const bool bodyRead = calendar_http::readBody(
      http, 64U * 1024U, config::HTTP_TIMEOUT_MS, response, failureReason);
  http.end();
  if (!bodyRead) {
    failureReason = "Google authentication response failed: " + failureReason;
    LOG.printf("[google] %s\n", failureReason.c_str());
    scrub(response);
    return false;
  }
  if (status != HTTP_CODE_OK) {
    const String detail = googleErrorMessage(response);
    failureReason = "Google authentication returned HTTP " + String(status);
    if (!detail.isEmpty()) failureReason += ": " + detail;
    LOG.printf("[google] %s\n", failureReason.c_str());
    scrub(response);
    return false;
  }

  JsonDocument document;
  const DeserializationError parseError =
      deserializeJson(document, response);
  if (parseError) {
    scrub(response);
    failureReason = "Google authentication returned invalid JSON: " +
                    String(parseError.c_str());
    LOG.printf("[google] %s\n", failureReason.c_str());
    return false;
  }
  accessToken = String(document["access_token"] | "");
  const String errorDescription =
      String(document["error_description"] | "");
  const int expiresIn = document["expires_in"] | 0;
  document.clear();
  scrub(response);
  if (accessToken.isEmpty()) {
    failureReason = errorDescription.isEmpty()
                        ? "Google authentication returned no access token"
                        : errorDescription;
    LOG.printf("[google] %s\n", failureReason.c_str());
    return false;
  }
  LOG.printf("[google] authentication succeeded; token expires in %d seconds\n",
             expiresIn);
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

void parseCalendarList(const String& body,
                       std::vector<GoogleCalendar>& calendars,
                       String& failureReason) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    failureReason =
        "Google calendar list returned invalid JSON: " + String(error.c_str());
    return;
  }
  for (JsonObject item : document["items"].as<JsonArray>()) {
    const String id = String(item["id"] | "");
    if (id.isEmpty()) continue;
    GoogleCalendar calendar;
    calendar.id = id;
    calendar.name = String(item["summaryOverride"] |
                           (item["summary"] | id.c_str()));
    calendar.color =
        calendar_logic::parseRgb(item["backgroundColor"] | "", 0x4A6FA5);
    calendars.push_back(calendar);
    if (calendars.size() >= config::MAX_GOOGLE_CALENDARS) break;
  }
}

bool parseEventColors(const String& body, uint32_t colors[12]) {
  JsonDocument document;
  if (deserializeJson(document, body)) return false;
  JsonObject eventColors = document["event"];
  for (JsonPair item : eventColors) {
    const int id = atoi(item.key().c_str());
    if (id < 1 || id > 11) continue;
    colors[id] = calendar_logic::parseRgb(
        item.value()["background"] | "", colors[id]);
  }
  return true;
}

bool parseCalendarListEntry(const String& body, GoogleCalendar& calendar,
                            String& failureReason) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    failureReason = "Google calendar metadata returned invalid JSON: " +
                    String(error.c_str());
    return false;
  }

  const String returnedId = String(document["id"] | "");
  if (returnedId.isEmpty()) {
    failureReason = "Google calendar metadata did not include an ID";
    return false;
  }
  const String background = String(document["backgroundColor"] | "");
  constexpr uint32_t kInvalidColor = 0x1000000;
  const uint32_t color =
      calendar_logic::parseRgb(background.c_str(), kInvalidColor);
  if (color == kInvalidColor) {
    failureReason =
        "Google calendar metadata did not include a valid background color";
    return false;
  }

  calendar.id = returnedId;
  calendar.name = String(
      document["summaryOverride"] | (document["summary"] | returnedId.c_str()));
  calendar.color = color;
  const String accessRole = String(document["accessRole"] | "unknown");
  LOG.printf("[google] calendar metadata: %s, access=%s, color=%s\n",
             calendar.name.c_str(), accessRole.c_str(),
             background.c_str());
  return true;
}

bool ensureCalendarListEntry(GoogleCalendar& calendar,
                             const String& accessToken,
                             String& failureReason) {
  const String fields =
      "fields=id,summary,summaryOverride,accessRole,colorId,"
      "backgroundColor,foregroundColor";
  const String entryUrl =
      "https://www.googleapis.com/calendar/v3/users/me/calendarList/" +
      urlEncode(calendar.id) + "?" + fields;
  String response;
  int status = 0;
  if (googleGet("Google calendar metadata", entryUrl, accessToken, response,
                failureReason, &status)) {
    return parseCalendarListEntry(response, calendar, failureReason);
  }
  if (status != HTTP_CODE_NOT_FOUND) return false;

  LOG.printf("[google] calendar %s is not in CalendarList; adding it\n",
             calendar.id.c_str());
  JsonDocument requestDocument;
  requestDocument["id"] = calendar.id;
  String requestBody;
  serializeJson(requestDocument, requestBody);
  response = "";
  failureReason = "";
  const String insertUrl =
      "https://www.googleapis.com/calendar/v3/users/me/calendarList"
      "?" +
      fields;
  if (!googlePostJson("Google calendar-list insert", insertUrl, accessToken,
                      requestBody, response, failureReason)) {
    return false;
  }
  return parseCalendarListEntry(response, calendar, failureReason);
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
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    failureReason =
        "Google events returned invalid JSON: " + String(error.c_str());
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

  const std::vector<String> requested = configuredIds();
  const char* scope =
      requested.empty() ? kCalendarReadOnlyScope : kConfiguredCalendarScopes;
  LOG.printf("[google] starting calendar refresh using %s mode\n",
             requested.empty() ? "calendar discovery" : "configured IDs");

  google_credentials::Credentials credentials;
  if (!google_credentials::load(credentials, failureReason)) {
    LOG.printf("[google] credentials unavailable: %s\n",
               failureReason.c_str());
    return false;
  }
  String accessToken;
  if (!exchangeAccessToken(credentials, scope, accessToken, failureReason)) {
    scrub(credentials.privateKey);
    return false;
  }
  scrub(credentials.privateKey);

  String response;
  std::vector<GoogleCalendar> calendars;
  if (requested.empty()) {
    const String listUrl =
        "https://www.googleapis.com/calendar/v3/users/me/calendarList"
        "?maxResults=250&minAccessRole=reader"
        "&fields=items(id,summary,summaryOverride,backgroundColor)";
    if (!googleGet("Google calendar list", listUrl, accessToken, response,
                   failureReason)) {
      scrub(accessToken);
      return false;
    }
    parseCalendarList(response, calendars, failureReason);
    response = "";
    if (!failureReason.isEmpty()) {
      LOG.printf("[google] calendar-list parsing failed: %s\n",
                 failureReason.c_str());
      scrub(accessToken);
      return false;
    }
    LOG.printf("[google] discovered %u readable calendar(s)\n",
               static_cast<unsigned>(calendars.size()));
  } else {
    for (const String& id : requested) {
      if (calendars.size() >= config::MAX_GOOGLE_CALENDARS) break;
      GoogleCalendar calendar;
      calendar.id = id;
      calendar.name = id;
      if (!ensureCalendarListEntry(calendar, accessToken, failureReason)) {
        LOG.printf("[google] calendar metadata failed for %s: %s\n",
                   id.c_str(), failureReason.c_str());
        scrub(accessToken);
        return false;
      }
      calendars.push_back(calendar);
    }
    LOG.printf("[google] querying %u configured calendar ID(s) directly\n",
               static_cast<unsigned>(calendars.size()));
  }
  if (calendars.empty()) {
    scrub(accessToken);
    failureReason =
        "No calendars are visible; share one with the service account";
    LOG.printf("[google] %s\n", failureReason.c_str());
    return false;
  }

  uint32_t eventColors[12] = {
      0, 0x7986CB, 0x33B679, 0x8E24AA, 0xE67C73, 0xF6BF26, 0xF4511E,
      0x039BE5, 0x616161, 0x3F51B5, 0x0B8043, 0xD50000};
  if (googleGet("Google event colors",
                "https://www.googleapis.com/calendar/v3/colors"
                "?fields=event",
                accessToken, response, failureReason)) {
    if (parseEventColors(response, eventColors)) {
      LOG.printf("[google] loaded event colors\n");
    } else {
      LOG.printf("[google] event colors returned invalid JSON; using defaults\n");
    }
  } else {
    LOG.printf("[google] event colors unavailable: %s; using defaults\n",
               failureReason.c_str());
    failureReason = "";
  }
  response = "";

  const String windowStart = utcTimestamp(window.start);
  const String windowEnd = utcTimestamp(window.end);
  LOG.printf("[google] event window %s to %s\n", windowStart.c_str(),
             windowEnd.c_str());
  for (size_t index = 0; index < calendars.size(); ++index) {
    calendar::Source source;
    source.id = std::string(calendars[index].id.c_str());
    source.name = std::string(calendars[index].name.c_str());
    source.colorRgb = calendars[index].color;
    out.sources.push_back(source);

    String pageToken;
    size_t validCandidates = 0;
    int pages = 0;
    LOG.printf("[google] querying calendar %u/%u: %s\n",
               static_cast<unsigned>(index + 1),
               static_cast<unsigned>(calendars.size()),
               calendars[index].id.c_str());
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
          urlEncode(windowStart) + "&timeMax=" + urlEncode(windowEnd);
      if (!pageToken.isEmpty()) {
        url += "&pageToken=" + urlEncode(pageToken);
      }
      if (!googleGet("Google events", url, accessToken, response,
                     failureReason)) {
        scrub(accessToken);
        return false;
      }
      if (!appendEvents(response, calendars[index],
                       static_cast<uint8_t>(index), eventColors, out,
                       pageToken, validCandidates, failureReason)) {
        LOG.printf("[google] event parsing failed for %s: %s\n",
                  calendars[index].id.c_str(), failureReason.c_str());
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
        LOG.printf("[google] %s for %s\n", failureReason.c_str(),
                   calendars[index].id.c_str());
        return false;
      }
    } while (!pageToken.isEmpty());
    LOG.printf("[google] calendar %s returned %u candidate event(s) in %d "
               "page(s)\n",
               calendars[index].id.c_str(),
               static_cast<unsigned>(validCandidates), pages);
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
  LOG.printf("[google] loaded %u event(s) from %u calendar(s)%s\n",
             static_cast<unsigned>(out.events.size()),
             static_cast<unsigned>(out.sources.size()),
             out.truncated ? " (truncated)" : "");
  return true;
}

}  // namespace calendar_provider
