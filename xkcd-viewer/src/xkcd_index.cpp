#include "xkcd_index.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "app_logger.h"
#include "config.h"
#include "xkcd_index_pure.h"

namespace xkcd_index {
namespace {

// Stateless std allocator backed by PSRAM. Every allocation goes through
// heap_caps_malloc(MALLOC_CAP_SPIRAM) so 3000+ per-comic strings can
// live outside the ~300 KB internal SRAM heap.
template <class T>
struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template <class U>
  PsramAllocator(const PsramAllocator<U>&) noexcept {}
  T* allocate(std::size_t n) {
    void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
      // No exceptions on Arduino; a null return from the allocator is
      // undefined behaviour, so fail loudly instead of corrupting state.
      LOG.println("[cache] PSRAM allocation failed");
      abort();
    }
    return static_cast<T*>(p);
  }
  void deallocate(T* p, std::size_t) noexcept { free(p); }
};
template <class T, class U>
bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return true; }
template <class T, class U>
bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return false; }

// A `std::string` whose buffer lives in PSRAM.
using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

// PSRAM-backed ArduinoJson allocator. Assigns the ~1.5–2 MB parse tree
// away from the tiny internal SRAM heap.
class PsramJsonAllocator : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t size) override {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  void deallocate(void* ptr) override { free(ptr); }
  void* reallocate(void* ptr, size_t new_size) override {
    return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
};

// Internal storage for a manifest entry — mirrors the public ComicMeta
// layout but with PSRAM-backed strings so 3000+ entries don't blow the
// internal SRAM heap. metadata() converts one entry to the public form
// on demand.
struct StoredMeta {
  PsString title;
  PsString alt;
  PsString extension;
  PsString url;
};

// g_numbers/g_skips are small (~13 KB and ~30 bytes at 3268 comics) and
// stay in internal SRAM so the existing `const std::vector<int>&`
// accessors work unchanged. g_metas is the large one and lives in
// PSRAM: both the vector's node array and every PsString buffer inside
// it are PSRAM allocations.
std::vector<int> g_numbers;
std::vector<int> g_skips;
std::vector<StoredMeta, PsramAllocator<StoredMeta>> g_metas;
// Scratch buffer returned by metadata(); populated on each call so the
// public API can keep returning `const ComicMeta*`. Valid until the
// next metadata() call.
ComicMeta g_metaScratch;
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

bool extensionIsSupported(const char* extension) {
  return std::strcmp(extension, ".png") == 0 ||
         std::strcmp(extension, ".jpg") == 0 ||
         std::strcmp(extension, ".jpeg") == 0 ||
         std::strcmp(extension, ".bmp") == 0;
}

void clearAll() {
  g_numbers.clear();
  g_skips.clear();
  g_metas.clear();
  g_metas.shrink_to_fit();
  g_latest = 0;
  g_ready = false;
}

// Locate `number` in the sorted g_numbers vector. Returns g_numbers.end()
// on miss.
std::vector<int>::const_iterator findNumber(int number) {
  const auto it = std::lower_bound(g_numbers.begin(), g_numbers.end(), number);
  if (it == g_numbers.end() || *it != number) return g_numbers.end();
  return it;
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
  const auto it = findNumber(number);
  if (it == g_numbers.end()) return nullptr;
  const StoredMeta& stored = g_metas[static_cast<size_t>(it - g_numbers.begin())];
  // Copy PSRAM strings into the Arduino String scratch. Each field is
  // short (a title/alt line) so the assignment cost is trivial, and
  // the caller sees a stable API.
  g_metaScratch.title = stored.title.c_str();
  g_metaScratch.alt = stored.alt.c_str();
  g_metaScratch.extension = stored.extension.c_str();
  g_metaScratch.url = stored.url.c_str();
  return &g_metaScratch;
}

void addComic(int number, const ComicMeta& meta) {
  if (number <= 0 || number == 404) return;
  const auto position =
      std::lower_bound(g_numbers.begin(), g_numbers.end(), number);
  const size_t index = static_cast<size_t>(position - g_numbers.begin());
  StoredMeta stored;
  stored.title = meta.title.c_str();
  stored.alt = meta.alt.c_str();
  stored.extension = meta.extension.c_str();
  stored.url = meta.url.c_str();
  if (position == g_numbers.end() || *position != number) {
    g_numbers.insert(position, number);
    g_metas.insert(g_metas.begin() + index, std::move(stored));
  } else {
    g_metas[index] = std::move(stored);
  }
  if (number > g_latest) g_latest = number;
  g_ready = true;
}

bool persist() {
  PsramJsonAllocator alloc;
  JsonDocument doc(&alloc);
  doc["version"] = config::CACHE_INDEX_VERSION;
  if (g_latest > 0) doc["latest"] = g_latest;
  JsonObject comics = doc["comics"].to<JsonObject>();
  for (size_t i = 0; i < g_numbers.size(); ++i) {
    const int number = g_numbers[i];
    const StoredMeta& stored = g_metas[i];
    // Numeric ArduinoJson keys must be strings, so key by the decimal
    // representation.
    char keyBuf[12];
    snprintf(keyBuf, sizeof(keyBuf), "%d", number);
    JsonObject entry = comics[keyBuf].to<JsonObject>();
    entry["t"] = stored.title.c_str();
    entry["a"] = stored.alt.c_str();
    entry["e"] = stored.extension.c_str();
    entry["u"] = stored.url.c_str();
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

  // ArduinoJson streaming from a File reads a byte (or a handful) at a
  // time, which costs seconds on a 1 MB manifest sitting on FAT32.
  // Slurp the whole file into PSRAM first and parse from memory — one
  // bulk read plus one in-memory parse is an order of magnitude faster.
  const size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > 8U * 1024U * 1024U) {
    file.close();
    LOG.printf("[cache] comic manifest has an unreasonable size (%u bytes)\n",
               static_cast<unsigned>(fileSize));
    return false;
  }
  char* const buffer = static_cast<char*>(
      heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    file.close();
    LOG.println("[cache] comic manifest cannot be buffered in PSRAM");
    return false;
  }
  const size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(buffer), fileSize);
  file.close();
  if (bytesRead != fileSize) {
    free(buffer);
    LOG.printf("[cache] comic manifest short read (%u of %u bytes)\n",
               static_cast<unsigned>(bytesRead),
               static_cast<unsigned>(fileSize));
    return false;
  }
  buffer[fileSize] = '\0';

