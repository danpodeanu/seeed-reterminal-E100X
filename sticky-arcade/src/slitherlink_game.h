#pragma once

#include <stdint.h>

class SlitherlinkGame {
 public:
  static constexpr int kSize = 5;
  static constexpr int kCellRows = 5;
  static constexpr int kCellColumns = 5;
  static constexpr int kDotRows = kCellRows + 1;
  static constexpr int kDotColumns = kCellColumns + 1;
  static constexpr int kCellCount = kCellRows * kCellColumns;
  static constexpr int kHorizontalEdgeCount = kDotRows * kCellColumns;
  static constexpr int kVerticalEdgeCount = kCellRows * kDotColumns;
  static constexpr uint8_t kPuzzleCount = 5;

  enum EdgeState : uint8_t { Blank = 0, Line = 1, Cross = 2 };

  struct Snapshot {
    uint64_t horizontalEdges;
    uint64_t verticalEdges;
    uint8_t puzzleIndex;
  };

  SlitherlinkGame() { loadPuzzle(0); }

  void start(uint8_t puzzleIndex = 0) {
    loadPuzzle(puzzleIndex < kPuzzleCount ? puzzleIndex : 0);
  }

  int8_t clue(int row, int column) const { return clueAt(row, column); }

  EdgeState horizontalEdge(int row, int column) const {
    return horizontalEdgeAt(row, column);
  }

  EdgeState verticalEdge(int row, int column) const {
    return verticalEdgeAt(row, column);
  }

  EdgeState horizontalEdgeAt(int dotRow, int column) const {
    if (!validHorizontalEdge(dotRow, column)) return Blank;
    return unpackEdgeState(horizontalEdges_, dotRow * kCellColumns + column);
  }

  EdgeState verticalEdgeAt(int row, int dotColumn) const {
    if (!validVerticalEdge(row, dotColumn)) return Blank;
    return unpackEdgeState(verticalEdges_, row * kDotColumns + dotColumn);
  }

  bool setHorizontalEdge(int dotRow, int column, EdgeState state) {
    if (solved_ || !validHorizontalEdge(dotRow, column) ||
        !validEdgeState(state)) {
      return false;
    }
    horizontalEdges_ =
        packEdgeState(horizontalEdges_, dotRow * kCellColumns + column, state);
    updateSolved();
    return true;
  }

  bool setVerticalEdge(int row, int dotColumn, EdgeState state) {
    if (solved_ || !validVerticalEdge(row, dotColumn) ||
        !validEdgeState(state)) {
      return false;
    }
    verticalEdges_ =
        packEdgeState(verticalEdges_, row * kDotColumns + dotColumn, state);
    updateSolved();
    return true;
  }

  bool toggleHorizontalEdge(int dotRow, int column) {
    if (!validHorizontalEdge(dotRow, column) || solved_) return false;
    return setHorizontalEdge(
        dotRow, column,
        nextState(horizontalEdgeAt(dotRow, column)));
  }

  bool cycleHorizontal(int row, int column) {
    return toggleHorizontalEdge(row, column);
  }

  bool toggleVerticalEdge(int row, int dotColumn) {
    if (!validVerticalEdge(row, dotColumn) || solved_) return false;
    return setVerticalEdge(row, dotColumn,
                           nextState(verticalEdgeAt(row, dotColumn)));
  }

  bool cycleVertical(int row, int column) {
    return toggleVerticalEdge(row, column);
  }

  void reset() {
    horizontalEdges_ = 0;
    verticalEdges_ = 0;
    solved_ = false;
  }

  void nextPuzzle() {
    loadPuzzle(static_cast<uint8_t>((puzzleIndex_ + 1U) % kPuzzleCount));
  }

  uint8_t puzzleIndex() const { return puzzleIndex_; }
  static constexpr uint8_t puzzleCount() { return kPuzzleCount; }

  int8_t clueAt(int row, int column) const {
    if (!validCell(row, column)) return -1;
    return kPuzzles[puzzleIndex_].clues[row * kCellColumns + column];
  }

  uint8_t clueLineCount(int row, int column) const {
    if (!validCell(row, column)) return 0;
    return lineCountAroundCell(horizontalEdges_, verticalEdges_, row, column);
  }

  bool clueSatisfied(int row, int column) const {
    const int8_t clue = clueAt(row, column);
    return clue < 0 || clueLineCount(row, column) == static_cast<uint8_t>(clue);
  }

  bool solved() const { return solved_; }

