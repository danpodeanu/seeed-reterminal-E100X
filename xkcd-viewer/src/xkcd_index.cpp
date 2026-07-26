#include "xkcd_index.h"

#include <Arduino.h>
#include <SD.h>

#include <algorithm>
#include <vector>

#include "app_logger.h"
#include "config.h"
#include "xkcd_index_pure.h"

namespace xkcd_index {
namespace {

std::vector<int> g_numbers;
std::vector<int> g_skips;
bool g_ready = false;

}  // namespace

bool ready() { return g_ready; }

uint32_t count() {
  return g_ready ? static_cast<uint32_t>(g_numbers.size()) : 0U;
}

const std::vector<int>& entries() { return g_numbers; }

const std::vector<int>& skips() { return g_skips; }

bool skipped(int number) {
  if (!g_ready || number <= 0) return false;
  return std::binary_search(g_skips.begin(), g_skips.end(), number);
}

bool parseUnsignedLine(String line, uint32_t& value, bool allowZero) {
  line.trim();
  return parseUnsignedDigits(line.c_str(), line.length(), value, allowZero);
}

bool writeFile(const std::vector<int>& cached,
               const std::vector<int>& skipped) {
  const String temporary = String(config::CACHE_INDEX) + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) return false;

  bool written = file.println(config::CACHE_INDEX_MAGIC) > 0 &&
                 file.println(cached.size()) > 0;
  for (const int number : cached) {
    if (!written || file.println(number) == 0) {
      written = false;
      break;
    }
  }
  if (written) written = file.println(skipped.size()) > 0;
  for (const int number : skipped) {
    if (!written || file.println(number) == 0) {
      written = false;
      break;
    }
  }
  file.flush();
  file.close();
  if (!written) {
    SD.remove(temporary);
    return false;
  }

  SD.remove(config::CACHE_INDEX);
  if (!SD.rename(temporary, config::CACHE_INDEX)) {
    SD.remove(temporary);
    return false;
  }
  return true;
}

bool persist() { return writeFile(g_numbers, g_skips); }

bool load() {
  g_ready = false;
  g_numbers.clear();
  g_skips.clear();
  if (!SD.exists(config::CACHE_INDEX)) {
    LOG.println("[cache] comic index is missing");
    return false;
  }

  File file = SD.open(config::CACHE_INDEX, FILE_READ);
  if (!file) {
    LOG.println("[cache] comic index could not be opened");
    return false;
  }

  String magic = file.readStringUntil('\n');
  magic.trim();
  uint32_t expectedCount = 0;
  const bool headerValid =
      magic == config::CACHE_INDEX_MAGIC &&
      parseUnsignedLine(file.readStringUntil('\n'), expectedCount, true) &&
      expectedCount <= config::MAX_CACHE_INDEX_ENTRIES;
  if (!headerValid) {
    file.close();
    LOG.println("[cache] comic index has an invalid header");
    return false;
  }

  std::vector<int> loaded;
  loaded.reserve(expectedCount);
  int previous = 0;
  for (uint32_t i = 0; i < expectedCount; ++i) {
    uint32_t number = 0;
    if (!file.available() ||
        !parseUnsignedLine(file.readStringUntil('\n'), number) ||
        number == 404 || static_cast<int>(number) <= previous) {
      file.close();
      LOG.println("[cache] comic index has invalid or unsorted entries");
      return false;
    }
    loaded.push_back(static_cast<int>(number));
    previous = static_cast<int>(number);
  }

  uint32_t skipCount = 0;
  if (!file.available() ||
      !parseUnsignedLine(file.readStringUntil('\n'), skipCount, true) ||
      skipCount > config::MAX_CACHE_INDEX_ENTRIES) {
    file.close();
    LOG.println("[cache] comic index has an invalid skip-section header");
    return false;
  }

  std::vector<int> loadedSkips;
  loadedSkips.reserve(skipCount);
  previous = 0;
  for (uint32_t i = 0; i < skipCount; ++i) {
    uint32_t number = 0;
    if (!file.available() ||
        !parseUnsignedLine(file.readStringUntil('\n'), number) ||
        number == 404 || static_cast<int>(number) <= previous) {
      file.close();
      LOG.println("[cache] comic index has invalid or unsorted skip entries");
      return false;
    }
    loadedSkips.push_back(static_cast<int>(number));
    previous = static_cast<int>(number);
  }

  while (file.available()) {
    const char trailing = static_cast<char>(file.read());
    if (trailing != '\r' && trailing != '\n' && trailing != ' ' &&
        trailing != '\t') {
      file.close();
      LOG.println("[cache] comic index contains unexpected trailing data");
      return false;
    }
  }
  file.close();

  g_numbers.swap(loaded);
  g_skips.swap(loadedSkips);
  g_ready = true;
  LOG.printf(
      "[cache] loaded comic index with %lu cached and %lu skipped entries\n",
      static_cast<unsigned long>(g_numbers.size()),
      static_cast<unsigned long>(g_skips.size()));
  return true;
}