  PsramJsonAllocator jsonAlloc;
  JsonDocument doc(&jsonAlloc);
  const DeserializationError error = deserializeJson(doc, buffer, fileSize);
  free(buffer);
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

  // Build the two loaded views in temporaries so a mid-parse failure
  // leaves the previously-swapped-in state untouched.
  std::vector<int> loadedNumbers;
  loadedNumbers.reserve(comics.size());
  std::vector<StoredMeta, PsramAllocator<StoredMeta>> loadedMetas;
  loadedMetas.reserve(comics.size());
  for (JsonPairConst kv : comics) {
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
    const char* rawExt = entry["e"].as<const char*>();
    if (rawExt == nullptr) rawExt = "";
    if (*rawExt != '\0' && !extensionIsSupported(rawExt)) {
      LOG.printf("[cache] comic manifest #%d has unsupported extension '%s'\n",
                 number, rawExt);
      return false;
    }
    StoredMeta stored;
    const char* rawTitle = entry["t"].as<const char*>();
    const char* rawAlt = entry["a"].as<const char*>();
    const char* rawUrl = entry["u"].as<const char*>();
    stored.title = rawTitle == nullptr ? "" : rawTitle;
    stored.alt = rawAlt == nullptr ? "" : rawAlt;
    stored.extension = rawExt;
    stored.url = rawUrl == nullptr ? "" : rawUrl;
    loadedNumbers.push_back(number);
    loadedMetas.push_back(std::move(stored));
  }
  // The JSON object preserves insertion order, but callers rely on
  // sorted numbers for binary lookup. Sort both vectors together.
  {
    std::vector<size_t> perm(loadedNumbers.size());
    for (size_t i = 0; i < perm.size(); ++i) perm[i] = i;
    std::sort(perm.begin(), perm.end(),
              [&](size_t a, size_t b) { return loadedNumbers[a] < loadedNumbers[b]; });
    std::vector<int> sortedNumbers;
    sortedNumbers.reserve(loadedNumbers.size());
    std::vector<StoredMeta, PsramAllocator<StoredMeta>> sortedMetas;
    sortedMetas.reserve(loadedMetas.size());
    for (const size_t idx : perm) {
      if (!sortedNumbers.empty() && sortedNumbers.back() == loadedNumbers[idx]) {
        // Duplicate keys should never happen in a well-formed manifest;
        // keep the first occurrence and skip subsequent copies.
        continue;
      }
      sortedNumbers.push_back(loadedNumbers[idx]);
      sortedMetas.push_back(std::move(loadedMetas[idx]));
    }
    loadedNumbers.swap(sortedNumbers);
    loadedMetas.swap(sortedMetas);
  }

  std::vector<int> loadedSkips;
  if (!decodeSkipArray(doc["skipped"].as<JsonArrayConst>(), loadedSkips)) {
    LOG.println("[cache] comic manifest has invalid skipped entries");
    return false;
  }

  const int loadedLatest = doc["latest"].as<int>();

  g_numbers.swap(loadedNumbers);
  g_metas.swap(loadedMetas);
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
    LOG.println("[cache] rebuild skipped: no manifest loaded; run "
                "tools/preload_sd.py");
    return false;
  }
  LOG.println("[cache] verifying manifest against SD images");

  std::vector<int> kept;
  kept.reserve(g_numbers.size());
  std::vector<StoredMeta, PsramAllocator<StoredMeta>> keptMetas;
  keptMetas.reserve(g_numbers.size());
  size_t dropped = 0;
  for (size_t i = 0; i < g_numbers.size(); ++i) {
    if (shouldAbort != nullptr && shouldAbort()) {
      LOG.println("[cache] manifest verify cancelled; keeping previous state");
      return false;
    }
    const StoredMeta& stored = g_metas[i];
    if (stored.extension.empty()) {
      ++dropped;
      continue;
    }
    if (!imageExists(g_numbers[i], String(stored.extension.c_str()))) {
      ++dropped;
      continue;
    }
    kept.push_back(g_numbers[i]);
    keptMetas.push_back(stored);
  }

  g_numbers.swap(kept);
  g_metas.swap(keptMetas);

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