  Snapshot snapshot() const {
    return {horizontalEdges_, verticalEdges_, puzzleIndex_};
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.puzzleIndex >= kPuzzleCount ||
        !validatePackedEdges(snapshot.horizontalEdges, kHorizontalEdgeCount) ||
        !validatePackedEdges(snapshot.verticalEdges, kVerticalEdgeCount) ||
        !validatePuzzle(kPuzzles[snapshot.puzzleIndex])) {
      return false;
    }

    horizontalEdges_ = snapshot.horizontalEdges;
    verticalEdges_ = snapshot.verticalEdges;
    puzzleIndex_ = snapshot.puzzleIndex;
    updateSolved();
    return true;
  }

 private:
  struct PuzzleDef {
    int8_t clues[kCellCount];
  };

  inline static constexpr PuzzleDef kPuzzles[kPuzzleCount] = {
      {{-1, 1, 1, 1, -1, 1, 2, 1, 2, 1, 1, 1, -1, 1, 1, 1, 2, 1, 2, 1, -1, 1,
        1, 1, -1}},
      {{2, 1, 1, 1, 2, 1, 0, -1, 0, 1, 1, -1, -1, -1, 1, 1, 0, -1, 0, 1, 2,
        1, 1, 1, 2}},
      {{1, 2, -1, 2, 1, 1, 1, 0, -1, 1, 2, -1, 1, 2, 0, 2, 0, 1, -1, 1, 2, 1,
        1, 2, 3}},
      {{1, 2, 2, 3, 1, 2, -1, 2, 1, 0, -1, 1, 2, -1, 1, 1, 0, 1, 1, 2, 2, 1,
        -1, 1, 2}},
      {{1, 1, 2, 2, 2, 3, 2, -1, 1, 1, 1, -1, 1, 2, 2, 0, 1, 1, -1, 2, 1, 3,
        2, 2, 2}},
  };

  uint64_t horizontalEdges_ = 0;
  uint64_t verticalEdges_ = 0;
  uint8_t puzzleIndex_ = 0;
  bool solved_ = false;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kCellRows && column >= 0 && column < kCellColumns;
  }

  static bool validHorizontalEdge(int dotRow, int column) {
    return dotRow >= 0 && dotRow < kDotRows && column >= 0 &&
           column < kCellColumns;
  }

  static bool validVerticalEdge(int row, int dotColumn) {
    return row >= 0 && row < kCellRows && dotColumn >= 0 &&
           dotColumn < kDotColumns;
  }

  static bool validEdgeState(EdgeState state) {
    return state == Blank || state == Line || state == Cross;
  }

  static uint64_t packedEdgeMask(int edgeCount) {
    return (1ULL << (edgeCount * 2)) - 1ULL;
  }

  static EdgeState unpackEdgeState(uint64_t packedEdges, int index) {
    return static_cast<EdgeState>((packedEdges >> (index * 2)) & 0x3ULL);
  }

  static uint64_t packEdgeState(uint64_t packedEdges, int index,
                                EdgeState state) {
    const uint64_t shift = static_cast<uint64_t>(index * 2);
    const uint64_t mask = 0x3ULL << shift;
    return (packedEdges & ~mask) |
           (static_cast<uint64_t>(state) << shift);
  }

  static EdgeState nextState(EdgeState state) {
    return state == Blank ? Line : state == Line ? Cross : Blank;
  }

  static bool validatePackedEdges(uint64_t packedEdges, int edgeCount) {
    if ((packedEdges & ~packedEdgeMask(edgeCount)) != 0) return false;
    for (int index = 0; index < edgeCount; ++index) {
      if (((packedEdges >> (index * 2)) & 0x3ULL) == 0x3ULL) return false;
    }
    return true;
  }

  static bool validatePuzzle(const PuzzleDef& puzzle) {
    for (int index = 0; index < kCellCount; ++index) {
      const int8_t clue = puzzle.clues[index];
      if (clue < -1 || clue > 3) return false;
    }
    return true;
  }

  static uint8_t lineCountAroundCell(uint64_t horizontalEdges,
                                     uint64_t verticalEdges, int row,
                                     int column) {
    uint8_t count = 0;
    if (unpackEdgeState(horizontalEdges, row * kCellColumns + column) ==
        Line) {
      ++count;
    }
    if (unpackEdgeState(horizontalEdges, (row + 1) * kCellColumns + column) ==
        Line) {
      ++count;
    }
    if (unpackEdgeState(verticalEdges, row * kDotColumns + column) == Line) {
      ++count;
    }
    if (unpackEdgeState(verticalEdges, row * kDotColumns + column + 1) ==
        Line) {
      ++count;
    }
    return count;
  }

  static bool cluesSatisfied(const PuzzleDef& puzzle, uint64_t horizontalEdges,
                             uint64_t verticalEdges) {
    for (int row = 0; row < kCellRows; ++row) {
      for (int column = 0; column < kCellColumns; ++column) {
        const int8_t clue = puzzle.clues[row * kCellColumns + column];
        if (clue >= 0 &&
            lineCountAroundCell(horizontalEdges, verticalEdges, row, column) !=
                static_cast<uint8_t>(clue)) {
          return false;
        }
      }
    }
    return true;
  }

  static bool singleClosedLoop(uint64_t horizontalEdges, uint64_t verticalEdges) {
    uint8_t degrees[kDotRows * kDotColumns] = {};
    bool visited[kDotRows * kDotColumns] = {};
    uint8_t stack[kDotRows * kDotColumns] = {};
    int usedVertexCount = 0;
    int lineEdgeCount = 0;

    for (int row = 0; row < kDotRows; ++row) {
      for (int column = 0; column < kCellColumns; ++column) {
        if (unpackEdgeState(horizontalEdges, row * kCellColumns + column) !=
            Line) {
          continue;
        }
        ++lineEdgeCount;
        const int left = row * kDotColumns + column;
        const int right = left + 1;
        if (++degrees[left] > 2 || ++degrees[right] > 2) return false;
      }
    }

    for (int row = 0; row < kCellRows; ++row) {
      for (int column = 0; column < kDotColumns; ++column) {
        if (unpackEdgeState(verticalEdges, row * kDotColumns + column) !=
            Line) {
          continue;
        }
        ++lineEdgeCount;
        const int top = row * kDotColumns + column;
        const int bottom = top + kDotColumns;
        if (++degrees[top] > 2 || ++degrees[bottom] > 2) return false;
      }
    }

    if (lineEdgeCount == 0) return false;

    int startVertex = -1;
    for (int vertex = 0; vertex < kDotRows * kDotColumns; ++vertex) {
      if (degrees[vertex] == 0) continue;
      if (degrees[vertex] != 2) return false;
      if (startVertex < 0) startVertex = vertex;
      ++usedVertexCount;
    }
    if (startVertex < 0) return false;

    int stackSize = 0;
    int visitedVertexCount = 0;
    stack[stackSize++] = static_cast<uint8_t>(startVertex);
    visited[startVertex] = true;
    while (stackSize > 0) {
      const int vertex = stack[--stackSize];
      ++visitedVertexCount;
      const int row = vertex / kDotColumns;
      const int column = vertex % kDotColumns;

      if (column > 0 &&
          unpackEdgeState(horizontalEdges, row * kCellColumns + column - 1) ==
              Line) {
        const int neighbor = vertex - 1;
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          stack[stackSize++] = static_cast<uint8_t>(neighbor);
        }
      }
      if (column < kCellColumns &&
          unpackEdgeState(horizontalEdges, row * kCellColumns + column) ==
              Line) {
        const int neighbor = vertex + 1;
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          stack[stackSize++] = static_cast<uint8_t>(neighbor);
        }
      }
      if (row > 0 &&
          unpackEdgeState(verticalEdges, (row - 1) * kDotColumns + column) ==
              Line) {
        const int neighbor = vertex - kDotColumns;
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          stack[stackSize++] = static_cast<uint8_t>(neighbor);
        }
      }
      if (row < kCellRows &&
          unpackEdgeState(verticalEdges, row * kDotColumns + column) ==
              Line) {
        const int neighbor = vertex + kDotColumns;
        if (!visited[neighbor]) {
          visited[neighbor] = true;
          stack[stackSize++] = static_cast<uint8_t>(neighbor);
        }
      }
    }

    return visitedVertexCount == usedVertexCount;
  }

  void updateSolved() {
    solved_ = cluesSatisfied(kPuzzles[puzzleIndex_], horizontalEdges_,
                             verticalEdges_) &&
              singleClosedLoop(horizontalEdges_, verticalEdges_);
  }

  void loadPuzzle(uint8_t puzzleIndex) {
    puzzleIndex_ =
        puzzleIndex < kPuzzleCount && validatePuzzle(kPuzzles[puzzleIndex])
            ? puzzleIndex
            : 0;
    reset();
  }
};
