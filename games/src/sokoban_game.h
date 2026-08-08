#pragma once

#include <stdint.h>

class SokobanGame {
 public:
  static constexpr int kSize = 7;
  static constexpr int kMaxSize = 7;
  static constexpr int kMaxCells = kMaxSize * kMaxSize;
  static constexpr uint8_t kLevelCount = 5;

  enum class Direction : uint8_t {
    Up = 0,
    Right = 1,
    Down = 2,
    Left = 3,
  };

  enum class Cell : uint8_t {
    Outside = 0,
    Wall = 1,
    Floor = 2,
    Target = 3,
    Box = 4,
    BoxOnTarget = 5,
    Player = 6,
    PlayerOnTarget = 7,
  };

  struct Snapshot {
    uint64_t boxes;
    uint16_t moves;
    uint16_t pushes;
    uint8_t levelIndex;
    uint8_t playerIndex;
  };

  SokobanGame() { loadLevel(0); }

  void start(uint8_t levelIndex = 0) {
    loadLevel(levelIndex < kLevelCount ? levelIndex : 0);
  }

  bool move(Direction direction) {
    if (solved()) return false;

    const LevelDef& level = kLevels[levelIndex_];
    const int row = playerIndex_ / level.width;
    const int column = playerIndex_ % level.width;
    const int directionIndex = static_cast<int>(direction);
    const int nextRow = row + kRowOffsets[directionIndex];
    const int nextColumn = column + kColumnOffsets[directionIndex];
    if (!validCell(level, nextRow, nextColumn)) return false;

    const uint8_t nextIndex =
        static_cast<uint8_t>(nextRow * level.width + nextColumn);
    const uint64_t nextBit = cellBit(nextIndex);
    if ((level.walls & nextBit) != 0) return false;

    if ((boxes_ & nextBit) != 0) {
      const int pushRow = nextRow + kRowOffsets[directionIndex];
      const int pushColumn = nextColumn + kColumnOffsets[directionIndex];
      if (!validCell(level, pushRow, pushColumn)) return false;

      const uint8_t pushIndex =
          static_cast<uint8_t>(pushRow * level.width + pushColumn);
      const uint64_t pushBit = cellBit(pushIndex);
      if ((level.walls & pushBit) != 0 || (boxes_ & pushBit) != 0) return false;

      boxes_ = (boxes_ & ~nextBit) | pushBit;
      playerIndex_ = nextIndex;
      saturatingIncrement(moveCount_);
      saturatingIncrement(pushCount_);
      return true;
    }

    playerIndex_ = nextIndex;
    saturatingIncrement(moveCount_);
    return true;
  }

  void reset() { loadLevel(levelIndex_); }

  void nextLevel() {
    loadLevel(static_cast<uint8_t>((levelIndex_ + 1U) % kLevelCount));
  }

  uint8_t levelIndex() const { return levelIndex_; }
  static constexpr uint8_t levelCount() { return kLevelCount; }

  uint8_t width() const { return kLevels[levelIndex_].width; }
  uint8_t height() const { return kLevels[levelIndex_].height; }
  int playerRow() const { return playerIndex_ / kLevels[levelIndex_].width; }
  int playerColumn() const { return playerIndex_ % kLevels[levelIndex_].width; }

  bool isWall(int row, int column) const {
    const LevelDef& level = kLevels[levelIndex_];
    if (!validCell(level, row, column)) return false;
    return (level.walls & cellBit(static_cast<uint8_t>(row * level.width +
                                                       column))) != 0;
  }

  bool isTarget(int row, int column) const {
    const LevelDef& level = kLevels[levelIndex_];
    if (!validCell(level, row, column)) return false;
    return (level.targets & cellBit(static_cast<uint8_t>(row * level.width +
                                                         column))) != 0;
  }

  bool hasBox(int row, int column) const {
    const LevelDef& level = kLevels[levelIndex_];
    if (!validCell(level, row, column)) return false;
    return (boxes_ & cellBit(static_cast<uint8_t>(row * level.width + column))) !=
           0;
  }

  Cell cellAt(int row, int column) const {
    const LevelDef& level = kLevels[levelIndex_];
    if (!validCell(level, row, column)) return Cell::Outside;

    const uint8_t index = static_cast<uint8_t>(row * level.width + column);
    const uint64_t bit = cellBit(index);
    if ((level.walls & bit) != 0) return Cell::Wall;

    const bool target = (level.targets & bit) != 0;
    const bool box = (boxes_ & bit) != 0;
    const bool player = playerIndex_ == index;
    if (player) return target ? Cell::PlayerOnTarget : Cell::Player;
    if (box) return target ? Cell::BoxOnTarget : Cell::Box;
    return target ? Cell::Target : Cell::Floor;
  }

