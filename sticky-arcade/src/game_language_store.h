#pragma once

#include <stdint.h>

#include "game_localization.h"

namespace game_language_store {

enum class Status : uint8_t {
  Ok,
  NotSelected,
  InvalidStoredValue,
  OpenFailed,
  WriteFailed,
};

struct LoadResult {
  Status status;
  game_localization::Language language;
};

LoadResult load();
Status save(game_localization::Language language);
const char* statusMessage(Status status);

}  // namespace game_language_store
