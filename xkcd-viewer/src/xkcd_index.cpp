#include "xkcd_index.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "app_logger.h"
#include "config.h"
#include "xkcd_index_pure.h"

namespace xkcd_index {
namespace {

// Stateless std allocator backed by PSRAM. Used for the g_metas
// vector (roughly 100 KB at full archive size) so its backing array
// doesn't compete with the tiny internal SRAM heap.
template <class T>
struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template <class U>
  PsramAllocator(const PsramAllocator<U>&) noexcept {}
  T* allocate(std::size_t n) {
    if (n == 0) n = 1;
    void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
      // No exceptions on Arduino; return null so the caller aborts loudly
      // rather than silently corrupting memory.
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

// Internal storage for a manifest entry. Every pointer aliases into a
// PSRAM char buffer owned by g_stringChunks; nothing here needs to be
// freed individually. Empty strings are stored as pointer to a shared
// "" literal so we never dereference null.
struct StoredMeta {
  const char* title;
  const char* alt;
  const char* extension;
  const char* url;
};

const char kEmpty[] = "";

// String storage backing g_metas. Element 0 is normally the
// stringArena filled during load() (a single PSRAM allocation into
// which every parsed title/alt/extension/url is copied). Subsequent
// chunks are per-string PSRAM allocations from addComic() at runtime.
std::vector<char*, PsramAllocator<char*>> g_stringChunks;

// g_numbers/g_skips are small (a few dozen KB at full archive size)
// and stay in internal SRAM so the existing `const std::vector<int>&`
// accessors work unchanged. g_metas holds pointers into g_stringChunks
// and lives in PSRAM because its backing array can grow to ~100 KB.
std::vector<int> g_numbers;
std::vector<int> g_skips;
std::vector<StoredMeta, PsramAllocator<StoredMeta>> g_metas;

// Scratch buffer returned by metadata(); populated on each call so
// the public API can keep returning `const ComicMeta*`. Valid until
// the next metadata() call.
ComicMeta g_metaScratch;
int g_latest = 0;
bool g_ready = false;

void freeStringChunks() {
  for (char* chunk : g_stringChunks) free(chunk);
  g_stringChunks.clear();
  g_stringChunks.shrink_to_fit();
}

// PSRAM strdup for runtime string data (addComic). Each call is one
// PSRAM allocation tracked in g_stringChunks. Returns kEmpty for a
// null/empty input so callers can store the pointer without a null
// check. Aborts on allocation failure — the app cannot function
// without PSRAM.
const char* dupString(const char* s) {
  if (s == nullptr || *s == '\0') return kEmpty;
  const size_t len = std::strlen(s) + 1;
  char* buf = static_cast<char*>(
      heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    LOG.println("[cache] PSRAM string allocation failed");
    abort();
  }
  std::memcpy(buf, s, len);
  g_stringChunks.push_back(buf);
  return buf;
}

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
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
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
  freeStringChunks();
  g_latest = 0;
  g_ready = false;
}

// Locate `number` in the sorted g_numbers vector. Returns
// g_numbers.end() on miss.
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
  g_metaScratch.title = stored.title;
  g_metaScratch.alt = stored.alt;
  g_metaScratch.extension = stored.extension;
  g_metaScratch.url = stored.url;
  return &g_metaScratch;
}

void addComic(int number, const ComicMeta& meta) {
  if (number <= 0 || number == 404) return;
  const auto position =
      std::lower_bound(g_numbers.begin(), g_numbers.end(), number);
  const size_t index = static_cast<size_t>(position - g_numbers.begin());
  StoredMeta stored;
  stored.title = dupString(meta.title.c_str());
  stored.alt = dupString(meta.alt.c_str());
  stored.extension = dupString(meta.extension.c_str());
  stored.url = dupString(meta.url.c_str());
  if (position == g_numbers.end() || *position != number) {
    g_numbers.insert(position, number);
    g_metas.insert(g_metas.begin() + index, stored);
  } else {
    g_metas[index] = stored;
  }
  if (number > g_latest) g_latest = number;
  g_ready = true;
}

namespace {

// Serialize one JSON object per line, streamed straight to the SD
// file. The whole file never exists as a single in-memory document,
// so persist() is O(1) memory in the number of comics.
bool writeJsonlHeader(File& file) {
  JsonDocument doc;
  doc["v"] = config::CACHE_INDEX_VERSION;
  if (g_latest > 0) doc["l"] = g_latest;
  JsonArray skipArray = doc["s"].to<JsonArray>();
  for (const int n : g_skips) skipArray.add(n);
  if (serializeJson(doc, file) == 0) return false;
  return file.print('\n') == 1;
}

bool writeJsonlComic(File& file, int number, const StoredMeta& stored) {
  JsonDocument doc;
  doc["n"] = number;
  doc["t"] = stored.title;
  doc["a"] = stored.alt;
  doc["e"] = stored.extension;
  doc["u"] = stored.url;
  if (serializeJson(doc, file) == 0) return false;
  return file.print('\n') == 1;
}

}  // namespace

bool persist() {
  const String temporary = String(config::CACHE_INDEX) + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) return false;

