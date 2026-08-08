#pragma once

#include <stdint.h>

#include <algorithm>

class Game2048 {
 public:
  static constexpr int kSize = 4;
  static constexpr int kCellCount = kSize * kSize;

  enum class Direction {
    Left,
    Right,
    Up,
    Down,
  };

  struct Snapshot {
    uint32_t cells[kCellCount];
    uint32_t score;
    uint32_t bestScore;
  };

  void start(uint32_t firstRandom, uint32_t secondRandom) {
    std::fill(cells_, cells_ + kCellCount, 0);
    score_ = 0;
    won_ = false;
    gameOver_ = false;
    addRandomTile(firstRandom);
    addRandomTile(secondRandom);
  }

  bool move(Direction direction, uint32_t randomValue) {
    if (gameOver_) return false;

    bool changed = false;
    for (int line = 0; line < kSize; ++line) {
      uint32_t input[kSize] = {};
      uint32_t output[kSize] = {};
      for (int offset = 0; offset < kSize; ++offset) {
        input[offset] = cellInDirection(direction, line, offset);
      }
      mergeLine(input, output);
      for (int offset = 0; offset < kSize; ++offset) {
        uint32_t& cell = cellInDirection(direction, line, offset);
        if (cell != output[offset]) changed = true;
        cell = output[offset];
      }
    }

    if (!changed) {
      gameOver_ = !movesAvailable();
      return false;
    }

    addRandomTile(randomValue);
    bestScore_ = std::max(bestScore_, score_);
    won_ = containsAtLeast(2048);
    gameOver_ = !movesAvailable();
    return true;
  }

  uint32_t at(int row, int column) const {
    if (!validCell(row, column)) return 0;
    return cells_[row * kSize + column];
  }

  uint32_t score() const { return score_; }
  uint32_t bestScore() const { return bestScore_; }
  bool won() const { return won_; }
  bool gameOver() const { return gameOver_; }

  Snapshot snapshot() const {
    Snapshot result = {};
    std::copy(cells_, cells_ + kCellCount, result.cells);
    result.score = score_;
    result.bestScore = bestScore_;
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    bool hasTile = false;
    for (uint32_t value : snapshot.cells) {
      if (value == 0) continue;
      if (value < 2 || value > (1UL << 30) ||
          (value & (value - 1)) != 0) {
        return false;
      }
      hasTile = true;
    }
    if (!hasTile || snapshot.bestScore < snapshot.score) return false;

    std::copy(snapshot.cells, snapshot.cells + kCellCount, cells_);
    score_ = snapshot.score;
    bestScore_ = snapshot.bestScore;
    won_ = containsAtLeast(2048);
    gameOver_ = !movesAvailable();
    return true;
  }

 private:
  uint32_t cells_[kCellCount] = {};
  uint32_t score_ = 0;
  uint32_t bestScore_ = 0;
  bool won_ = false;
  bool gameOver_ = false;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  uint32_t& cellInDirection(Direction direction, int line, int offset) {
    switch (direction) {
      case Direction::Left:
        return cells_[line * kSize + offset];
      case Direction::Right:
        return cells_[line * kSize + (kSize - 1 - offset)];
      case Direction::Up:
        return cells_[offset * kSize + line];
      case Direction::Down:
        return cells_[(kSize - 1 - offset) * kSize + line];
    }
    return cells_[0];
  }

  void mergeLine(const uint32_t input[kSize], uint32_t output[kSize]) {
    uint32_t packed[kSize] = {};
    int packedCount = 0;
    for (int index = 0; index < kSize; ++index) {
      if (input[index] != 0) packed[packedCount++] = input[index];
    }

    int outputIndex = 0;
    for (int index = 0; index < packedCount; ++index) {
      if (index + 1 < packedCount && packed[index] == packed[index + 1]) {
        output[outputIndex] = packed[index] * 2;
        score_ += output[outputIndex];
        ++index;
      } else {
        output[outputIndex] = packed[index];
      }
      ++outputIndex;
    }
  }

  void addRandomTile(uint32_t randomValue) {
    int emptyCount = 0;
    for (uint32_t value : cells_) {
      if (value == 0) ++emptyCount;
    }
    if (emptyCount == 0) return;

    int target = static_cast<int>(randomValue % emptyCount);
    const uint32_t tileValue = ((randomValue >> 16) % 10) == 0 ? 4 : 2;
    for (uint32_t& value : cells_) {
      if (value != 0) continue;
      if (target-- == 0) {
        value = tileValue;
        return;
      }
    }
  }

  bool containsAtLeast(uint32_t target) const {
    for (uint32_t value : cells_) {
      if (value >= target) return true;
    }
    return false;
  }

  bool movesAvailable() const {
    for (int row = 0; row < kSize; ++row) {
      for (int column = 0; column < kSize; ++column) {
        const uint32_t value = cells_[row * kSize + column];
        if (value == 0) return true;
        if (column + 1 < kSize &&
            value == cells_[row * kSize + column + 1]) {
          return true;
        }
        if (row + 1 < kSize &&
            value == cells_[(row + 1) * kSize + column]) {
          return true;
        }
      }
    }
    return false;
  }
};
