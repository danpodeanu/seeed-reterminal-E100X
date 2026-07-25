#pragma once

// Photo cache manifest (Contract C6).
//
// `tools/prepare_photos.py` writes a `manifest.json` next to the prepared
// 4-bit BMPs. The firmware reads it on boot and warns if the dither version
// the tool used no longer matches what the firmware would expect - which is
// the tell-tale sign that the dither algorithm has changed between when the
// user pre-rendered their photos and now. We never block rendering on a
// stale manifest; e-paper cards in the field commonly outlive firmware
// updates and the user experience is that the panel still shows a photo.
//
// Bump `dither_version` in lockstep with any change to
// `image_pipeline/src/dither.cpp` or the Python quantiser in
// `photo-viewer/tools/prepare_photos.py`.

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <cstddef>
#include <cstring>
#include <string>

namespace photo_manifest {

inline constexpr const char SCHEMA_FIELD[] = "_schema";
inline constexpr const char SCHEMA_TAG[]   = "reterminal-photos-v1";
inline constexpr const char VERSION_FIELD[] = "dither_version";
inline constexpr const char MANIFEST_FILE[] = "/manifest.json";

enum class Status {
  Absent,        // no manifest on disk (or unreadable)
  Matches,       // schema and dither_version match firmware expectations
  StaleDither,   // schema matches, dither_version differs
  Unrecognised,  // schema mismatch or malformed JSON
};

// Extract the string value of a top-level JSON string field. Returns true
// on success and writes the found value to `out`. The parser is intentionally
// small: we only need to locate `"_schema"` and `"dither_version"` inside a
// well-formed object written by prepare_photos.py, not accept arbitrary JSON.
inline bool findStringField(const char* json, size_t len,
                            const char* fieldName,
                            std::string& out) {
  out.clear();
  const size_t fieldLen = std::strlen(fieldName);
  size_t i = 0;
  bool inString = false;
  bool escape = false;
  int depth = 0;
  while (i < len) {
    const char c = json[i];
    if (escape) { escape = false; ++i; continue; }
    if (inString) {
      if (c == '\\') { escape = true; ++i; continue; }
      if (c == '"') { inString = false; ++i; continue; }
      ++i;
      continue;
    }
    if (c == '"') {
      // Potential field name at object depth 1.
      if (depth == 1 && i + 1 + fieldLen + 1 <= len &&
          std::memcmp(json + i + 1, fieldName, fieldLen) == 0 &&
          json[i + 1 + fieldLen] == '"') {
        size_t p = i + 1 + fieldLen + 1;
        while (p < len && (json[p] == ' ' || json[p] == '\t' ||
                           json[p] == '\r' || json[p] == '\n')) ++p;
        if (p >= len || json[p] != ':') return false;
        ++p;
        while (p < len && (json[p] == ' ' || json[p] == '\t' ||
                           json[p] == '\r' || json[p] == '\n')) ++p;
        if (p >= len || json[p] != '"') return false;
        ++p;
        const size_t valueStart = p;
        while (p < len && json[p] != '"') {
          if (json[p] == '\\' && p + 1 < len) p += 2;
          else ++p;
        }
        out.assign(json + valueStart, p - valueStart);
        return true;
      }
      inString = true;
      ++i;
      continue;
    }
    if (c == '{' || c == '[') ++depth;
    else if (c == '}' || c == ']') --depth;
    ++i;
  }
  return false;
}

// Parse a manifest blob against the expected dither version. `foundVersion`
// receives the value from the manifest (empty if the field was missing).
inline Status inspect(const char* json, size_t len,
                      const char* expectedDitherVersion,
                      std::string& foundVersion) {
  foundVersion.clear();
  std::string schema;
  if (!findStringField(json, len, SCHEMA_FIELD, schema)) return Status::Unrecognised;
  if (schema != SCHEMA_TAG) return Status::Unrecognised;
  if (!findStringField(json, len, VERSION_FIELD, foundVersion)) {
    return Status::Unrecognised;
  }
  return foundVersion == expectedDitherVersion ? Status::Matches
                                               : Status::StaleDither;
}

#ifdef ARDUINO
inline Status inspect(const String& json, const char* expectedDitherVersion,
                      String& foundVersion) {
  std::string found;
  const Status status = inspect(json.c_str(), json.length(),
                                expectedDitherVersion, found);
  foundVersion = String(found.c_str());
  return status;
}
#endif

}  // namespace photo_manifest
