#pragma once

#include <stdint.h>

class PegSolitaireGame {
 public:
  static constexpr int kSize = 7;
  static constexpr int kCellCount = kSize * kSize;
  static constexpr int kCenterRow = 3;
  static constexpr int kCenterColumn = 3;
  static constexpr uint8_t kNoSelection = 0xFF;
  static constexpr uint8_t kStartPegCount = 32;

  enum class TapResult : uint8_t {
    InvalidCell = 0,
    NoSelection = 1,
    Selected = 2,
    Reselected = 3,
    ClearedSelection = 4,
    Moved = 5,
  };

  struct Snapshot {
    uint64_t pegs;
    uint8_t selectedIndex;
  };

  void start() { reset(); }

  void reset() {
    pegs_ = kInitialPegsMask;
    selectedIndex_ = kNoSelection;
  }

  TapResult tap(int row, int column) {
    if (!isValidCell(row, column)) {
      if (!hasSelection()) return TapResult::InvalidCell;
      selectedIndex_ = kNoSelection;
      return TapResult::ClearedSelection;
    }

    const int index = row * kSize + column;
    const uint64_t bit = cellBit(index);
    if ((pegs_ & bit) != 0) {
      const bool reselected = hasSelection();
      selectedIndex_ = static_cast<uint8_t>(index);
      return reselected ? TapResult::Reselected : TapResult::Selected;
    }

    if (hasSelection() &&
        isLegalMoveByIndex(selectedIndex_, static_cast<uint8_t>(index))) {
      performMove(selectedIndex_, static_cast<uint8_t>(index));
      selectedIndex_ = kNoSelection;
      return TapResult::Moved;
    }

    if (!hasSelection()) return TapResult::NoSelection;
    selectedIndex_ = kNoSelection;
    return TapResult::ClearedSelection;
  }

  bool move(int fromRow, int fromColumn, int toRow, int toColumn) {
    if (!isLegalMove(fromRow, fromColumn, toRow, toColumn)) return false;
    performMove(static_cast<uint8_t>(fromRow * kSize + fromColumn),
                static_cast<uint8_t>(toRow * kSize + toColumn));
    selectedIndex_ = kNoSelection;
    return true;
  }

  bool isLegalMove(int fromRow, int fromColumn, int toRow, int toColumn) const {
    if (!validCell(fromRow, fromColumn) || !validCell(toRow, toColumn)) {
      return false;
    }
    return isLegalMoveByIndex(
        static_cast<uint8_t>(fromRow * kSize + fromColumn),
        static_cast<uint8_t>(toRow * kSize + toColumn));
  }

  bool validCell(int row, int column) const { return isValidBoardCell(row, column); }

  bool isValidCell(int row, int column) const { return validCell(row, column); }

  bool hasPeg(int row, int column) const {
    return isValidBoardCell(row, column) &&
           (pegs_ & cellBit(row * kSize + column)) != 0;
  }

  bool hasSelection() const { return selectedIndex_ != kNoSelection; }

  int selectedRow() const {
    return hasSelection() ? selectedIndex_ / kSize : -1;
  }

  int selectedColumn() const {
    return hasSelection() ? selectedIndex_ % kSize : -1;
  }

  bool isSelected(int row, int column) const {
    return hasSelection() && isValidBoardCell(row, column) &&
           selectedIndex_ == static_cast<uint8_t>(row * kSize + column);
  }

  uint8_t pegCount() const { return static_cast<uint8_t>(bitCount(pegs_)); }

  uint8_t moveCount() const {
    return static_cast<uint8_t>(kStartPegCount - pegCount());
  }

  uint8_t moves() const { return moveCount(); }

  bool hasAvailableMoves() const {
    for (int row = 0; row < kSize; ++row) {
      for (int column = 0; column < kSize; ++column) {
        if (!hasPeg(row, column)) continue;
        if (isLegalMove(row, column, row - 2, column) ||
            isLegalMove(row, column, row + 2, column) ||
            isLegalMove(row, column, row, column - 2) ||
            isLegalMove(row, column, row, column + 2)) {
          return true;
        }
      }
    }
    return false;
  }

  bool hasAvailableMove() const { return hasAvailableMoves(); }

  bool stalemate() const { return !solved() && !hasAvailableMoves(); }

  bool solved() const {
    return pegs_ == cellBit(kCenterRow * kSize + kCenterColumn);
  }

  Snapshot snapshot() const { return {pegs_, selectedIndex_}; }