  uint16_t moveCount() const { return moveCount_; }
  uint16_t pushCount() const { return pushCount_; }

  bool solved() const { return boxes_ == kLevels[levelIndex_].targets; }

  Snapshot snapshot() const {
    return {boxes_, moveCount_, pushCount_, levelIndex_, playerIndex_};
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.levelIndex >= kLevelCount) return false;

    const LevelDef& level = kLevels[snapshot.levelIndex];
    if (!validateLevel(level) || snapshot.pushes > snapshot.moves ||
        !validateDynamicState(level, snapshot.boxes, snapshot.playerIndex)) {
      return false;
    }

    boxes_ = snapshot.boxes;
    moveCount_ = snapshot.moves;
    pushCount_ = snapshot.pushes;
    levelIndex_ = snapshot.levelIndex;
    playerIndex_ = snapshot.playerIndex;
    return true;
  }

 private:
  struct LevelDef {
    uint64_t walls;
    uint64_t targets;
    uint64_t boxes;
    uint8_t width;
    uint8_t height;
    uint8_t player;
  };

  inline static constexpr int8_t kRowOffsets[4] = {-1, 0, 1, 0};
  inline static constexpr int8_t kColumnOffsets[4] = {0, 1, 0, -1};
  inline static constexpr LevelDef kLevels[kLevelCount] = {
      {0x1F8C63FULL, 0x80ULL, 0x1000ULL, 5, 5, 17},
      {0xFE186187FULL, 0x4000080ULL, 0x204000ULL, 6, 6, 9},
      {0x1FE4C983064FFULL, 0x1000000000ULL, 0x20000ULL, 7, 7, 12},
      {0x1FE0C5932E0FFULL, 0x1000000400ULL, 0x100040000ULL, 7, 7, 37},
      {0xFE186187FULL, 0x2000500ULL, 0x602000ULL, 6, 6, 7},
  };

  uint64_t boxes_ = 0;
  uint16_t moveCount_ = 0;
  uint16_t pushCount_ = 0;
  uint8_t levelIndex_ = 0;
  uint8_t playerIndex_ = 0;

  static uint64_t boardMask(const LevelDef& level) {
    return (1ULL << (level.width * level.height)) - 1ULL;
  }

  static uint64_t cellBit(uint8_t index) { return 1ULL << index; }

  static bool validCell(const LevelDef& level, int row, int column) {
    return row >= 0 && row < level.height && column >= 0 &&
           column < level.width;
  }

  static int bitCount(uint64_t value) {
    int count = 0;
    while (value != 0) {
      count += static_cast<int>(value & 1ULL);
      value >>= 1;
    }
    return count;
  }

  static void saturatingIncrement(uint16_t& value) {
    if (value != 0xFFFFU) ++value;
  }

  static bool validateDynamicState(const LevelDef& level, uint64_t boxes,
                                   uint8_t player) {
    const int cellCount = level.width * level.height;
    const uint64_t validBits = boardMask(level);
    const uint64_t playerBit = player < cellCount ? cellBit(player) : 0ULL;
    if (player >= cellCount || (boxes & ~validBits) != 0 ||
        (boxes & level.walls) != 0 || playerBit == 0 ||
        (playerBit & level.walls) != 0 || (playerBit & boxes) != 0) {
      return false;
    }
    return bitCount(boxes) == bitCount(level.targets);
  }

  static bool validateLevel(const LevelDef& level) {
    if (level.width == 0 || level.width > kMaxSize || level.height == 0 ||
        level.height > kMaxSize) {
      return false;
    }

    const uint64_t validBits = boardMask(level);
    if ((level.walls & ~validBits) != 0 || (level.targets & ~validBits) != 0 ||
        (level.boxes & ~validBits) != 0 || (level.targets & level.walls) != 0 ||
        bitCount(level.targets) == 0 ||
        bitCount(level.targets) != bitCount(level.boxes)) {
      return false;
    }
    return validateDynamicState(level, level.boxes, level.player);
  }

  void loadLevel(uint8_t levelIndex) {
    const uint8_t validLevelIndex = levelIndex < kLevelCount ? levelIndex : 0;
    const LevelDef& level = kLevels[validLevelIndex];
    levelIndex_ = validateLevel(level) ? validLevelIndex : 0;
    boxes_ = kLevels[levelIndex_].boxes;
    playerIndex_ = kLevels[levelIndex_].player;
    moveCount_ = 0;
    pushCount_ = 0;
  }
};
