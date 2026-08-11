#pragma once

namespace epub_browser_logic {

constexpr bool hasParentFolder(const char* path) {
  return path != nullptr && path[0] == '/' && path[1] != '\0';
}

constexpr int itemCount(int storedEntryCount, bool includeParentFolder) {
  return (storedEntryCount > 0 ? storedEntryCount : 0) +
         (includeParentFolder ? 1 : 0);
}

constexpr bool isParentFolderItem(int itemIndex, bool includeParentFolder) {
  return includeParentFolder && itemIndex == 0;
}

constexpr int storedEntryIndex(int itemIndex, bool includeParentFolder) {
  if (itemIndex < 0 || isParentFolderItem(itemIndex, includeParentFolder)) {
    return -1;
  }
  return itemIndex - (includeParentFolder ? 1 : 0);
}

}  // namespace epub_browser_logic
