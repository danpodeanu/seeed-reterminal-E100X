#pragma once

// Cache schema helpers for the xkcd metadata JSON stored on the SD card.
//
// Contract C6 ("on-disk cache format is stable and self-describing"): every
// file we write must carry a schema tag so that a firmware update, a change
// to `parseComic`, or an upstream field rename never yields a silently
// misparsed cache. Legacy untagged files (from firmwares before this
// header existed) are still accepted for one grace release; anything with a
// tag must match `SCHEMA_TAG`.

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cctype>
#include <string>

namespace xkcd_cache {

inline constexpr const char SCHEMA_FIELD[] = "_schema";
inline constexpr const char SCHEMA_TAG[] = "xkcd-comic-v1";

// Wraps a JSON object payload by injecting `"_schema":"xkcd-comic-v1"` as
// the first key. If the payload is not a JSON object (does not start with
// `{` after leading whitespace), it is returned unchanged so that we never
// corrupt a partial download by prepending a tag to something we do not
// understand. `parseComic` will still reject the untagged file later, at
// which point the caller refetches.
inline std::string wrapWithSchema(const std::string& rawJson) {
  size_t i = 0;
  while (i < rawJson.size() &&
         std::isspace(static_cast<unsigned char>(rawJson[i]))) {
    ++i;
  }
  if (i >= rawJson.size() || rawJson[i] != '{') return rawJson;
  size_t j = i + 1;
  while (j < rawJson.size() &&
         std::isspace(static_cast<unsigned char>(rawJson[j]))) {
    ++j;
  }
  const bool objectIsEmpty = (j < rawJson.size() && rawJson[j] == '}');
  std::string out;
  out.reserve(rawJson.size() + 32);
  out.append(rawJson, 0, i + 1);
  out += '"';
  out += SCHEMA_FIELD;
  out += "\":\"";
  out += SCHEMA_TAG;
  out += '"';
  if (!objectIsEmpty) out += ',';
  out.append(rawJson, i + 1, std::string::npos);
  return out;
}

#ifdef ARDUINO
inline String wrapWithSchema(const String& rawJson) {
  const std::string in(rawJson.c_str(), rawJson.length());
  const std::string out = wrapWithSchema(in);
  return String(out.c_str());
}
#endif

}  // namespace xkcd_cache
