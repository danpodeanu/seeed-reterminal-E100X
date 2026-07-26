#include "xkcd_index.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "app_logger.h"
#include "config.h"
#include "xkcd_index_pure.h"

namespace xkcd_index {
namespace {

std::vector<int> g_numbers;
std::vector<int> g_skips;
std::unordered_map<int, ComicMeta> g_meta;
int g_latest = 0;
bool g_ready = false;

bool validateSortedUnique(const std::vector<int>& values) {
  int previous = 0;
  for (const int value : values) {
    if (value <= previous || value == 404) return false;
    previous = value;
  }
  return true;
}

bool decodeSkipArray(JsonArrayConst array, std::vector<int>& out) {
  if (array.size() > config::MAX_CACHE_INDEX_ENTRIES) return false;
  out.clear();
  out.reserve(array.size());
  for (JsonVariantConst element : array) {
    if (!element.is<int>() && !element.is<long>()) return false;
    const long value = element.as<long>();
    if (value <= 0 || value > INT32_MAX) return false;
    out.push_back(static_cast<int>(value));
  }
  return validateSortedUnique(out);
}

bool extensionIsSupported(const String& extension) {
  return extension == ".png" || extension == ".jpg" ||
         extension == ".jpeg" || extension == ".bmp";
}

void clearAll() {
  g_numbers.clear();
  g_skips.clear();
  g_meta.clear();
  g_latest = 0;
  g_ready = false;
}

}  // namespace

bool ready() { return g_ready; }

uint32_t count() {
  return g_ready ? static_cast<uint32_t>(g_numbers.size()) : 0U;
}

int latest() { return g_latest; }

void setLatest(int number) {
  if (number > 0) g_latest = number;
}

const std::vector<int>& entries() { return g_numbers; }

const std::vector<int>& skips() { return g_skips; }

bool skipped(int number) {
  if (!g_ready || number <= 0) return false;
  return std::binary_search(g_skips.begin(), g_skips.end(), number);
}

const ComicMeta* metadata(int number) {
  if (!g_ready || number <= 0) return nullptr;
  const auto it = g_meta.find(number);
  return it == g_meta.end() ? nullptr : &it->second;
}

void addComic(int number, const ComicMeta& meta) {
  if (number <= 0 || number == 404) return;
  const auto position =
      std::lower_bound(g_numbers.begin(), g_numbers.end(), number);
  if (position == g_numbers.end() || *position != number) {
    g_numbers.insert(position, number);
  }
  g_meta[number] = meta;
  if (number > g_latest) g_latest = number;
  g_ready = true;
}

bool persist() {
  JsonDocument doc;
  doc["version"] = config::CACHE_INDEX_VERSION;
  if (g_latest > 0) doc["latest"] = g_latest;
  JsonObject comics = doc["comics"].to<JsonObject>();
  for (const int number : g_numbers) {
    const auto it = g_meta.find(number);
    if (it == g_meta.end()) continue;
    // Numeric ArduinoJson keys must be strings, so key by the
    // decimal representation.
    JsonObject entry = comics[String(number).c_str()].to<JsonObject>();
    entry["t"] = it->second.title.c_str();
    entry["a"] = it->second.alt.c_str();
    entry["e"] = it->second.extension.c_str();
    entry["u"] = it->second.url.c_str();
  }
  JsonArray skippedArray = doc["skipped"].to<JsonArray>();
  for (const int number : g_skips) skippedArray.add(number);

  const String temporary = String(config::CACHE_INDEX) + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) return false;

  const size_t bytesWritten = serializeJson(doc, file);
  file.flush();
  file.close();
  if (bytesWritten == 0) {
    SD.remove(temporary);
    return false;
  }

  SD.remove(config::CACHE_INDEX);
  if (!SD.rename(temporary, config::CACHE_INDEX)) {
    SD.remove(temporary);
    return false;
  }
  // Older on-disk formats are now fully superseded; clean them up
  // so a downgrade never sees stale data.
  SD.remove(config::CACHE_INDEX_LEGACY_TXT);
  return true;
}