  bool ok = writeJsonlHeader(file);
  for (size_t i = 0; ok && i < g_numbers.size(); ++i) {
    ok = writeJsonlComic(file, g_numbers[i], g_metas[i]);
  }
  file.flush();
  file.close();
  if (!ok) {
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
  SD.remove(config::CACHE_INDEX_LEGACY_JSON);
  SD.remove(config::CACHE_INDEX_LEGACY_TXT);
  return true;
}

bool load() {
  const uint32_t t0 = millis();
  clearAll();
  // v4 .json (single-doc) is superseded; drop it so it never shadows
  // the new .jsonl file if the SD card still has both.
  SD.remove(config::CACHE_INDEX_LEGACY_JSON);
  if (!SD.exists(config::CACHE_INDEX)) {
    LOG.println("[cache] comic manifest is missing");
    return false;
  }

  File file = SD.open(config::CACHE_INDEX, FILE_READ);
  if (!file) {
    LOG.println("[cache] comic manifest could not be opened");
    return false;
  }

  // Slurp the whole file into a PSRAM buffer, then walk it line by
  // line. Bulk read is an order of magnitude faster than the byte-
  // at-a-time reads ArduinoJson does when parsing from a File, and
  // JSONL means we can hand ArduinoJson one tiny doc per line so the
  // parse tree never grows past a couple of hundred bytes.
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
  const uint32_t tSlurpStart = millis();
  const size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(buffer), fileSize);
  file.close();
  const uint32_t tSlurpEnd = millis();
  if (bytesRead != fileSize) {
    free(buffer);
    LOG.printf("[cache] comic manifest short read (%u of %u bytes)\n",
               static_cast<unsigned>(bytesRead),
               static_cast<unsigned>(fileSize));
    return false;
  }
  buffer[fileSize] = '\0';