bool rebuild(bool sdReady, ComicCachedFn isCached,
             ShouldAbortFn shouldAbort) {
  if (!sdReady || isCached == nullptr) return false;
  LOG.println("[cache] rebuilding comic index from SD");

  File directory = SD.open(config::CACHE_DIR);
  if (!directory || !directory.isDirectory()) {
    LOG.println(
        "[cache] comic index rebuild failed: cache directory unavailable");
    return false;
  }

  std::vector<int> rebuilt;
  File entry = directory.openNextFile();
  while (entry) {
    int candidate = 0;
    if (!entry.isDirectory()) {
      String name = entry.name();
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      const int dot = name.lastIndexOf('.');
      if (dot > 0) {
        String extension = name.substring(dot);
        extension.toLowerCase();
        if (extension == ".png" || extension == ".jpg" ||
            extension == ".jpeg" || extension == ".bmp") {
          const String numberText = name.substring(0, dot);
          bool numeric = !numberText.isEmpty();
          for (size_t i = 0; i < numberText.length(); ++i)
            numeric &= isDigit(numberText[i]);
          candidate = numeric ? numberText.toInt() : 0;
        }
      }
    }
    entry.close();

    if (candidate > 0 && candidate != 404 && isCached(candidate)) {
      rebuilt.push_back(candidate);
    }
    if (shouldAbort != nullptr && shouldAbort()) {
      directory.close();
      LOG.println(
          "[cache] comic index rebuild cancelled; keeping previous index");
      return false;
    }
    entry = directory.openNextFile();
  }
  directory.close();

  std::sort(rebuilt.begin(), rebuilt.end());
  rebuilt.erase(std::unique(rebuilt.begin(), rebuilt.end()), rebuilt.end());
  // Preserve any skip verdicts already in memory across the rebuild.
  // A rebuild triggered by load() failure enters with an empty skip
  // list, so skips restart empty in that case and are re-detected
  // lazily on the next attempt; a rebuild triggered mid-run keeps
  // what's been learned so far.
  const bool stored = writeFile(rebuilt, g_skips);
  g_numbers.swap(rebuilt);
  g_ready = true;
  if (stored) {
    LOG.printf("[cache] comic index rebuilt with %lu complete comics\n",
               static_cast<unsigned long>(g_numbers.size()));
  } else {
    LOG.printf(
        "[cache] comic index contains %lu comics in memory, "
        "but could not be stored\n",
        static_cast<unsigned long>(g_numbers.size()));
  }
  return true;
}

void addCurrent(int number) {
  if (!g_ready || number <= 0 || number == 404) return;
  const auto position =
      std::lower_bound(g_numbers.begin(), g_numbers.end(), number);
  if (position == g_numbers.end() || *position != number) {
    g_numbers.insert(position, number);
  }
}

bool markSkipped(int number) {
  if (!g_ready || number <= 0 || number == 404) return false;
  const auto position =
      std::lower_bound(g_skips.begin(), g_skips.end(), number);
  const bool alreadyPresent =
      position != g_skips.end() && *position == number;
  if (!alreadyPresent) {
    g_skips.insert(position, number);
  }
  // Persist immediately so the verdict survives an unplanned power
  // loss between now and the next scheduled persist(). The whole
  // index is small (thousands of decimal ints at worst) so the write
  // cost is negligible compared to re-detecting the same skip on
  // every subsequent wake.
  const bool stored = persist();
  if (!alreadyPresent) {
    LOG.printf("[cache] marked #%d as permanently unusable%s\n", number,
               stored ? "" : " (index write failed)");
  }
  return stored;
}

}  // namespace xkcd_index
