#include "game_progress_store.h"

#include <Preferences.h>

#include <cstring>

namespace game_progress {
namespace {

constexpr char kNamespace[] = "game_progress";
constexpr size_t kMaxNvsKeyLength = 15;

bool validKey(const char* gameKey) {
  return gameKey != nullptr && gameKey[0] != '\0' &&
         std::strlen(gameKey) <= kMaxNvsKeyLength;
}

}  // namespace

LoadResult loadHighestCheckpoint(const char* gameKey,
                                 uint16_t checkpointCount) {
  if (!validKey(gameKey) || checkpointCount == 0) {
    return {Status::InvalidArgument, 0};
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return {Status::OpenFailed, 0};
  }
  const uint16_t checkpoint = preferences.getUShort(gameKey, 0);
  preferences.end();
  if (checkpoint >= checkpointCount) {
    return {Status::InvalidStoredValue, 0};
  }
  return {Status::Ok, checkpoint};
}

SaveResult saveHighestCheckpoint(const char* gameKey, uint16_t candidate,
                                 uint16_t checkpointCount) {
  if (!validKey(gameKey) || checkpointCount == 0 ||
      candidate >= checkpointCount) {
    return {Status::InvalidArgument, 0, false};
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return {Status::OpenFailed, 0, false};
  }

  const uint16_t stored = preferences.getUShort(gameKey, 0);
  const Advancement advancement =
      evaluateAdvancement(stored, candidate, checkpointCount);
  if (advancement.valid && !advancement.changed) {
    preferences.end();
    return {Status::Ok, advancement.checkpoint, false};
  }

  const uint16_t checkpoint =
      advancement.valid ? advancement.checkpoint : candidate;
  const size_t bytesWritten = preferences.putUShort(gameKey, checkpoint);
  preferences.end();
  if (bytesWritten != sizeof(checkpoint)) {
    return {Status::WriteFailed, checkpoint, false};
  }
  return {Status::Ok, checkpoint, true};
}

const char* statusMessage(Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::InvalidArgument:
      return "invalid argument";
    case Status::InvalidStoredValue:
      return "invalid stored value";
    case Status::OpenFailed:
      return "could not open NVM";
    case Status::WriteFailed:
      return "could not write NVM";
  }
  return "unknown";
}

}  // namespace game_progress
