#pragma once

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include <ArduinoJson.h>

// Pure JSONL manifest builders extracted from xkcd_index.cpp so the
// on-disk serialization can be exercised by native unit tests.
//
// The production persist() path calls these once per line and streams
// the resulting text straight to the SD file, so peak RAM is still
// O(1) in the number of comics -- only one line ever lives in memory
// at a time.
namespace xkcd_manifest {

namespace detail {

inline std::string serializeToString(JsonDocument& doc) {
  const size_t needed = measureJson(doc) + 1;
  std::string buffer(needed, '\0');
  const size_t written = serializeJson(doc, &buffer[0], needed);
  buffer.resize(written);
  return buffer;
}

}  // namespace detail

// Header line: {"v":<version>[,"l":<latest>],"s":[<skip>,...]}
// A trailing '\n' terminates every JSONL record.  `latest` is only
// emitted when > 0 so a freshly created empty manifest stays compact.
inline std::string buildJsonlHeader(uint32_t version, int latest,
                                    const std::vector<int>& skips) {
  JsonDocument doc;
  doc["v"] = version;
  if (latest > 0) doc["l"] = latest;
  JsonArray skipArray = doc["s"].to<JsonArray>();
  for (const int n : skips) skipArray.add(n);
  std::string out = detail::serializeToString(doc);
  out.push_back('\n');
  return out;
}

// Per-comic line: {"n":N,"t":"...","a":"...","e":"...","u":"...","y":Y,"m":M,"d":D}
// Null pointers are serialized as empty strings so the parser round-trip
// stays symmetric with StoredMeta whose fields default to empty String.
// The "y"/"m"/"d" publication-date triplet is omitted when year is zero,
// so v5 files upgraded in place without dates stay compact.
inline std::string buildJsonlComic(int number, const char* title,
                                   const char* alt, const char* extension,
                                   const char* url, int year = 0,
                                   int month = 0, int day = 0) {
  JsonDocument doc;
  doc["n"] = number;
  doc["t"] = title != nullptr ? title : "";
  doc["a"] = alt != nullptr ? alt : "";
  doc["e"] = extension != nullptr ? extension : "";
  doc["u"] = url != nullptr ? url : "";
  if (year > 0 && month > 0 && day > 0) {
    doc["y"] = year;
    doc["m"] = month;
    doc["d"] = day;
  }
  std::string out = detail::serializeToString(doc);
  out.push_back('\n');
  return out;
}

}  // namespace xkcd_manifest
