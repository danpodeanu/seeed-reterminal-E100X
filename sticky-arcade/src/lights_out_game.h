#pragma once

#include <stdint.h>

class LightsOutGame {
 public:
  static constexpr int kSize = 5;
  static constexpr int kCellCount = kSize * kSize;
  static constexpr uint32_t kCellMask = (1UL << kCellCount) - 1UL;

  struct Snapshot {
    uint32_t cells;
    uint32_t initialCells;
    uint32_t solutionMask;
    uint16_t moves;
  };

  void startPuzzle(uint32_t scrambleMask) {
    scrambleMask &= kCellMask;
    cells_ = 0;
    for (int index = 0; index < kCellCount; ++index) {
      if ((scrambleMask & (1UL << index)) != 0) {
        cells_ ^= pressMask(index / kSize, index % kSize);
      }
    }
    if (cells_ == 0) {
      scrambleMask = 1UL << (kCellCount / 2);
      cells_ = pressMask(kSize / 2, kSize / 2);
    }
    initialCells_ = cells_;
    solutionMask_ = scrambleMask;
    moves_ = 0;
  }

  bool press(int row, int column) {
    if (!validCell(row, column)) return false;
    cells_ ^= pressMask(row, column);
    ++moves_;
    return true;
  }

  void reset() {
    cells_ = initialCells_;
    moves_ = 0;
  }

  bool isOn(int row, int column) const {
    if (!validCell(row, column)) return false;
    return (cells_ & (1UL << (row * kSize + column))) != 0;
  }

  bool solved() const { return cells_ == 0; }
  uint16_t moves() const { return moves_; }
  uint32_t cells() const { return cells_; }
  uint32_t solutionMask() const { return solutionMask_; }

  Snapshot snapshot() const {
    return {cells_, initialCells_, solutionMask_, moves_};
  }

  bool restore(const Snapshot& snapshot) {
    if ((snapshot.cells & ~kCellMask) != 0 ||
        (snapshot.initialCells & ~kCellMask) != 0 ||
        (snapshot.solutionMask & ~kCellMask) != 0 ||
        snapshot.initialCells == 0 || snapshot.solutionMask == 0) {
      return false;
    }
    cells_ = snapshot.cells;
    initialCells_ = snapshot.initialCells;
    solutionMask_ = snapshot.solutionMask;
    moves_ = snapshot.moves;
    return true;
  }

 private:
  uint32_t cells_ = 0;
  uint32_t initialCells_ = 0;
  uint32_t solutionMask_ = 0;
  uint16_t moves_ = 0;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  static uint32_t pressMask(int row, int column) {
    uint32_t mask = 0;
    constexpr int offsets[][2] = {
        {0, 0},
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    };
    for (const auto& offset : offsets) {
      const int neighborRow = row + offset[0];
      const int neighborColumn = column + offset[1];
      if (validCell(neighborRow, neighborColumn)) {
        mask |= 1UL << (neighborRow * kSize + neighborColumn);
      }
    }
    return mask;
  }
};