  bool restore(const Snapshot& snapshot) {
    if ((snapshot.pegs & ~kValidCellsMask) != 0) return false;

    const uint8_t pegCount = static_cast<uint8_t>(bitCount(snapshot.pegs));
    if (pegCount == 0 || pegCount > kStartPegCount) return false;
    if (pegCount == kStartPegCount && snapshot.pegs != kInitialPegsMask) {
      return false;
    }

    if (snapshot.selectedIndex != kNoSelection) {
      if (snapshot.selectedIndex >= kCellCount ||
          (snapshot.pegs & cellBit(snapshot.selectedIndex)) == 0 ||
          (kValidCellsMask & cellBit(snapshot.selectedIndex)) == 0) {
        return false;
      }
    }

    pegs_ = snapshot.pegs;
    selectedIndex_ = snapshot.selectedIndex;
    return true;
  }

 private:
  static constexpr uint64_t cellBit(int index) { return 1ULL << index; }

  inline static constexpr uint64_t kValidCellsMask =
      (1ULL << (0 * kSize + 2)) | (1ULL << (0 * kSize + 3)) |
      (1ULL << (0 * kSize + 4)) | (1ULL << (1 * kSize + 2)) |
      (1ULL << (1 * kSize + 3)) | (1ULL << (1 * kSize + 4)) |
      (1ULL << (2 * kSize + 0)) | (1ULL << (2 * kSize + 1)) |
      (1ULL << (2 * kSize + 2)) | (1ULL << (2 * kSize + 3)) |
      (1ULL << (2 * kSize + 4)) | (1ULL << (2 * kSize + 5)) |
      (1ULL << (2 * kSize + 6)) | (1ULL << (3 * kSize + 0)) |
      (1ULL << (3 * kSize + 1)) | (1ULL << (3 * kSize + 2)) |
      (1ULL << (3 * kSize + 3)) | (1ULL << (3 * kSize + 4)) |
      (1ULL << (3 * kSize + 5)) | (1ULL << (3 * kSize + 6)) |
      (1ULL << (4 * kSize + 0)) | (1ULL << (4 * kSize + 1)) |
      (1ULL << (4 * kSize + 2)) | (1ULL << (4 * kSize + 3)) |
      (1ULL << (4 * kSize + 4)) | (1ULL << (4 * kSize + 5)) |
      (1ULL << (4 * kSize + 6)) | (1ULL << (5 * kSize + 2)) |
      (1ULL << (5 * kSize + 3)) | (1ULL << (5 * kSize + 4)) |
      (1ULL << (6 * kSize + 2)) | (1ULL << (6 * kSize + 3)) |
      (1ULL << (6 * kSize + 4));
  inline static constexpr uint64_t kInitialPegsMask =
      kValidCellsMask & ~(1ULL << (kCenterRow * kSize + kCenterColumn));

  uint64_t pegs_ = kInitialPegsMask;
  uint8_t selectedIndex_ = kNoSelection;

  static bool isValidBoardCell(int row, int column) {
    if (row < 0 || row >= kSize || column < 0 || column >= kSize) return false;
    return (row >= 2 && row <= 4) || (column >= 2 && column <= 4);
  }

  static int bitCount(uint64_t value) {
    int count = 0;
    while (value != 0) {
      value &= value - 1;
      ++count;
    }
    return count;
  }

  bool isLegalMoveByIndex(uint8_t fromIndex, uint8_t toIndex) const {
    const int fromRow = fromIndex / kSize;
    const int fromColumn = fromIndex % kSize;
    const int toRow = toIndex / kSize;
    const int toColumn = toIndex % kSize;
    if (!isValidBoardCell(fromRow, fromColumn) ||
        !isValidBoardCell(toRow, toColumn)) {
      return false;
    }

    const int rowDelta = toRow - fromRow;
    const int columnDelta = toColumn - fromColumn;
    if (!((rowDelta == 0 && (columnDelta == -2 || columnDelta == 2)) ||
          (columnDelta == 0 && (rowDelta == -2 || rowDelta == 2)))) {
      return false;
    }

    const uint64_t fromBit = cellBit(fromIndex);
    const uint64_t toBit = cellBit(toIndex);
    if ((pegs_ & fromBit) == 0 || (pegs_ & toBit) != 0) return false;

    const int middleRow = (fromRow + toRow) / 2;
    const int middleColumn = (fromColumn + toColumn) / 2;
    return (pegs_ & cellBit(middleRow * kSize + middleColumn)) != 0;
  }

  void performMove(uint8_t fromIndex, uint8_t toIndex) {
    const int fromRow = fromIndex / kSize;
    const int fromColumn = fromIndex % kSize;
    const int toRow = toIndex / kSize;
    const int toColumn = toIndex % kSize;
    const int middleIndex = ((fromRow + toRow) / 2) * kSize +
                            ((fromColumn + toColumn) / 2);
    pegs_ &= ~cellBit(fromIndex);
    pegs_ &= ~cellBit(middleIndex);
    pegs_ |= cellBit(toIndex);
  }
};
