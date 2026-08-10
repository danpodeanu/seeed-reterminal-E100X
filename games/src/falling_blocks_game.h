#pragma once

#include <stdint.h>

#include <algorithm>

class FallingBlocksGame {
 public:
  static constexpr int kWidth = 10;
  static constexpr int kHeight = 16;
  static constexpr int kCellCount = kWidth * kHeight;
  static constexpr uint8_t kPieceCount = 7;
  static constexpr uint8_t kNoPiece = kPieceCount;

  enum class Action : uint8_t {
    MoveLeft,
    MoveRight,
    RotateClockwise,
    SoftDrop,
    HardDrop,
  };

  struct Snapshot {
    uint8_t cells[kCellCount];
    uint32_t initialSeed;
    uint32_t randomState;
    uint32_t score;
    uint16_t lines;
    uint8_t activePiece;
    uint8_t nextPiece;
    uint8_t rotation;
    int8_t activeRow;
    int8_t activeColumn;
    uint8_t gameOver;
  };

  FallingBlocksGame() { start(1); }

  void start(uint32_t seed) {
    std::fill(cells_, cells_ + kCellCount, 0);
    initialSeed_ = seed == 0 ? 0xA341316CUL : seed;
    randomState_ = initialSeed_;
    score_ = 0;
    lines_ = 0;
    gameOver_ = false;
    activePiece_ = drawPiece();
    nextPiece_ = drawPiece();
    spawnActivePiece();
  }

  void reset() { start(initialSeed_); }

  bool turn(Action action) {
    if (gameOver_) return false;
    if (action == Action::HardDrop) {
      int distance = 0;
      while (tryMove(1, 0)) ++distance;
      score_ += static_cast<uint32_t>(distance * 2);
      lockActivePiece();
      return true;
    }
    if (action == Action::SoftDrop) {
      if (tryMove(1, 0)) {
        ++score_;
      } else {
        lockActivePiece();
      }
      return true;
    }

    if (action == Action::MoveLeft) {
      tryMove(0, -1);
    } else if (action == Action::MoveRight) {
      tryMove(0, 1);
    } else if (action == Action::RotateClockwise) {
      tryRotate();
    }
    if (tryMove(1, 0)) return true;
    lockActivePiece();
    return true;
  }

  uint8_t at(int row, int column) const {
    if (!validCell(row, column)) return 0;
    if (!gameOver_ && pieceOccupies(activePiece_, rotation_, activeRow_,
                                    activeColumn_, row, column)) {
      return activePiece_ + 1;
    }
    return cells_[row * kWidth + column];
  }

  uint8_t settledAt(int row, int column) const {
    return validCell(row, column) ? cells_[row * kWidth + column] : 0;
  }

  uint8_t activePiece() const { return activePiece_; }
  uint8_t nextPiece() const { return nextPiece_; }
  uint8_t rotation() const { return rotation_; }
  int activeRow() const { return activeRow_; }
  int activeColumn() const { return activeColumn_; }
  uint32_t score() const { return score_; }
  uint16_t lines() const { return lines_; }
  uint16_t level() const { return static_cast<uint16_t>(lines_ / 10 + 1); }
  bool gameOver() const { return gameOver_; }

  static bool pieceCell(uint8_t piece, uint8_t rotation, int row, int column) {
    if (piece >= kPieceCount || row < 0 || row >= 4 || column < 0 ||
        column >= 4) {
      return false;
    }
    return (shapeMask(piece, rotation) &
            (static_cast<uint16_t>(1U) << (row * 4 + column))) != 0;
  }

  Snapshot snapshot() const {
    Snapshot result = {};
    std::copy(cells_, cells_ + kCellCount, result.cells);
    result.initialSeed = initialSeed_;
    result.randomState = randomState_;
    result.score = score_;
    result.lines = lines_;
    result.activePiece = activePiece_;
    result.nextPiece = nextPiece_;
    result.rotation = rotation_;
    result.activeRow = static_cast<int8_t>(activeRow_);
    result.activeColumn = static_cast<int8_t>(activeColumn_);
    result.gameOver = gameOver_ ? 1 : 0;
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.initialSeed == 0 || snapshot.randomState == 0 ||
        snapshot.nextPiece >= kPieceCount || snapshot.rotation >= 4 ||
        snapshot.gameOver > 1) {
      return false;
    }
    for (uint8_t value : snapshot.cells) {
      if (value > kPieceCount) return false;
    }

    FallingBlocksGame candidate;
    std::copy(snapshot.cells, snapshot.cells + kCellCount, candidate.cells_);
    candidate.initialSeed_ = snapshot.initialSeed;
    candidate.randomState_ = snapshot.randomState;
    candidate.score_ = snapshot.score;
    candidate.lines_ = snapshot.lines;
    candidate.activePiece_ = snapshot.activePiece;
    candidate.nextPiece_ = snapshot.nextPiece;
    candidate.rotation_ = snapshot.rotation;
    candidate.activeRow_ = snapshot.activeRow;
    candidate.activeColumn_ = snapshot.activeColumn;
    candidate.gameOver_ = snapshot.gameOver != 0;

