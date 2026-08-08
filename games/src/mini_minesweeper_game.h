#pragma once

#include <stdint.h>

class MiniMinesweeperGame {
 public:
  static constexpr int kSize = 6;
  static constexpr int kCellCount = kSize * kSize;
  static constexpr int kMineCount = 6;
  static constexpr uint64_t kCellMask = (1ULL << kCellCount) - 1ULL;

  enum class RevealResult {
    NoChange,
    Revealed,
    Won,
    Lost,
  };

  struct Snapshot {
    uint64_t mines;
    uint64_t revealed;
    uint64_t flagged;
    uint32_t seed;
    uint8_t generated;
  };

  void start(uint32_t seed) {
    mines_ = 0;
    revealed_ = 0;
    flagged_ = 0;
    seed_ = seed == 0 ? 0x9E3779B9UL : seed;
    generated_ = false;
  }

  RevealResult reveal(int row, int column) {
    if (!validCell(row, column) || gameOver()) {
      return RevealResult::NoChange;
    }
    const int index = row * kSize + column;
    const uint64_t bit = 1ULL << index;
    if ((flagged_ & bit) != 0 || (revealed_ & bit) != 0) {
      return RevealResult::NoChange;
    }

    if (!generated_) generate(index);
    if ((mines_ & bit) != 0) {
      revealed_ |= bit;
      return RevealResult::Lost;
    }

    revealSafeArea(index);
    return won() ? RevealResult::Won : RevealResult::Revealed;
  }

  bool toggleFlag(int row, int column) {
    if (!validCell(row, column) || gameOver()) return false;
    const uint64_t bit = 1ULL << (row * kSize + column);
    if ((revealed_ & bit) != 0) return false;
    flagged_ ^= bit;
    return true;
  }

  void reset() {
    revealed_ = 0;
    flagged_ = 0;
  }

  bool isMine(int row, int column) const {
    return hasBit(mines_, row, column);
  }

  bool isRevealed(int row, int column) const {
    return hasBit(revealed_, row, column);
  }

  bool isFlagged(int row, int column) const {
    return hasBit(flagged_, row, column);
  }

  int adjacentMines(int row, int column) const {
    if (!validCell(row, column) || !generated_) return 0;
    int count = 0;
    for (int rowOffset = -1; rowOffset <= 1; ++rowOffset) {
      for (int columnOffset = -1; columnOffset <= 1; ++columnOffset) {
        if (rowOffset == 0 && columnOffset == 0) continue;
        const int neighborRow = row + rowOffset;
        const int neighborColumn = column + columnOffset;
        if (validCell(neighborRow, neighborColumn) &&
            isMine(neighborRow, neighborColumn)) {
          ++count;
        }
      }
    }
    return count;
  }

  int flags() const { return bitCount(flagged_); }
  int revealedCount() const { return bitCount(revealed_); }
  bool generated() const { return generated_; }
  bool lost() const { return (revealed_ & mines_) != 0; }

  bool won() const {
    return generated_ && !lost() &&
           (revealed_ & (kCellMask & ~mines_)) ==
               (kCellMask & ~mines_);
  }

  bool gameOver() const { return lost() || won(); }

  Snapshot snapshot() const {
    return {
        mines_,
        revealed_,
        flagged_,
        seed_,
        static_cast<uint8_t>(generated_ ? 1 : 0),
    };
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.seed == 0 || snapshot.generated > 1 ||
        ((snapshot.mines | snapshot.revealed | snapshot.flagged) &
         ~kCellMask) != 0 ||
        (snapshot.revealed & snapshot.flagged) != 0) {
      return false;
    }

    if (snapshot.generated == 0) {
      if (snapshot.mines != 0 || snapshot.revealed != 0) return false;
    } else if (bitCount(snapshot.mines) != kMineCount ||
               bitCount(snapshot.revealed & snapshot.mines) > 1) {
      return false;
    }

    mines_ = snapshot.mines;
    revealed_ = snapshot.revealed;
    flagged_ = snapshot.flagged;
    seed_ = snapshot.seed;
    generated_ = snapshot.generated != 0;
    return true;
  }

 private:
  uint64_t mines_ = 0;
  uint64_t revealed_ = 0;
  uint64_t flagged_ = 0;
  uint32_t seed_ = 0x9E3779B9UL;
  bool generated_ = false;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  static bool hasBit(uint64_t mask, int row, int column) {
    return validCell(row, column) &&
           (mask & (1ULL << (row * kSize + column))) != 0;
  }

  static int bitCount(uint64_t value) {
    int count = 0;
    while (value != 0) {
      value &= value - 1;
      ++count;
    }
    return count;
  }

  static uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  void generate(int safeIndex) {
    uint8_t candidates[kCellCount] = {};
    int candidateCount = 0;
    const int safeRow = safeIndex / kSize;
    const int safeColumn = safeIndex % kSize;
    for (int index = 0; index < kCellCount; ++index) {
      const int row = index / kSize;
      const int column = index % kSize;
      if (row >= safeRow - 1 && row <= safeRow + 1 &&
          column >= safeColumn - 1 && column <= safeColumn + 1) {
        continue;
      }
      candidates[candidateCount++] = static_cast<uint8_t>(index);
    }

    uint32_t randomState = seed_;
    for (int mine = 0; mine < kMineCount; ++mine) {
      const int selected =
          mine + static_cast<int>(nextRandom(randomState) %
                                  static_cast<uint32_t>(candidateCount - mine));
      const uint8_t temporary = candidates[mine];
      candidates[mine] = candidates[selected];
      candidates[selected] = temporary;
      mines_ |= 1ULL << candidates[mine];
    }
    generated_ = true;
  }

  void revealSafeArea(int startIndex) {
    uint8_t queue[kCellCount] = {};
    int head = 0;
    int tail = 0;
    revealed_ |= 1ULL << startIndex;
    queue[tail++] = static_cast<uint8_t>(startIndex);

    while (head < tail) {
      const int index = queue[head++];
      const int row = index / kSize;
      const int column = index % kSize;
      if (adjacentMines(row, column) != 0) continue;

      for (int rowOffset = -1; rowOffset <= 1; ++rowOffset) {
        for (int columnOffset = -1; columnOffset <= 1; ++columnOffset) {
          const int neighborRow = row + rowOffset;
          const int neighborColumn = column + columnOffset;
          if (!validCell(neighborRow, neighborColumn)) continue;
          const int neighbor = neighborRow * kSize + neighborColumn;
          const uint64_t bit = 1ULL << neighbor;
          if ((mines_ & bit) != 0 || (flagged_ & bit) != 0 ||
              (revealed_ & bit) != 0) {
            continue;
          }
          revealed_ |= bit;
          if (adjacentMines(neighborRow, neighborColumn) == 0) {
            queue[tail++] = static_cast<uint8_t>(neighbor);
          }
        }
      }
    }
  }
};
