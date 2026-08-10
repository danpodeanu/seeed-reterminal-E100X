#pragma once

#include <stdint.h>

namespace game_progress {

enum class Status : uint8_t {
  Ok,
  InvalidArgument,
  InvalidStoredValue,
  OpenFailed,
  WriteFailed,
};

struct LoadResult {
  Status status;
  uint16_t checkpoint;
};

struct SaveResult {
  Status status;
  uint16_t checkpoint;
  bool changed;
};

struct HighScoreLoadResult {
  Status status;
  uint32_t score;
};

struct HighScoreSaveResult {
  Status status;
  uint32_t score;
  bool changed;
};

struct Advancement {
  bool valid;
  bool changed;
  uint16_t checkpoint;
};

struct HighScoreAdvancement {
  bool changed;
  uint32_t score;
};

constexpr Advancement evaluateAdvancement(uint16_t stored,
                                          uint16_t candidate,
                                          uint16_t checkpointCount) {
  if (checkpointCount == 0 || stored >= checkpointCount ||
      candidate >= checkpointCount) {
    return {false, false, 0};
  }
  return {true, candidate > stored,
          candidate > stored ? candidate : stored};
}

constexpr HighScoreAdvancement evaluateHighScore(uint32_t stored,
                                                uint32_t candidate) {
  return {candidate > stored, candidate > stored ? candidate : stored};
}

LoadResult loadHighestCheckpoint(const char* gameKey,
                                 uint16_t checkpointCount);
SaveResult saveHighestCheckpoint(const char* gameKey, uint16_t candidate,
                                 uint16_t checkpointCount);
HighScoreLoadResult loadHighScore(const char* gameKey);
HighScoreSaveResult saveHighScore(const char* gameKey, uint32_t candidate);
const char* statusMessage(Status status);

}  // namespace game_progress
