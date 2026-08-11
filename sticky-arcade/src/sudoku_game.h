#pragma once

#include <stdint.h>

#include "sudoku_puzzles.h"

class SudokuGame {
 public:
  static constexpr int kSize = 9;
  static constexpr int kCellCount = kSize * kSize;
  static constexpr uint8_t kNoSelection = 0xFFU;
  static constexpr uint8_t kPuzzleCount = sudoku_puzzles::kPuzzleCount;

  struct Snapshot {
    uint8_t cells[kCellCount];
    uint8_t puzzleIndex;
    uint8_t selectedIndex;
  };

  SudokuGame() { start(0); }

  void start(uint8_t puzzleIndex) {
    puzzleIndex_ = puzzleIndex < kPuzzleCount ? puzzleIndex : 0;
    selectedIndex_ = kNoSelection;
    for (int index = 0; index < kCellCount; ++index) {
      const int row = index / kSize;
      const int column = index % kSize;
      cells_[index] =
          sudoku_puzzles::kPuzzles[puzzleIndex_].givens[row][column];
    }
  }

  bool select(int row, int column) {
    if (!validCell(row, column) || given(row, column) || solved()) return false;
    selectedIndex_ = static_cast<uint8_t>(row * kSize + column);
    return true;
  }

  bool setDigit(uint8_t digit) {
    if (selectedIndex_ == kNoSelection || digit > 9 || solved()) return false;
    const int row = selectedIndex_ / kSize;
    const int column = selectedIndex_ % kSize;
    if (given(row, column)) return false;
    if (digit != 0 && conflicts(row, column, digit)) return false;
    if (cells_[selectedIndex_] == digit) return false;
    cells_[selectedIndex_] = digit;
    return true;
  }

  void reset() { start(puzzleIndex_); }

  uint8_t at(int row, int column) const {
    return validCell(row, column) ? cells_[row * kSize + column] : 0;
  }

  bool given(int row, int column) const {
    return validCell(row, column) &&
           sudoku_puzzles::kPuzzles[puzzleIndex_].givens[row][column] != 0;
  }

  bool selected(int row, int column) const {
    return validCell(row, column) &&
           selectedIndex_ == row * kSize + column;
  }

  bool solved() const {
    for (int index = 0; index < kCellCount; ++index) {
      const int row = index / kSize;
      const int column = index % kSize;
      if (cells_[index] !=
          sudoku_puzzles::kPuzzles[puzzleIndex_].solution[row][column]) {
        return false;
      }
    }
    return true;
  }

  uint8_t puzzleIndex() const { return puzzleIndex_; }
  uint8_t selectedIndex() const { return selectedIndex_; }

  Snapshot snapshot() const {
    Snapshot result = {};
    for (int index = 0; index < kCellCount; ++index) {
      result.cells[index] = cells_[index];
    }
    result.puzzleIndex = puzzleIndex_;
    result.selectedIndex = selectedIndex_;
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.puzzleIndex >= kPuzzleCount ||
        (snapshot.selectedIndex != kNoSelection &&
         snapshot.selectedIndex >= kCellCount)) {
      return false;
    }
    for (int index = 0; index < kCellCount; ++index) {
      const uint8_t value = snapshot.cells[index];
      const int row = index / kSize;
      const int column = index % kSize;
      const uint8_t required =
          sudoku_puzzles::kPuzzles[snapshot.puzzleIndex].givens[row][column];
      if (value > 9 || (required != 0 && value != required)) return false;
    }
    if (snapshot.selectedIndex != kNoSelection) {
      const int row = snapshot.selectedIndex / kSize;
      const int column = snapshot.selectedIndex % kSize;
      if (sudoku_puzzles::kPuzzles[snapshot.puzzleIndex]
              .givens[row][column] != 0) {
        return false;
      }
    }

    puzzleIndex_ = snapshot.puzzleIndex;
    selectedIndex_ = snapshot.selectedIndex;
    for (int index = 0; index < kCellCount; ++index) {
      cells_[index] = snapshot.cells[index];
    }
    return true;
  }

 private:
  uint8_t cells_[kCellCount] = {};
  uint8_t puzzleIndex_ = 0;
  uint8_t selectedIndex_ = kNoSelection;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  bool conflicts(int row, int column, uint8_t digit) const {
    for (int position = 0; position < kSize; ++position) {
      if (position != column && at(row, position) == digit) return true;
      if (position != row && at(position, column) == digit) return true;
    }
    const int boxRow = row / 3 * 3;
    const int boxColumn = column / 3 * 3;
    for (int offsetRow = 0; offsetRow < 3; ++offsetRow) {
      for (int offsetColumn = 0; offsetColumn < 3; ++offsetColumn) {
        const int candidateRow = boxRow + offsetRow;
        const int candidateColumn = boxColumn + offsetColumn;
        if ((candidateRow != row || candidateColumn != column) &&
            at(candidateRow, candidateColumn) == digit) {
          return true;
        }
      }
    }
    return false;
  }
};