bool load() {
  clearAll();
  if (!SD.exists(config::CACHE_INDEX)) {
    LOG.println("[cache] comic manifest is missing");
    return false;
  }

  File file = SD.open(config::CACHE_INDEX, FILE_READ);
  if (!file) {
    LOG.println("[cache] comic manifest could not be opened");
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    LOG.printf("[cache] comic manifest JSON parse failed: %s\n", error.c_str());
    return false;
  }

  const uint32_t version = doc["version"].as<uint32_t>();
  if (version != config::CACHE_INDEX_VERSION) {
    LOG.printf("[cache] comic manifest is version %lu, expected %lu; "
               "re-run tools/preload_sd.py to upgrade\n",
               static_cast<unsigned long>(version),
               static_cast<unsigned long>(config::CACHE_INDEX_VERSION));
    return false;
  }

  JsonObjectConst comics = doc["comics"].as<JsonObjectConst>();
  if (comics.isNull()) {
    LOG.println("[cache] comic manifest has no comics object");
    return false;
  }
  if (comics.size() > config::MAX_CACHE_INDEX_ENTRIES) {
    LOG.println("[cache] comic manifest exceeds MAX_CACHE_INDEX_ENTRIES");
    return false;
  }

  std::vector<int> loadedNumbers;
  loadedNumbers.reserve(comics.size());
  std::unordered_map<int, ComicMeta> loadedMeta;
  loadedMeta.reserve(comics.size());
  for (JsonPairConst kv : comics) {
    // Parse the string key back to an int.
    const char* key = kv.key().c_str();
    if (key == nullptr || *key == '\0') return false;
    uint32_t parsedNumber = 0;
    if (!parseUnsignedDigits(key, strlen(key), parsedNumber, false) ||
        parsedNumber == 404) {
      LOG.printf("[cache] comic manifest has invalid key '%s'\n", key);
      return false;
    }
    const int number = static_cast<int>(parsedNumber);
    JsonObjectConst entry = kv.value().as<JsonObjectConst>();
    if (entry.isNull()) return false;
    ComicMeta meta;
    meta.title = String(entry["t"].as<const char*>() ?: "");
    meta.alt = String(entry["a"].as<const char*>() ?: "");
    meta.extension = String(entry["e"].as<const char*>() ?: "");
    meta.url = String(entry["u"].as<const char*>() ?: "");
    if (!meta.extension.isEmpty() && !extensionIsSupported(meta.extension)) {
      LOG.printf("[cache] comic manifest #%d has unsupported extension '%s'\n",
                 number, meta.extension.c_str());
      return false;
    }
    loadedNumbers.push_back(number);
    loadedMeta.emplace(number, std::move(meta));
  }
  std::sort(loadedNumbers.begin(), loadedNumbers.end());
  loadedNumbers.erase(std::unique(loadedNumbers.begin(), loadedNumbers.end()),
                      loadedNumbers.end());

  std::vector<int> loadedSkips;
  if (!decodeSkipArray(doc["skipped"].as<JsonArrayConst>(), loadedSkips)) {
    LOG.println("[cache] comic manifest has invalid skipped entries");
    return false;
  }

  const int loadedLatest = doc["latest"].as<int>();

  g_numbers.swap(loadedNumbers);
  g_meta.swap(loadedMeta);
  g_skips.swap(loadedSkips);
  g_latest = loadedLatest > 0 ? loadedLatest : 0;
  if (g_latest == 0 && !g_numbers.empty()) g_latest = g_numbers.back();
  g_ready = true;

  LOG.printf("[cache] loaded manifest: %lu comics, %lu skipped, latest #%d\n",
             static_cast<unsigned long>(g_numbers.size()),
             static_cast<unsigned long>(g_skips.size()), g_latest);
  return true;
}

bool rebuild(bool sdReady, ImageExistsFn imageExists,
             ShouldAbortFn shouldAbort) {
  if (!sdReady || imageExists == nullptr) return false;
  if (!g_ready || g_numbers.empty()) {
    // Nothing loaded — a v3 or older cache cannot be reconstructed
    // without per-comic metadata. Log actionable guidance instead
    // of pretending we succeeded.
    LOG.println("[cache] rebuild skipped: no manifest loaded; run "
                "tools/preload_sd.py");
    return false;
  }
  LOG.println("[cache] verifying manifest against SD images");

  std::vector<int> kept;
  kept.reserve(g_numbers.size());
  std::unordered_map<int, ComicMeta> keptMeta;
  keptMeta.reserve(g_numbers.size());
  size_t dropped = 0;
  for (const int number : g_numbers) {
    if (shouldAbort != nullptr && shouldAbort()) {
      LOG.println("[cache] manifest verify cancelled; keeping previous state");
      return false;
    }
    const auto it = g_meta.find(number);
    if (it == g_meta.end() || it->second.extension.isEmpty()) {
      ++dropped;
      continue;
    }
    if (!imageExists(number, it->second.extension)) {
      ++dropped;
      continue;
    }
    kept.push_back(number);
    keptMeta.emplace(number, it->second);
  }

  g_numbers.swap(kept);
  g_meta.swap(keptMeta);

  if (dropped > 0) {
    LOG.printf("[cache] manifest verify removed %lu stale entries\n",
               static_cast<unsigned long>(dropped));
  }
  const bool stored = persist();
  if (!stored) {
    LOG.println("[cache] manifest could not be re-persisted after verify");
  }
  return true;
}

bool markSkipped(int number) {
  if (number <= 0 || number == 404) return false;
  const auto position =
      std::lower_bound(g_skips.begin(), g_skips.end(), number);
  const bool alreadyPresent =
      position != g_skips.end() && *position == number;
  if (!alreadyPresent) {
    g_skips.insert(position, number);
    g_ready = true;
  }
  const bool stored = persist();
  if (!alreadyPresent) {
    LOG.printf("[cache] marked #%d as permanently unusable%s\n", number,
               stored ? "" : " (manifest write failed)");
  }
  return stored;
}

}  // namespace xkcd_index
