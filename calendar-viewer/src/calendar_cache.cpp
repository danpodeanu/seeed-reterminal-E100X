#include "calendar_cache.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>

#include <cstdio>
#include <cstring>
#include <utility>

#include "config.h"

namespace calendar_cache {
namespace {

constexpr uint32_t kSchemaVersion = 1;
constexpr const char* kCachePath = "/calendar-cache.json";
constexpr const char* kTemporaryPath = "/calendar-cache.tmp";
constexpr const char* kBackupPath = "/calendar-cache.bak";
constexpr size_t kMaximumStringBytes = 8192;

bool ensureMounted(String& failureReason) {
  static bool attempted = false;
  static bool mounted = false;
  if (!attempted) {
    attempted = true;
    mounted = SPIFFS.begin(true);
  }
  if (!mounted) failureReason = "Could not mount the internal calendar cache";
  return mounted;
}

String identityText(uint64_t identity) {
  char value[17] = {};
  snprintf(value, sizeof(value), "%016llx",
           static_cast<unsigned long long>(identity));
  return String(value);
}

bool validString(JsonVariantConst value) {
  const char* text = value.as<const char*>();
  return text != nullptr && strlen(text) <= kMaximumStringBytes;
}

bool cacheableString(const std::string& value) {
  return value.size() <= kMaximumStringBytes;
}

bool readSource(JsonObjectConst object, calendar::Source& source) {
  if (!validString(object["i"]) || !validString(object["n"]) ||
      !object["c"].is<uint32_t>() || !object["g"].is<bool>()) {
    return false;
  }
  source.id = object["i"].as<const char*>();
  source.name = object["n"].as<const char*>();
  source.colorRgb = object["c"].as<uint32_t>();
  source.googleColorAvailable = object["g"].as<bool>();
  return true;
}

bool readEvent(JsonObjectConst object, calendar::Event& event) {
  if (!validString(object["u"]) || !validString(object["t"]) ||
      !validString(object["l"]) || !object["s"].is<int64_t>() ||
      !object["e"].is<int64_t>() || !object["a"].is<bool>() ||
      !object["c"].is<uint32_t>() || !object["i"].is<uint8_t>()) {
    return false;
  }
  event.uid = object["u"].as<const char*>();
  event.title = object["t"].as<const char*>();
  event.location = object["l"].as<const char*>();
  event.start = static_cast<time_t>(object["s"].as<int64_t>());
  event.end = static_cast<time_t>(object["e"].as<int64_t>());
  event.allDay = object["a"].as<bool>();
  event.colorRgb = object["c"].as<uint32_t>();
  event.sourceIndex = object["i"].as<uint8_t>();
  return event.end > event.start;
}

bool loadFile(const char* path, uint64_t identity,
              calendar::Data& data, calendar::Window& window,
              String& failureReason) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    failureReason = "Could not open the calendar cache";
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > config::MAX_CALENDAR_CACHE_BYTES) {
    file.close();
    failureReason = "The calendar cache has an invalid size";
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    failureReason = "Could not parse the calendar cache: " +
                    String(error.c_str());
    return false;
  }

  if (document["v"].as<uint32_t>() != kSchemaVersion ||
      String(document["id"] | "") != identityText(identity)) {
    failureReason = "The calendar cache belongs to different settings";
    return false;
  }
  if (!document["saved"].is<int64_t>() ||
      !document["start"].is<int64_t>() ||
      !document["end"].is<int64_t>() ||
      !document["truncated"].is<bool>() ||
      !validString(document["label"]) ||
      !document["sources"].is<JsonArrayConst>() ||
      !document["events"].is<JsonArrayConst>()) {
    failureReason = "The calendar cache metadata is incomplete";
    return false;
  }

  const time_t saved =
      static_cast<time_t>(document["saved"].as<int64_t>());
  if (saved <= 0) {
    failureReason = "The calendar cache timestamp is invalid";
    return false;
  }

  const calendar::Window cachedWindow{
      static_cast<time_t>(document["start"].as<int64_t>()),
      static_cast<time_t>(document["end"].as<int64_t>()),
  };
  JsonArrayConst sources = document["sources"].as<JsonArrayConst>();
  JsonArrayConst events = document["events"].as<JsonArrayConst>();
  if (cachedWindow.end <= cachedWindow.start ||
      sources.size() > config::MAX_GOOGLE_CALENDARS ||
      events.size() > config::MAX_CALENDAR_EVENTS) {
    failureReason = "The calendar cache metadata is invalid";
    return false;
  }

  calendar::Data loaded;
  loaded.sources.reserve(sources.size());
  for (JsonObjectConst object : sources) {
    calendar::Source source;
    if (!readSource(object, source)) {
      failureReason = "The calendar cache contains an invalid source";
      return false;
    }
    loaded.sources.push_back(std::move(source));
  }
  loaded.events.reserve(events.size());
  for (JsonObjectConst object : events) {
    calendar::Event event;
    if (!readEvent(object, event)) {
      failureReason = "The calendar cache contains an invalid event";
      return false;
    }
    loaded.events.push_back(std::move(event));
  }
  loaded.sourceLabel = document["label"].as<const char*>();
  loaded.fetchedAt = saved;
  loaded.truncated = document["truncated"].as<bool>();

  data = std::move(loaded);
  window = cachedWindow;
  return true;
}

}  // namespace

