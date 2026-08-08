#pragma once

#include <stdint.h>

#include "microban_levels.h"

class SokobanGame {
 public:
  static constexpr int kMaxWidth = 30;
  static constexpr int kMaxHeight = 17;
  static constexpr int kMaxCells = kMaxWidth * kMaxHeight;
  static constexpr int kWordCount = (kMaxCells + 63) / 64;
  static constexpr int kSize = kMaxWidth;
  static constexpr uint16_t kLevelCount = microban_levels::kLevelCount;
  static constexpr uint16_t kInvalidCell = 0xFFFFU;

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
    uint64_t boxes[kWordCount];
    uint16_t moves;
    uint16_t pushes;
    uint16_t levelIndex;
    uint16_t playerIndex;
  };

  SokobanGame() { loadLevel(0); }

  void start(uint16_t levelIndex = 0) {
    loadLevel(levelIndex < kLevelCount ? levelIndex : 0);
  }

  bool move(Direction direction) {
    if (!levelValid_ || solved()) return false;
    const int directionIndex = static_cast<int>(direction);
    if (directionIndex < 0 || directionIndex >= 4) return false;

    const int row = playerIndex_ / width_;
    const int column = playerIndex_ % width_;
    const int nextRow = row + kRowOffsets[directionIndex];
    const int nextColumn = column + kColumnOffsets[directionIndex];
    if (!isPlayable(nextRow, nextColumn)) return false;

    const uint16_t nextIndex =
        static_cast<uint16_t>(nextRow * width_ + nextColumn);
    if (bitAt(boxes_, nextIndex)) {
      const int pushRow = nextRow + kRowOffsets[directionIndex];
      const int pushColumn = nextColumn + kColumnOffsets[directionIndex];
      if (!isPlayable(pushRow, pushColumn)) return false;

      const uint16_t pushIndex =
          static_cast<uint16_t>(pushRow * width_ + pushColumn);
      if (bitAt(boxes_, pushIndex)) return false;
      clearBit(boxes_, nextIndex);
      setBit(boxes_, pushIndex);
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
    loadLevel(static_cast<uint16_t>((levelIndex_ + 1U) % kLevelCount));
  }

  uint16_t levelIndex() const { return levelIndex_; }
  static constexpr uint16_t levelCount() { return kLevelCount; }

  uint8_t width() const { return width_; }
  uint8_t height() const { return height_; }
  int playerRow() const { return playerIndex_ / width_; }
  int playerColumn() const { return playerIndex_ % width_; }
  bool valid() const { return levelValid_; }

  bool isPlayable(int row, int column) const {
    return validCell(row, column) &&
           bitAt(playable_, static_cast<uint16_t>(row * width_ + column));
  }

  bool isWall(int row, int column) const {
    return validCell(row, column) &&
           bitAt(walls_, static_cast<uint16_t>(row * width_ + column));
  }

  bool isTarget(int row, int column) const {
    return validCell(row, column) &&
           bitAt(targets_, static_cast<uint16_t>(row * width_ + column));
  }

  bool hasBox(int row, int column) const {
    return validCell(row, column) &&
           bitAt(boxes_, static_cast<uint16_t>(row * width_ + column));
  }

  Cell cellAt(int row, int column) const {
    if (!validCell(row, column) || !isPlayable(row, column)) {
      return isWall(row, column) ? Cell::Wall : Cell::Outside;
    }

    const uint16_t index = static_cast<uint16_t>(row * width_ + column);
    const bool target = bitAt(targets_, index);
    const bool box = bitAt(boxes_, index);
    const bool player = playerIndex_ == index;
    if (player) return target ? Cell::PlayerOnTarget : Cell::Player;
    if (box) return target ? Cell::BoxOnTarget : Cell::Box;
    return target ? Cell::Target : Cell::Floor;
  }

  uint16_t moveCount() const { return moveCount_; }
  uint16_t pushCount() const { return pushCount_; }

  bool solved() const {
    return levelValid_ && equalBits(boxes_, targets_);
  }

  Snapshot snapshot() const {
    Snapshot result = {};
    copyBits(result.boxes, boxes_);
    result.moves = moveCount_;
    result.pushes = pushCount_;
    result.levelIndex = levelIndex_;
    result.playerIndex = playerIndex_;
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.levelIndex >= kLevelCount ||
        snapshot.pushes > snapshot.moves) {
      return false;
    }

    SokobanGame restored;
    restored.loadLevel(snapshot.levelIndex);
    if (!restored.levelValid_ ||
        !restored.validateDynamicState(snapshot.boxes,
                                       snapshot.playerIndex)) {
      return false;
    }
    copyBits(restored.boxes_, snapshot.boxes);
    restored.moveCount_ = snapshot.moves;
    restored.pushCount_ = snapshot.pushes;
    restored.playerIndex_ = snapshot.playerIndex;
    *this = restored;
    return true;
  }

 private:
  inline static constexpr int8_t kRowOffsets[4] = {-1, 0, 1, 0};
  inline static constexpr int8_t kColumnOffsets[4] = {0, 1, 0, -1};

  uint64_t walls_[kWordCount] = {};
  uint64_t targets_[kWordCount] = {};
  uint64_t playable_[kWordCount] = {};
  uint64_t boxes_[kWordCount] = {};
  uint16_t moveCount_ = 0;
  uint16_t pushCount_ = 0;
  uint16_t levelIndex_ = 0;
  uint16_t playerIndex_ = kInvalidCell;
  uint8_t width_ = 0;
  uint8_t height_ = 0;
  bool levelValid_ = false;

  bool validCell(int row, int column) const {
    return row >= 0 && row < height_ && column >= 0 && column < width_;
  }

  static bool bitAt(const uint64_t (&bits)[kWordCount], uint16_t index) {
    return (bits[index / 64] & (1ULL << (index % 64))) != 0;
  }

  static void setBit(uint64_t (&bits)[kWordCount], uint16_t index) {
    bits[index / 64] |= 1ULL << (index % 64);
  }

  static void clearBit(uint64_t (&bits)[kWordCount], uint16_t index) {
    bits[index / 64] &= ~(1ULL << (index % 64));
  }

  static void clearBits(uint64_t (&bits)[kWordCount]) {
    for (int word = 0; word < kWordCount; ++word) bits[word] = 0;
  }

  static void copyBits(uint64_t (&destination)[kWordCount],
                       const uint64_t (&source)[kWordCount]) {
    for (int word = 0; word < kWordCount; ++word) {
      destination[word] = source[word];
    }
  }

  static bool equalBits(const uint64_t (&first)[kWordCount],
                        const uint64_t (&second)[kWordCount]) {
    for (int word = 0; word < kWordCount; ++word) {
      if (first[word] != second[word]) return false;
    }
    return true;
  }

  static int bitCount(const uint64_t (&bits)[kWordCount]) {
    int count = 0;
    for (int word = 0; word < kWordCount; ++word) {
      uint64_t remaining = bits[word];
      while (remaining != 0) {
        remaining &= remaining - 1;
        ++count;
      }
    }
    return count;
  }

  static void saturatingIncrement(uint16_t& value) {
    if (value != 0xFFFFU) ++value;
  }

  void buildPlayableArea() {
    clearBits(playable_);
    uint64_t outside[kWordCount] = {};
    uint16_t queue[kMaxCells] = {};
    int readIndex = 0;
    int writeIndex = 0;

    for (int row = 0; row < height_; ++row) {
      for (int column = 0; column < width_; ++column) {
        if (row != 0 && row != height_ - 1 && column != 0 &&
            column != width_ - 1) {
          continue;
        }
        const uint16_t index =
            static_cast<uint16_t>(row * width_ + column);
        if (bitAt(walls_, index) || bitAt(outside, index)) continue;
        setBit(outside, index);
        queue[writeIndex++] = index;
      }
    }

    while (readIndex < writeIndex) {
      const uint16_t index = queue[readIndex++];
      const int row = index / width_;
      const int column = index % width_;
      for (int direction = 0; direction < 4; ++direction) {
        const int nextRow = row + kRowOffsets[direction];
        const int nextColumn = column + kColumnOffsets[direction];
        if (!validCell(nextRow, nextColumn)) continue;
        const uint16_t nextIndex =
            static_cast<uint16_t>(nextRow * width_ + nextColumn);
        if (bitAt(walls_, nextIndex) || bitAt(outside, nextIndex)) continue;
        setBit(outside, nextIndex);
        queue[writeIndex++] = nextIndex;
      }
    }

    for (int row = 0; row < height_; ++row) {
      for (int column = 0; column < width_; ++column) {
        const uint16_t index =
            static_cast<uint16_t>(row * width_ + column);
        if (!bitAt(walls_, index) && !bitAt(outside, index)) {
          setBit(playable_, index);
        }
      }
    }
  }

  bool validateDynamicState(const uint64_t (&boxes)[kWordCount],
                            uint16_t playerIndex) const {
    const int cellCount = width_ * height_;
    if (playerIndex >= cellCount || !bitAt(playable_, playerIndex) ||
        bitAt(boxes, playerIndex)) {
      return false;
    }
    for (int word = 0; word < kWordCount; ++word) {
      if ((boxes[word] & ~playable_[word]) != 0 ||
          (targets_[word] & ~playable_[word]) != 0) {
        return false;
      }
    }
    const int targetCount = bitCount(targets_);
    return targetCount > 0 && bitCount(boxes) == targetCount;
  }

  bool parseLevel(uint16_t levelIndex) {
    clearBits(walls_);
    clearBits(targets_);
    clearBits(playable_);
    clearBits(boxes_);
    playerIndex_ = kInvalidCell;

    const microban_levels::Level& level =
        microban_levels::kLevels[levelIndex];
    width_ = level.width;
    height_ = level.height;
    for (int row = 0; row < height_; ++row) {
      for (int column = 0; column < width_; ++column) {
        const uint16_t index =
            static_cast<uint16_t>(row * width_ + column);
        const char tile =
            microban_levels::kCells[level.offset + index];
        if (tile == '#') {
          setBit(walls_, index);
        } else if (tile == '.') {
          setBit(targets_, index);
        } else if (tile == '$') {
          setBit(boxes_, index);
        } else if (tile == '*') {
          setBit(targets_, index);
          setBit(boxes_, index);
        } else if (tile == '@') {
          if (playerIndex_ != kInvalidCell) return false;
          playerIndex_ = index;
        } else if (tile == '+') {
          if (playerIndex_ != kInvalidCell) return false;
          setBit(targets_, index);
          playerIndex_ = index;
        } else if (tile != ' ') {
          return false;
        }
      }
    }

    buildPlayableArea();
    return playerIndex_ != kInvalidCell &&
           validateDynamicState(boxes_, playerIndex_);
  }

  void loadLevel(uint16_t levelIndex) {
    levelIndex_ = levelIndex < kLevelCount ? levelIndex : 0;
    moveCount_ = 0;
    pushCount_ = 0;
    levelValid_ = parseLevel(levelIndex_);
  }
};
