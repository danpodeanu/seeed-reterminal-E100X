#pragma once

#include <stdint.h>

#include "crossword_puzzles.h"

class CrosswordGame {
 public:
  static constexpr int kMaxSize = 9;
  static constexpr int kMaxCells = kMaxSize * kMaxSize;
  static constexpr uint8_t kNoSelection = 0xFFU;
  static constexpr uint8_t kPuzzleCount = crossword_puzzles::kPuzzleCount;

  enum class Direction : uint8_t { Across = 0, Down = 1 };

  struct Snapshot {
    char entries[kMaxCells];
    uint8_t puzzleIndex;
    uint8_t selectedIndex;
    uint8_t direction;
  };

  CrosswordGame() { start(0); }

  void start(uint8_t puzzleIndex) {
    puzzleIndex_ = puzzleIndex < kPuzzleCount ? puzzleIndex : 0;
    selectedIndex_ = kNoSelection;
    direction_ = Direction::Across;
    clearEntries();
  }

  void reset() {
    selectedIndex_ = kNoSelection;
    direction_ = Direction::Across;
    clearEntries();
  }

  bool select(int row, int column) {
    if (!validCell(row, column) || blocked(row, column) || solved()) {
      return false;
    }
    const uint8_t index = static_cast<uint8_t>(row * width() + column);
    const bool across = clueForCell(index, Direction::Across) >= 0;
    const bool down = clueForCell(index, Direction::Down) >= 0;
    if (!across && !down) return false;
    if (selectedIndex_ == index && across && down) {
      direction_ = direction_ == Direction::Across ? Direction::Down
                                                   : Direction::Across;
    } else if (direction_ == Direction::Across && !across) {
      direction_ = Direction::Down;
    } else if (direction_ == Direction::Down && !down) {
      direction_ = Direction::Across;
    }
    selectedIndex_ = index;
    return true;
  }

  bool setLetter(char letter) {
    if (selectedIndex_ == kNoSelection || solved()) return false;
    if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
    if (letter < 'A' || letter > 'Z') return false;
    entries_[selectedIndex_] = letter;
    moveSelection(1);
    return true;
  }

  bool erase() {
    if (selectedIndex_ == kNoSelection || solved()) return false;
    if (entries_[selectedIndex_] != '\0') {
      entries_[selectedIndex_] = '\0';
      return true;
    }
    if (!moveSelection(-1)) return false;
    entries_[selectedIndex_] = '\0';
    return true;
  }

  uint8_t width() const { return puzzle().width; }
  uint8_t height() const { return puzzle().height; }
  uint8_t puzzleIndex() const { return puzzleIndex_; }
  uint8_t selectedIndex() const { return selectedIndex_; }
  Direction direction() const { return direction_; }

  bool blocked(int row, int column) const {
    return validCell(row, column) && solutionAt(row, column) == '#';
  }

  char entryAt(int row, int column) const {
    return validCell(row, column) ? entries_[row * width() + column] : '\0';
  }

  char solutionAt(int row, int column) const {
    if (!validCell(row, column)) return '#';
    return crossword_puzzles::kSolutions[puzzle().solutionOffset +
                                         row * width() + column];
  }

  bool selected(int row, int column) const {
    return validCell(row, column) &&
           selectedIndex_ == row * width() + column;
  }

  int cellNumber(int row, int column) const {
    if (!validCell(row, column) || blocked(row, column)) return 0;
    int number = 0;
    for (int index = 0; index <= row * width() + column; ++index) {
      if (clueStartsAt(index)) ++number;
    }
    return number;
  }

  int currentClueNumber() const {
    const int clueIndex = currentClueIndex();
    if (clueIndex < 0) return 0;
    const crossword_puzzles::Clue& clue = clueAt(clueIndex);
    return cellNumber(clue.startIndex / width(), clue.startIndex % width());
  }

  const char* currentClueText() const {
    const int clueIndex = currentClueIndex();
    return clueIndex < 0
               ? ""
               : &crossword_puzzles::kClueText[clueAt(clueIndex).textOffset];
  }

  bool solved() const {
    const int count = width() * height();
    for (int index = 0; index < count; ++index) {
      const char solution =
          crossword_puzzles::kSolutions[puzzle().solutionOffset + index];
      if (solution != '#' && entries_[index] != solution) return false;
    }
    return true;
  }

