#pragma once

#include <stdint.h>

class NonogramGame {
 public:
  static constexpr int kSize = 5;
  static constexpr int kCellCount = kSize * kSize;
  static constexpr uint32_t kCellMask = (1UL << kCellCount) - 1UL;

  enum class CellState : uint8_t {
    Blank = 0,
    Filled = 1,
    Crossed = 2,
  };

  struct Snapshot {
    uint32_t solution;
    uint64_t cells;
  };

  void start(uint32_t solution) {
    solution_ = solution & kCellMask;
    if (solution_ == 0) solution_ = 1UL << (kCellCount / 2);
    cells_ = 0;
  }

  bool cycle(int row, int column) {
    if (!validCell(row, column) || solved()) return false;
    const int shift = (row * kSize + column) * 2;
    const uint64_t mask = 3ULL << shift;
    const uint8_t current = static_cast<uint8_t>((cells_ & mask) >> shift);
    const uint8_t next = static_cast<uint8_t>((current + 1) % 3);
    cells_ = (cells_ & ~mask) | (static_cast<uint64_t>(next) << shift);
    return true;
  }

  void reset() { cells_ = 0; }

  CellState at(int row, int column) const {
    if (!validCell(row, column)) return CellState::Blank;
    const int shift = (row * kSize + column) * 2;
    return static_cast<CellState>((cells_ >> shift) & 3ULL);
  }

  bool solutionAt(int row, int column) const {
    return validCell(row, column) &&
           (solution_ & (1UL << (row * kSize + column))) != 0;
  }

  int rowClues(int row, uint8_t clues[kSize]) const {
    return lineClues(true, row, clues);
  }

  int columnClues(int column, uint8_t clues[kSize]) const {
    return lineClues(false, column, clues);
  }

  bool solved() const { return filledMask() == solution_; }

  Snapshot snapshot() const { return {solution_, cells_}; }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.solution == 0 ||
        (snapshot.solution & ~kCellMask) != 0 ||
        (snapshot.cells >> (kCellCount * 2)) != 0) {
      return false;
    }
    for (int index = 0; index < kCellCount; ++index) {
      if (((snapshot.cells >> (index * 2)) & 3ULL) == 3) return false;
    }
    solution_ = snapshot.solution;
    cells_ = snapshot.cells;
    return true;
  }

 private:
  uint32_t solution_ = 1UL << (kCellCount / 2);
  uint64_t cells_ = 0;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  uint32_t filledMask() const {
    uint32_t result = 0;
    for (int index = 0; index < kCellCount; ++index) {
      if (((cells_ >> (index * 2)) & 3ULL) ==
          static_cast<uint8_t>(CellState::Filled)) {
        result |= 1UL << index;
      }
    }
    return result;
  }

  int lineClues(bool rowLine, int line, uint8_t clues[kSize]) const {
    for (int index = 0; index < kSize; ++index) clues[index] = 0;
    if (line < 0 || line >= kSize) return 0;

    int count = 0;
    int run = 0;
    for (int position = 0; position < kSize; ++position) {
      const int row = rowLine ? line : position;
      const int column = rowLine ? position : line;
      if (solutionAt(row, column)) {
        ++run;
      } else if (run > 0) {
        clues[count++] = static_cast<uint8_t>(run);
        run = 0;
      }
    }
    if (run > 0) clues[count++] = static_cast<uint8_t>(run);
    return count;
  }
};
