#include "game_language_store.h"

#include <Preferences.h>

namespace game_language_store {
namespace {

constexpr char kNamespace[] = "games_config";
constexpr char kLanguageKey[] = "language";

}  // namespace

LoadResult load() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    // ESP Preferences refuses a read-only open until the namespace exists.
    if (!preferences.begin(kNamespace, false)) {
      return {Status::OpenFailed, game_localization::Language::English};
    }
  }
  if (!preferences.isKey(kLanguageKey)) {
    preferences.end();
    return {Status::NotSelected, game_localization::Language::English};
  }

  const uint8_t stored = preferences.getUChar(
      kLanguageKey, static_cast<uint8_t>(game_localization::Language::Count));
  preferences.end();
  if (!game_localization::validLanguageValue(stored)) {
    return {Status::InvalidStoredValue,
            game_localization::Language::English};
  }
  return {Status::Ok, static_cast<game_localization::Language>(stored)};
}

Status save(game_localization::Language language) {
  const uint8_t value = static_cast<uint8_t>(language);
  if (!game_localization::validLanguageValue(value)) {
    return Status::InvalidStoredValue;
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return Status::OpenFailed;
  const size_t written = preferences.putUChar(kLanguageKey, value);
  preferences.end();
  return written == sizeof(value) ? Status::Ok : Status::WriteFailed;
}

const char* statusMessage(Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::NotSelected:
      return "not selected";
    case Status::InvalidStoredValue:
      return "invalid stored value";
    case Status::OpenFailed:
      return "could not open NVM";
    case Status::WriteFailed:
      return "could not write NVM";
  }
  return "unknown";
}

}  // namespace game_language_store
