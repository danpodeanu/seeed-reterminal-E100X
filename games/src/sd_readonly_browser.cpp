#include "sd_readonly_browser.h"

#include <FS.h>

#include "sd_card.h"

namespace {

String baseName(const String& path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

}  // namespace

bool SdReadonlyBrowser::isEpub(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".epub");
}

bool SdReadonlyBrowser::precedes(const Entry& left, const Entry& right) {
  if (left.directory != right.directory) return left.directory;
  String leftName = left.name;
  String rightName = right.name;
  leftName.toLowerCase();
  rightName.toLowerCase();
  return leftName < rightName;
}

String SdReadonlyBrowser::parentPath(const String& path) {
  if (path.isEmpty() || path == "/") return "/";
  String normalized = path;
  while (normalized.length() > 1 && normalized.endsWith("/")) {
    normalized.remove(normalized.length() - 1);
  }
  const int slash = normalized.lastIndexOf('/');
  return slash <= 0 ? "/" : normalized.substring(0, slash);
}

bool SdReadonlyBrowser::open(const String& requestedPath) {
  File directory = sd_card::openForRead(requestedPath);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }

  path_ = requestedPath.isEmpty() ? "/" : requestedPath;
  count_ = 0;
  truncated_ = false;
  for (Entry& entry : entries_) entry = {};

  File file = directory.openNextFile();
  while (file) {
    const String rawName = file.name();
    const String name = baseName(rawName);
    if (!name.isEmpty() && name != "." && name != "..") {
      if (count_ >= kMaximumEntries) {
        truncated_ = true;
        file.close();
        break;
      } else {
        Entry& entry = entries_[count_++];
        entry.name = name;
        entry.path =
            path_ == "/" ? String("/") + name : path_ + "/" + name;
        entry.size = file.size();
        entry.directory = file.isDirectory();
        entry.epub = !entry.directory && isEpub(name);
      }
    }
    file.close();
    file = directory.openNextFile();
  }
  directory.close();

  for (int index = 1; index < count_; ++index) {
    Entry value = entries_[index];
    int insertion = index;
    while (insertion > 0 && precedes(value, entries_[insertion - 1])) {
      entries_[insertion] = entries_[insertion - 1];
      --insertion;
    }
    entries_[insertion] = value;
  }
  return true;
}
