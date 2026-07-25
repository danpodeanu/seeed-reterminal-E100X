#include "xkcd_index.h"

#include <Arduino.h>
#include <SD.h>

#include <algorithm>
#include <vector>

#include "app_logger.h"
#include "config.h"

namespace xkcd_index {
namespace {

std::vector<int> g_numbers;
bool g_ready = false;

}  // namespace

bool ready() { return g_ready; }

uint32_t count() {
  return g_ready ? static_cast<uint32_t>(g_numbers.size()) : 0U;
}

const std::vector<int>& entries() { return g_numbers; }

bool parseUnsignedLine(String line, uint32_t& value, bool allowZero) {
  line.trim();
  if (line.isEmpty() || line.length() > 10) return false;
  uint64_t parsed = 0;
  for (size_t i = 0; i < line.length(); ++i) {
    if (!isDigit(line[i])) return false;
    parsed = parsed * 10U + static_cast<uint8_t>(line[i] - '0');
    if (parsed > 100000U) return false;
  }
  if (!allowZero && parsed == 0) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool writeFile(const std::vector<int>& numbers) {
  const String temporary = String(config::CACHE_INDEX) + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) return false;

  bool written = file.println(config::CACHE_INDEX_MAGIC) > 0 &&
                 file.println(numbers.size()) > 0;
  for (const int number : numbers) {
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

bool persist() { return writeFile(g_numbers); }

bool load() {
  g_ready = false;
  g_numbers.clear();
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
  g_ready = true;
  LOG.printf("[cache] loaded comic index with %lu entries\n",
             static_cast<unsigned long>(g_numbers.size()));
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
  const bool stored = writeFile(rebuilt);
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

void pack4bppInPlace(uint8_t* indices, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* source = indices + static_cast<size_t>(y) * width;
    uint8_t* destination =
        indices + static_cast<size_t>(y) * (width / 2);
    for (int x = 0; x < width; x += 2) {
      destination[x / 2] = static_cast<uint8_t>(
          ((source[x] & 0x0F) << 4) | (source[x + 1] & 0x0F));
    }
  }
}

}  // namespace xkcd_index
