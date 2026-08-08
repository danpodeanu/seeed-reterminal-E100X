#pragma once

#include <stdint.h>

#include <algorithm>

class PipeConnectGame {
 public:
  static constexpr int kSize = 6;
  static constexpr int kCellCount = kSize * kSize;

  enum Edge : uint8_t {
    North = 1U << 0,
    East = 1U << 1,
    South = 1U << 2,
    West = 1U << 3,
  };

  struct Snapshot {
    uint8_t cells[kCellCount];
    uint8_t initialCells[kCellCount];
    uint8_t solutionCells[kCellCount];
    uint16_t moves;
  };

  void start(uint32_t seed) {
    std::fill(cells_, cells_ + kCellCount, 0);
    std::fill(solutionCells_, solutionCells_ + kCellCount, 0);

    bool visited[kCellCount] = {};
    uint8_t stack[kCellCount] = {};
    int stackSize = 0;
    uint32_t randomState = seed == 0 ? 0xA341316CUL : seed;
    const int startCell =
        static_cast<int>(nextRandom(randomState) % kCellCount);
    stack[stackSize++] = static_cast<uint8_t>(startCell);
    visited[startCell] = true;

    while (stackSize > 0) {
      const int cell = stack[stackSize - 1];
      const int row = cell / kSize;
      const int column = cell % kSize;
      uint8_t candidates[4] = {};
      int candidateCount = 0;
      for (int direction = 0; direction < 4; ++direction) {
        const int neighborRow = row + kRowOffsets[direction];
        const int neighborColumn = column + kColumnOffsets[direction];
        if (!validCell(neighborRow, neighborColumn)) continue;
        const int neighbor = neighborRow * kSize + neighborColumn;
        if (!visited[neighbor]) {
          candidates[candidateCount++] = static_cast<uint8_t>(direction);
        }
      }

      if (candidateCount == 0) {
        --stackSize;
        continue;
      }

      const int direction =
          candidates[nextRandom(randomState) % candidateCount];
      const int neighborRow = row + kRowOffsets[direction];
      const int neighborColumn = column + kColumnOffsets[direction];
      const int neighbor = neighborRow * kSize + neighborColumn;
      solutionCells_[cell] |= kEdges[direction];
      solutionCells_[neighbor] |= kEdges[(direction + 2) % 4];
      visited[neighbor] = true;
      stack[stackSize++] = static_cast<uint8_t>(neighbor);
    }

    for (int index = 0; index < kCellCount; ++index) {
      cells_[index] = rotateMask(
          solutionCells_[index], static_cast<int>(nextRandom(randomState) & 3U));
    }
    if (networkConnectedAndClosed(cells_)) {
      for (int index = 0; index < kCellCount; ++index) {
        const uint8_t rotated = rotateMask(cells_[index], 1);
        if (rotated != cells_[index]) {
          cells_[index] = rotated;
          break;
        }
      }
    }

    std::copy(cells_, cells_ + kCellCount, initialCells_);
    moves_ = 0;
    solved_ = false;
  }

  bool rotate(int row, int column) {
    if (!validCell(row, column) || solved_) return false;
    const int index = row * kSize + column;
    cells_[index] = rotateMask(cells_[index], 1);
    ++moves_;
    solved_ = networkConnectedAndClosed(cells_);
    return true;
  }

  void reset() {
    std::copy(initialCells_, initialCells_ + kCellCount, cells_);
    moves_ = 0;
    solved_ = false;
  }

  uint8_t at(int row, int column) const {
    if (!validCell(row, column)) return 0;
    return cells_[row * kSize + column];
  }

  uint8_t solutionAt(int row, int column) const {
    if (!validCell(row, column)) return 0;
    return solutionCells_[row * kSize + column];
  }

  uint16_t moves() const { return moves_; }
  bool solved() const { return solved_; }

  Snapshot snapshot() const {
    Snapshot result = {};
    std::copy(cells_, cells_ + kCellCount, result.cells);
    std::copy(initialCells_, initialCells_ + kCellCount, result.initialCells);
    std::copy(solutionCells_, solutionCells_ + kCellCount,
              result.solutionCells);
    result.moves = moves_;
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    if (!networkConnectedAndClosed(snapshot.solutionCells) ||
        networkConnectedAndClosed(snapshot.initialCells)) {
      return false;
    }
    for (int index = 0; index < kCellCount; ++index) {
      if (!sameShape(snapshot.cells[index], snapshot.solutionCells[index]) ||
          !sameShape(snapshot.initialCells[index],
                     snapshot.solutionCells[index])) {
        return false;
      }
    }

    std::copy(snapshot.cells, snapshot.cells + kCellCount, cells_);
    std::copy(snapshot.initialCells, snapshot.initialCells + kCellCount,
              initialCells_);
    std::copy(snapshot.solutionCells, snapshot.solutionCells + kCellCount,
              solutionCells_);
    moves_ = snapshot.moves;
    solved_ = networkConnectedAndClosed(cells_);
    return true;
  }

 private:
  inline static constexpr int kRowOffsets[4] = {-1, 0, 1, 0};
  inline static constexpr int kColumnOffsets[4] = {0, 1, 0, -1};
  inline static constexpr uint8_t kEdges[4] = {
      North,
      East,
      South,
      West,
  };

  uint8_t cells_[kCellCount] = {};
  uint8_t initialCells_[kCellCount] = {};
  uint8_t solutionCells_[kCellCount] = {};
  uint16_t moves_ = 0;
  bool solved_ = false;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  static uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  static uint8_t rotateMask(uint8_t mask, int turns) {
    turns &= 3;
    if (turns == 0) return mask;
    return static_cast<uint8_t>(((mask << turns) | (mask >> (4 - turns))) &
                                0x0F);
  }

  static bool sameShape(uint8_t candidate, uint8_t solution) {
    if (candidate == 0 || (candidate & 0xF0) != 0) return false;
    for (int turns = 0; turns < 4; ++turns) {
      if (candidate == rotateMask(solution, turns)) return true;
    }
    return false;
  }

  static bool networkConnectedAndClosed(const uint8_t cells[kCellCount]) {
    bool visited[kCellCount] = {};
    uint8_t stack[kCellCount] = {};
    int stackSize = 1;
    int visitedCount = 0;
    stack[0] = 0;
    visited[0] = true;

    while (stackSize > 0) {
      const int cell = stack[--stackSize];
      ++visitedCount;
      const int row = cell / kSize;
      const int column = cell % kSize;
      const uint8_t mask = cells[cell];
      if (mask == 0 || (mask & 0xF0) != 0) return false;

      for (int direction = 0; direction < 4; ++direction) {
        if ((mask & kEdges[direction]) == 0) continue;
        const int neighborRow = row + kRowOffsets[direction];
        const int neighborColumn = column + kColumnOffsets[direction];
        if (!validCell(neighborRow, neighborColumn)) return false;
        const int neighbor = neighborRow * kSize + neighborColumn;
        if ((cells[neighbor] & kEdges[(direction + 2) % 4]) == 0) {
          return false;
        }
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          stack[stackSize++] = static_cast<uint8_t>(neighbor);
        }
      }
    }

    return visitedCount == kCellCount;
  }
};