  // ArduinoJson v7 keeps string values in its own StringPool (see
  // StringBuilder in the library), and JsonDocument::clear() releases
  // that pool. If we handed out `.as<const char*>()` pointers into
  // that pool and then reused the doc for the next line, every prior
  // pointer would dangle. So copy every string into a persistent
  // PSRAM arena as we go. Total unescaped string bytes cannot exceed
  // the raw JSON file size (each value in the file is at minimum
  // wrapped in two quote chars, more than the trailing '\0' we add),
  // so a single arena sized to fileSize+1 always fits.
  char* const stringArena = static_cast<char*>(
      heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (stringArena == nullptr) {
    free(buffer);
    LOG.println("[cache] comic manifest string arena cannot be allocated");
    return false;
  }
  size_t arenaPos = 0;
  const size_t arenaCapacity = fileSize + 1;
  const auto arenaDup = [&](const char* s) -> const char* {
    if (s == nullptr || *s == '\0') return kEmpty;
    const size_t len = std::strlen(s) + 1;
    if (arenaPos + len > arenaCapacity) return kEmpty;
    char* const dst = stringArena + arenaPos;
    std::memcpy(dst, s, len);
    arenaPos += len;
    return dst;
  };

  // Loaded state built into temporaries so a mid-parse failure leaves
  // any previously-swapped-in state untouched.
  std::vector<int> loadedNumbers;
  std::vector<StoredMeta, PsramAllocator<StoredMeta>> loadedMetas;
  std::vector<int> loadedSkips;
  int loadedLatest = 0;
  uint32_t loadedVersion = 0;
  bool sawHeader = false;
  size_t lineCount = 0;
  const uint32_t tParseStart = millis();

  // A tiny reusable doc: each line is at most a handful of fields, so
  // one small pool block covers it and .clear() resets it between
  // iterations without hitting the allocator again.
  JsonDocument line;

  size_t lineStart = 0;
  for (size_t i = 0; i <= fileSize; ++i) {
    if (i != fileSize && buffer[i] != '\n') continue;
    // Overwrite the newline (or the sentinel '\0' at fileSize) so the
    // slice becomes a null-terminated C string in place. Strings
    // ArduinoJson gives us are copied into stringArena below (v7 keeps
    // them in its own pool that .clear() releases between lines).
    buffer[i] = '\0';
    char* const linePtr = buffer + lineStart;
    lineStart = i + 1;
    // Blank lines (trailing newline, hand-edited files) are tolerated.
    if (*linePtr == '\0' || *linePtr == '\r') continue;
    ++lineCount;
    if (lineCount > config::MAX_CACHE_INDEX_ENTRIES + 8) {
      free(buffer);
      LOG.println("[cache] comic manifest exceeds MAX_CACHE_INDEX_ENTRIES");
      return false;
    }

    line.clear();
    const DeserializationError error = deserializeJson(line, linePtr);
    if (error) {
      free(buffer);
      free(stringArena);
      LOG.printf("[cache] comic manifest line %u parse failed: %s\n",
                 static_cast<unsigned>(lineCount), error.c_str());
      return false;
    }
    JsonObjectConst obj = line.as<JsonObjectConst>();
    if (obj.isNull()) {
      free(buffer);
      free(stringArena);
      LOG.printf("[cache] comic manifest line %u is not an object\n",
                 static_cast<unsigned>(lineCount));
      return false;
    }

    // Header line has no numeric "n". It carries version + latest + skips.
    if (!obj["n"].is<int>()) {
      if (sawHeader) {
        free(buffer);
        free(stringArena);
        LOG.println("[cache] comic manifest has more than one header line");
        return false;
      }
      sawHeader = true;
      loadedVersion = obj["v"].as<uint32_t>();
      if (loadedVersion != config::CACHE_INDEX_VERSION) {
        free(buffer);
        free(stringArena);
        LOG.printf("[cache] comic manifest is version %lu, expected %lu; "
                   "re-run tools/preload_sd.py to upgrade\n",
                   static_cast<unsigned long>(loadedVersion),
                   static_cast<unsigned long>(config::CACHE_INDEX_VERSION));
        return false;
      }
      loadedLatest = obj["l"].as<int>();
      if (!decodeSkipArray(obj["s"].as<JsonArrayConst>(), loadedSkips)) {
        free(buffer);
        free(stringArena);
        LOG.println("[cache] comic manifest has invalid skipped entries");
        return false;
      }
      continue;
    }

    // Comic line.
    if (!sawHeader) {
      free(buffer);
      free(stringArena);
      LOG.println("[cache] comic manifest is missing its header line");
      return false;
    }
    const int number = obj["n"].as<int>();
    if (number <= 0 || number == 404) {
      free(buffer);
      free(stringArena);
      LOG.printf("[cache] comic manifest line %u has invalid number %d\n",
                 static_cast<unsigned>(lineCount), number);
      return false;
    }
    const char* rawExt = obj["e"].as<const char*>();
    if (rawExt == nullptr) rawExt = kEmpty;
    if (*rawExt != '\0' && !extensionIsSupported(rawExt)) {
      free(buffer);
      free(stringArena);
      LOG.printf("[cache] comic manifest #%d has unsupported extension '%s'\n",
                 number, rawExt);
      return false;
    }
    StoredMeta stored;
    // Copy every string out of ArduinoJson's pool now, before the next
    // line.clear() releases it.
    stored.title = arenaDup(obj["t"].as<const char*>());
    stored.alt = arenaDup(obj["a"].as<const char*>());
    stored.extension = arenaDup(rawExt);
    stored.url = arenaDup(obj["u"].as<const char*>());
    loadedNumbers.push_back(number);
    loadedMetas.push_back(stored);
  }
  const uint32_t tParseEnd = millis();

  if (!sawHeader) {
    free(buffer);
    free(stringArena);
    LOG.println("[cache] comic manifest is empty");
    return false;
  }

  // The Python writer emits entries in numeric order, but sort defensively
  // so a hand-edited manifest still gives us the sorted invariant that
  // findNumber() / entries() consumers rely on.
  const uint32_t tSortStart = millis();
  if (!std::is_sorted(loadedNumbers.begin(), loadedNumbers.end())) {
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
        continue;
      }
      sortedNumbers.push_back(loadedNumbers[idx]);
      sortedMetas.push_back(loadedMetas[idx]);
    }
    loadedNumbers.swap(sortedNumbers);
    loadedMetas.swap(sortedMetas);
  }
  const uint32_t tSortEnd = millis();

  // Slurp buffer is no longer needed — every string lives in
  // stringArena now. Arena becomes the first (and usually only) chunk
  // in the string pool; every StoredMeta.* pointer aliases into it.
  free(buffer);
  g_stringChunks.push_back(stringArena);
  g_numbers.swap(loadedNumbers);
  g_metas.swap(loadedMetas);
  g_skips.swap(loadedSkips);
  g_latest = loadedLatest > 0 ? loadedLatest : 0;
  if (g_latest == 0 && !g_numbers.empty()) g_latest = g_numbers.back();
  g_ready = true;

  LOG.printf("[cache] loaded manifest: %lu comics, %lu skipped, latest #%d\n",
             static_cast<unsigned long>(g_numbers.size()),
             static_cast<unsigned long>(g_skips.size()), g_latest);
  LOG.printf("[cache] timings ms: slurp=%lu parse=%lu sort=%lu total=%lu\n",
             static_cast<unsigned long>(tSlurpEnd - tSlurpStart),
             static_cast<unsigned long>(tParseEnd - tParseStart),
             static_cast<unsigned long>(tSortEnd - tSortStart),
             static_cast<unsigned long>(millis() - t0));
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
    if (stored.extension == nullptr || *stored.extension == '\0') {
      ++dropped;
      continue;
    }
    if (!imageExists(g_numbers[i], String(stored.extension))) {
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