bool load(uint64_t identity, calendar::Data& data,
          calendar::Window& window, String& failureReason) {
  failureReason = "";
  if (!ensureMounted(failureReason)) return false;

  const bool primaryExists = SPIFFS.exists(kCachePath);
  const bool backupExists = SPIFFS.exists(kBackupPath);
  if (!primaryExists && !backupExists) {
    failureReason = "No calendar cache is stored";
    return false;
  }

  String primaryFailure;
  if (primaryExists &&
      loadFile(kCachePath, identity, data, window, primaryFailure)) {
    if (backupExists) SPIFFS.remove(kBackupPath);
    return true;
  }

  String backupFailure;
  calendar::Data backupData;
  calendar::Window backupWindow;
  if (backupExists &&
      loadFile(kBackupPath, identity, backupData, backupWindow,
               backupFailure)) {
    data = std::move(backupData);
    window = backupWindow;
    if (!primaryExists || SPIFFS.remove(kCachePath)) {
      SPIFFS.rename(kBackupPath, kCachePath);
    }
    return true;
  }

  failureReason = primaryExists ? primaryFailure : backupFailure;
  if (primaryExists && backupExists && !backupFailure.isEmpty()) {
    failureReason += "; backup: " + backupFailure;
  }
  return false;
}

bool save(uint64_t identity, const calendar::Data& data,
          const calendar::Window& window, String& failureReason) {
  failureReason = "";
  if (data.fetchedAt <= 0 || window.end <= window.start ||
      data.sources.size() > config::MAX_GOOGLE_CALENDARS ||
      data.events.size() > config::MAX_CALENDAR_EVENTS ||
      !cacheableString(data.sourceLabel)) {
    failureReason = "Calendar data is not valid for caching";
    return false;
  }
  for (const calendar::Source& source : data.sources) {
    if (!cacheableString(source.id) || !cacheableString(source.name)) {
      failureReason = "A calendar source is too large for caching";
      return false;
    }
  }
  for (const calendar::Event& event : data.events) {
    if (!cacheableString(event.uid) || !cacheableString(event.title) ||
        !cacheableString(event.location)) {
      failureReason = "A calendar event is too large for caching";
      return false;
    }
  }
  if (!ensureMounted(failureReason)) return false;

  JsonDocument document;
  document["v"] = kSchemaVersion;
  document["id"] = identityText(identity);
  document["saved"] = static_cast<int64_t>(data.fetchedAt);
  document["start"] = static_cast<int64_t>(window.start);
  document["end"] = static_cast<int64_t>(window.end);
  document["truncated"] = data.truncated;
  document["label"] = data.sourceLabel.c_str();

  JsonArray sources = document["sources"].to<JsonArray>();
  for (const calendar::Source& source : data.sources) {
    JsonObject object = sources.add<JsonObject>();
    object["i"] = source.id.c_str();
    object["n"] = source.name.c_str();
    object["c"] = source.colorRgb;
    object["g"] = source.googleColorAvailable;
  }

  JsonArray events = document["events"].to<JsonArray>();
  for (const calendar::Event& event : data.events) {
    JsonObject object = events.add<JsonObject>();
    object["u"] = event.uid.c_str();
    object["t"] = event.title.c_str();
    object["l"] = event.location.c_str();
    object["s"] = static_cast<int64_t>(event.start);
    object["e"] = static_cast<int64_t>(event.end);
    object["a"] = event.allDay;
    object["c"] = event.colorRgb;
    object["i"] = event.sourceIndex;
  }

  const size_t expectedSize = measureJson(document);
  if (expectedSize == 0 || expectedSize > config::MAX_CALENDAR_CACHE_BYTES) {
    failureReason = "Calendar data is too large for the internal cache";
    return false;
  }

  SPIFFS.remove(kTemporaryPath);
  File file = SPIFFS.open(kTemporaryPath, FILE_WRITE);
  if (!file) {
    failureReason = "Could not create the calendar cache";
    return false;
  }
  const size_t written = serializeJson(document, file);
  file.flush();
  file.close();
  if (written != expectedSize) {
    SPIFFS.remove(kTemporaryPath);
    failureReason = "Could not write the complete calendar cache";
    return false;
  }

  if (!SPIFFS.exists(kCachePath) && SPIFFS.exists(kBackupPath) &&
      !SPIFFS.rename(kBackupPath, kCachePath)) {
    SPIFFS.remove(kTemporaryPath);
    failureReason = "Could not recover the previous calendar cache";
    return false;
  }
  if (SPIFFS.exists(kCachePath)) {
    if (SPIFFS.exists(kBackupPath) && !SPIFFS.remove(kBackupPath)) {
      SPIFFS.remove(kTemporaryPath);
      failureReason = "Could not prepare the calendar cache backup";
      return false;
    }
    if (!SPIFFS.rename(kCachePath, kBackupPath)) {
      SPIFFS.remove(kTemporaryPath);
      failureReason = "Could not back up the previous calendar cache";
      return false;
    }
  }
  if (!SPIFFS.rename(kTemporaryPath, kCachePath)) {
    if (SPIFFS.exists(kBackupPath) && !SPIFFS.exists(kCachePath)) {
      SPIFFS.rename(kBackupPath, kCachePath);
    }
    SPIFFS.remove(kTemporaryPath);
    failureReason = "Could not install the updated calendar cache";
    return false;
  }
  SPIFFS.remove(kBackupPath);
  return true;
}

}  // namespace calendar_cache
