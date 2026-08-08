#pragma once

#include <stdint.h>

class ReversiGame {
 public:
  static constexpr int kSize = 8;
  static constexpr int kCellCount = kSize * kSize;

  enum class Disc : uint8_t {
    Empty = 0,
    Black = 1,
    White = 2,
  };

  enum class MoveResult : uint8_t {
    Illegal,
    Moved,
    OpponentPassed,
    GameOver,
  };

  struct Snapshot {
    uint64_t black;
    uint64_t white;
    uint8_t currentPlayer;
  };

  void start() {
    black_ = cellBit(3, 4) | cellBit(4, 3);
    white_ = cellBit(3, 3) | cellBit(4, 4);
    currentPlayer_ = Disc::Black;
  }

  void reset() { start(); }

  Disc at(int row, int column) const {
    if (!validCell(row, column)) return Disc::Empty;
    const uint64_t bit = cellBit(row, column);
    if ((black_ & bit) != 0) return Disc::Black;
    if ((white_ & bit) != 0) return Disc::White;
    return Disc::Empty;
  }

  Disc currentPlayer() const { return currentPlayer_; }

  bool isLegalMove(int row, int column) const {
    return validCell(row, column) &&
           flipsFor(row, column, currentPlayer_) != 0;
  }

  uint64_t legalMoves() const { return legalMovesFor(currentPlayer_); }

  MoveResult play(int row, int column) {
    const uint64_t flips = flipsFor(row, column, currentPlayer_);
    if (flips == 0) return MoveResult::Illegal;

    const uint64_t placed = cellBit(row, column);
    const Disc mover = currentPlayer_;
    if (mover == Disc::Black) {
      black_ |= placed | flips;
      white_ &= ~flips;
    } else {
      white_ |= placed | flips;
      black_ &= ~flips;
    }

    const Disc opponent = other(mover);
    if (legalMovesFor(opponent) != 0) {
      currentPlayer_ = opponent;
      return MoveResult::Moved;
    }
    currentPlayer_ = mover;
    return legalMovesFor(mover) != 0 ? MoveResult::OpponentPassed
                                     : MoveResult::GameOver;
  }

  int score(Disc player) const {
    const uint64_t cells =
        player == Disc::Black ? black_ : player == Disc::White ? white_ : 0;
    int count = 0;
    for (uint64_t remaining = cells; remaining != 0; remaining >>= 1) {
      count += static_cast<int>(remaining & 1ULL);
    }
    return count;
  }

  bool gameOver() const {
    return legalMovesFor(Disc::Black) == 0 &&
           legalMovesFor(Disc::White) == 0;
  }

  Disc winner() const {
    const int blackScore = score(Disc::Black);
    const int whiteScore = score(Disc::White);
    return blackScore > whiteScore
               ? Disc::Black
               : whiteScore > blackScore ? Disc::White : Disc::Empty;
  }

  Snapshot snapshot() const {
    return {black_, white_, static_cast<uint8_t>(currentPlayer_)};
  }

  bool restore(const Snapshot& snapshot) {
    if ((snapshot.black & snapshot.white) != 0 ||
        snapshot.currentPlayer < static_cast<uint8_t>(Disc::Black) ||
        snapshot.currentPlayer > static_cast<uint8_t>(Disc::White) ||
        bitCount(snapshot.black | snapshot.white) < 4) {
      return false;
    }

    const Disc restoredPlayer = static_cast<Disc>(snapshot.currentPlayer);
    if (legalMovesFor(snapshot.black, snapshot.white, restoredPlayer) == 0 &&
        legalMovesFor(snapshot.black, snapshot.white, other(restoredPlayer)) !=
            0) {
      return false;
    }
    black_ = snapshot.black;
    white_ = snapshot.white;
    currentPlayer_ = restoredPlayer;
    return true;
  }

 private:
  uint64_t black_ = 0;
  uint64_t white_ = 0;
  Disc currentPlayer_ = Disc::Black;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  static uint64_t cellBit(int row, int column) {
    return 1ULL << (row * kSize + column);
  }

  static Disc other(Disc player) {
    return player == Disc::Black ? Disc::White : Disc::Black;
  }

  static int bitCount(uint64_t cells) {
    int count = 0;
    while (cells != 0) {
      count += static_cast<int>(cells & 1ULL);
      cells >>= 1;
    }
    return count;
  }

  uint64_t flipsFor(int row, int column, Disc player) const {
    return flipsFor(black_, white_, row, column, player);
  }

  static uint64_t flipsFor(uint64_t black, uint64_t white, int row, int column,
                           Disc player) {
    if (!validCell(row, column)) return 0;
    const uint64_t occupied = black | white;
    if ((occupied & cellBit(row, column)) != 0) return 0;

    const uint64_t own = player == Disc::Black ? black : white;
    const uint64_t opponent = player == Disc::Black ? white : black;
    uint64_t flips = 0;
    for (int rowDirection = -1; rowDirection <= 1; ++rowDirection) {
      for (int columnDirection = -1; columnDirection <= 1;
           ++columnDirection) {
        if (rowDirection == 0 && columnDirection == 0) continue;
        int scanRow = row + rowDirection;
        int scanColumn = column + columnDirection;
        uint64_t line = 0;
        while (validCell(scanRow, scanColumn) &&
               (opponent & cellBit(scanRow, scanColumn)) != 0) {
          line |= cellBit(scanRow, scanColumn);
          scanRow += rowDirection;
          scanColumn += columnDirection;
        }
        if (line != 0 && validCell(scanRow, scanColumn) &&
            (own & cellBit(scanRow, scanColumn)) != 0) {
          flips |= line;
        }
      }
    }
    return flips;
  }

  uint64_t legalMovesFor(Disc player) const {
    return legalMovesFor(black_, white_, player);
  }

  static uint64_t legalMovesFor(uint64_t black, uint64_t white, Disc player) {
    uint64_t moves = 0;
    for (int row = 0; row < kSize; ++row) {
      for (int column = 0; column < kSize; ++column) {
        if (flipsFor(black, white, row, column, player) != 0) {
          moves |= cellBit(row, column);
        }
      }
    }
    return moves;
  }
};