    if (candidate.gameOver_) {
      if (candidate.activePiece_ != kNoPiece) return false;
    } else if (candidate.activePiece_ >= kPieceCount ||
               !candidate.canPlace(candidate.activePiece_, candidate.rotation_,
                                   candidate.activeRow_,
                                   candidate.activeColumn_)) {
      return false;
    }
    *this = candidate;
    return true;
  }

 private:
  uint8_t cells_[kCellCount] = {};
  uint32_t initialSeed_ = 1;
  uint32_t randomState_ = 1;
  uint32_t score_ = 0;
  uint16_t lines_ = 0;
  uint8_t activePiece_ = 0;
  uint8_t nextPiece_ = 0;
  uint8_t rotation_ = 0;
  int activeRow_ = 0;
  int activeColumn_ = 3;
  bool gameOver_ = false;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kHeight && column >= 0 && column < kWidth;
  }

  static uint16_t shapeMask(uint8_t piece, uint8_t rotation) {
    rotation &= 3U;
    switch (piece) {
      case 0:
        return (rotation & 1U) == 0 ? 0x000F : 0x2222;
      case 1:
        return 0x0033;
      case 2:
        switch (rotation) {
          case 0:
            return 0x0027;
          case 1:
            return 0x0262;
          case 2:
            return 0x0072;
          default:
            return 0x0232;
        }
      case 3:
        return (rotation & 1U) == 0 ? 0x0036 : 0x0231;
      case 4:
        return (rotation & 1U) == 0 ? 0x0063 : 0x0132;
      case 5:
        switch (rotation) {
          case 0:
            return 0x0071;
          case 1:
            return 0x0113;
          case 2:
            return 0x0047;
          default:
            return 0x0322;
        }
      case 6:
        switch (rotation) {
          case 0:
            return 0x0074;
          case 1:
            return 0x0311;
          case 2:
            return 0x0017;
          default:
            return 0x0223;
        }
      default:
        return 0;
    }
  }

  static bool pieceOccupies(uint8_t piece, uint8_t rotation, int pieceRow,
                            int pieceColumn, int row, int column) {
    return pieceCell(piece, rotation, row - pieceRow, column - pieceColumn);
  }

  bool canPlace(uint8_t piece, uint8_t rotation, int row, int column) const {
    for (int shapeRow = 0; shapeRow < 4; ++shapeRow) {
      for (int shapeColumn = 0; shapeColumn < 4; ++shapeColumn) {
        if (!pieceCell(piece, rotation, shapeRow, shapeColumn)) continue;
        const int boardRow = row + shapeRow;
        const int boardColumn = column + shapeColumn;
        if (!validCell(boardRow, boardColumn) ||
            cells_[boardRow * kWidth + boardColumn] != 0) {
          return false;
        }
      }
    }
    return true;
  }

  bool tryMove(int rowOffset, int columnOffset) {
    const int row = activeRow_ + rowOffset;
    const int column = activeColumn_ + columnOffset;
    if (!canPlace(activePiece_, rotation_, row, column)) return false;
    activeRow_ = row;
    activeColumn_ = column;
    return true;
  }

  bool tryRotate() {
    const uint8_t rotation = static_cast<uint8_t>((rotation_ + 1U) & 3U);
    constexpr int kColumnKicks[] = {0, -1, 1, -2, 2};
    for (int offset : kColumnKicks) {
      if (!canPlace(activePiece_, rotation, activeRow_,
                    activeColumn_ + offset)) {
        continue;
      }
      rotation_ = rotation;
      activeColumn_ += offset;
      return true;
    }
    return false;
  }

  uint32_t nextRandom() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return randomState_;
  }

  uint8_t drawPiece() {
    return static_cast<uint8_t>(nextRandom() % kPieceCount);
  }

  void spawnActivePiece() {
    rotation_ = 0;
    activeRow_ = 0;
    activeColumn_ = (kWidth - 4) / 2;
    if (!canPlace(activePiece_, rotation_, activeRow_, activeColumn_)) {
      activePiece_ = kNoPiece;
      gameOver_ = true;
    }
  }

  int clearCompleteLines() {
    int cleared = 0;
    for (int row = kHeight - 1; row >= 0; --row) {
      bool complete = true;
      for (int column = 0; column < kWidth; ++column) {
        if (cells_[row * kWidth + column] == 0) {
          complete = false;
          break;
        }
      }
      if (!complete) continue;

      for (int destination = row; destination > 0; --destination) {
        std::copy(cells_ + (destination - 1) * kWidth,
                  cells_ + destination * kWidth,
                  cells_ + destination * kWidth);
      }
      std::fill(cells_, cells_ + kWidth, 0);
      ++cleared;
      ++row;
    }
    return cleared;
  }

  void lockActivePiece() {
    for (int shapeRow = 0; shapeRow < 4; ++shapeRow) {
      for (int shapeColumn = 0; shapeColumn < 4; ++shapeColumn) {
        if (!pieceCell(activePiece_, rotation_, shapeRow, shapeColumn)) {
          continue;
        }
        cells_[(activeRow_ + shapeRow) * kWidth +
               activeColumn_ + shapeColumn] = activePiece_ + 1;
      }
    }

    const int cleared = clearCompleteLines();
    if (cleared > 0) {
      static constexpr uint16_t kLineScores[] = {0, 100, 300, 500, 800};
      score_ += static_cast<uint32_t>(kLineScores[cleared]) * level();
      lines_ = static_cast<uint16_t>(
          std::min<uint32_t>(UINT16_MAX, lines_ + cleared));
    }
    activePiece_ = nextPiece_;
    nextPiece_ = drawPiece();
    spawnActivePiece();
  }
};
