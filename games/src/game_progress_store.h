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

struct Advancement {
  bool valid;
  bool changed;
  uint16_t checkpoint;
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

LoadResult loadHighestCheckpoint(const char* gameKey,
                                 uint16_t checkpointCount);
SaveResult saveHighestCheckpoint(const char* gameKey, uint16_t candidate,
                                 uint16_t checkpointCount);
const char* statusMessage(Status status);

}  // namespace game_progress