  Snapshot snapshot() const {
    Snapshot result = {};
    for (int index = 0; index < kMaxCells; ++index) {
      result.entries[index] = entries_[index];
    }
    result.puzzleIndex = puzzleIndex_;
    result.selectedIndex = selectedIndex_;
    result.direction = static_cast<uint8_t>(direction_);
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.puzzleIndex >= kPuzzleCount ||
        snapshot.direction > static_cast<uint8_t>(Direction::Down)) {
      return false;
    }
    const crossword_puzzles::Puzzle& restoredPuzzle =
        crossword_puzzles::kPuzzles[snapshot.puzzleIndex];
    const int cellCount = restoredPuzzle.width * restoredPuzzle.height;
    if ((snapshot.selectedIndex != kNoSelection &&
         snapshot.selectedIndex >= cellCount) ||
        restoredPuzzle.width == 0 || restoredPuzzle.width > kMaxSize ||
        restoredPuzzle.height == 0 || restoredPuzzle.height > kMaxSize) {
      return false;
    }
    for (int index = 0; index < kMaxCells; ++index) {
      const char entry = snapshot.entries[index];
      if ((entry != '\0' && (entry < 'A' || entry > 'Z')) ||
          (index >= cellCount && entry != '\0')) {
        return false;
      }
      if (index < cellCount &&
          crossword_puzzles::kSolutions[restoredPuzzle.solutionOffset +
                                        index] == '#' &&
          entry != '\0') {
        return false;
      }
    }

    puzzleIndex_ = snapshot.puzzleIndex;
    selectedIndex_ = snapshot.selectedIndex;
    direction_ = static_cast<Direction>(snapshot.direction);
    for (int index = 0; index < kMaxCells; ++index) {
      entries_[index] = snapshot.entries[index];
    }
    if (selectedIndex_ != kNoSelection &&
        clueForCell(selectedIndex_, direction_) < 0) {
      return false;
    }
    return true;
  }

 private:
  char entries_[kMaxCells] = {};
  uint8_t puzzleIndex_ = 0;
  uint8_t selectedIndex_ = kNoSelection;
  Direction direction_ = Direction::Across;

  const crossword_puzzles::Puzzle& puzzle() const {
    return crossword_puzzles::kPuzzles[puzzleIndex_];
  }

  const crossword_puzzles::Clue& clueAt(int localIndex) const {
    return crossword_puzzles::kClues[puzzle().clueOffset + localIndex];
  }

  bool validCell(int row, int column) const {
    return row >= 0 && row < height() && column >= 0 && column < width();
  }

  bool clueStartsAt(int index) const {
    for (int clueIndex = 0; clueIndex < puzzle().clueCount; ++clueIndex) {
      if (clueAt(clueIndex).startIndex == index) return true;
    }
    return false;
  }

  bool clueContains(const crossword_puzzles::Clue& clue, int index) const {
    const int step = clue.direction == 0 ? 1 : width();
    for (int offset = 0; offset < clue.length; ++offset) {
      if (clue.startIndex + offset * step == index) return true;
    }
    return false;
  }

  int clueForCell(int index, Direction direction) const {
    for (int clueIndex = 0; clueIndex < puzzle().clueCount; ++clueIndex) {
      const crossword_puzzles::Clue& clue = clueAt(clueIndex);
      if (clue.direction == static_cast<uint8_t>(direction) &&
          clueContains(clue, index)) {
        return clueIndex;
      }
    }
    return -1;
  }

  int currentClueIndex() const {
    return selectedIndex_ == kNoSelection
               ? -1
               : clueForCell(selectedIndex_, direction_);
  }

  bool moveSelection(int delta) {
    const int clueIndex = currentClueIndex();
    if (clueIndex < 0) return false;
    const crossword_puzzles::Clue& clue = clueAt(clueIndex);
    const int step = clue.direction == 0 ? 1 : width();
    const int position = (selectedIndex_ - clue.startIndex) / step;
    const int nextPosition = position + delta;
    if (nextPosition < 0 || nextPosition >= clue.length) return false;
    selectedIndex_ =
        static_cast<uint8_t>(clue.startIndex + nextPosition * step);
    return true;
  }

  void clearEntries() {
    for (char& entry : entries_) entry = '\0';
  }
};
