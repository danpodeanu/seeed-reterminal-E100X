#pragma once

#include <stdint.h>

class ConnectFourGame {
 public:
  static constexpr int kRows = 6;
  static constexpr int kColumns = 7;
  static constexpr int kCellCount = kRows * kColumns;

  enum class Disc : uint8_t {
    Empty = 0,
    Red = 1,
    Yellow = 2,
  };

  struct Snapshot {
    uint64_t red;
    uint64_t yellow;
    uint8_t currentPlayer;
  };

  void start() { reset(); }

  void reset() {
    red_ = 0;
    yellow_ = 0;
    currentPlayer_ = Disc::Red;
  }

  Disc at(int row, int column) const {
    if (!validCell(row, column)) return Disc::Empty;
    const uint64_t bit = cellBit(row, column);
    if ((red_ & bit) != 0) return Disc::Red;
    if ((yellow_ & bit) != 0) return Disc::Yellow;
    return Disc::Empty;
  }

  Disc currentPlayer() const { return currentPlayer_; }

  bool canDrop(int column) const {
    return column >= 0 && column < kColumns && !gameOver() &&
           ((red_ | yellow_) & cellBit(0, column)) == 0;
  }

  bool drop(int column) {
    if (!canDrop(column)) return false;
    const uint64_t bit = landingBit(column);
    if (currentPlayer_ == Disc::Red) {
      red_ |= bit;
      currentPlayer_ = Disc::Yellow;
    } else {
      yellow_ |= bit;
      currentPlayer_ = Disc::Red;
    }
    return true;
  }

  uint8_t moveCount() const {
    return static_cast<uint8_t>(bitCount(red_ | yellow_));
  }

  bool gameOver() const {
    return hasWin(red_) || hasWin(yellow_) || moveCount() == kCellCount;
  }

  Disc winner() const {
    return hasWin(red_)      ? Disc::Red
           : hasWin(yellow_) ? Disc::Yellow
                             : Disc::Empty;
  }

  bool tied() const {
    return moveCount() == kCellCount && winner() == Disc::Empty;
  }

  Snapshot snapshot() const {
    return {red_, yellow_, static_cast<uint8_t>(currentPlayer_)};
  }

  bool restore(const Snapshot& snapshot) {
    if (((snapshot.red | snapshot.yellow) & ~kBoardMask) != 0 ||
        (snapshot.red & snapshot.yellow) != 0 ||
        !validPlayer(snapshot.currentPlayer) ||
        !hasGravity(snapshot.red | snapshot.yellow)) {
      return false;
    }

    const int redCount = bitCount(snapshot.red);
    const int yellowCount = bitCount(snapshot.yellow);
    const Disc restoredPlayer = static_cast<Disc>(snapshot.currentPlayer);
    if (!((redCount == yellowCount && restoredPlayer == Disc::Red) ||
          (redCount == yellowCount + 1 &&
           restoredPlayer == Disc::Yellow))) {
      return false;
    }

    const bool redWon = hasWin(snapshot.red);
    const bool yellowWon = hasWin(snapshot.yellow);
    if ((redWon && yellowWon) ||
        (redWon && (redCount != yellowCount + 1 ||
                    !validWinningLastMove(snapshot.red, snapshot.yellow))) ||
        (yellowWon && (redCount != yellowCount ||
                       !validWinningLastMove(snapshot.yellow, snapshot.red)))) {
      return false;
    }

    red_ = snapshot.red;
    yellow_ = snapshot.yellow;
    currentPlayer_ = restoredPlayer;
    return true;
  }

  bool chooseBestColumn(int depth, int& column) const {
    if (gameOver()) return false;
    if (depth < 1) depth = 1;

    const Disc rootPlayer = currentPlayer_;
    for (int orderIndex = 0; orderIndex < kColumns; ++orderIndex) {
      const int candidateColumn = kColumnOrder[orderIndex];
      if (isWinningDrop(rootPlayer, candidateColumn)) {
        column = candidateColumn;
        return true;
      }
    }

    const Disc opponent = other(rootPlayer);
    const bool opponentThreat = hasImmediateWin(opponent);
    bool hasSafeMove = false;
    if (opponentThreat) {
      for (int orderIndex = 0; orderIndex < kColumns; ++orderIndex) {
        const int candidateColumn = kColumnOrder[orderIndex];
        if (!canDrop(candidateColumn)) continue;
        ConnectFourGame candidate = *this;
        candidate.drop(candidateColumn);
        if (!candidate.hasImmediateWin(opponent)) {
          hasSafeMove = true;
          break;
        }
      }
    }

    int bestColumn = -1;
    int bestScore = -kSearchLimit;
    int alpha = -kSearchLimit;
    for (int orderIndex = 0; orderIndex < kColumns; ++orderIndex) {
      const int candidateColumn = kColumnOrder[orderIndex];
      if (!canDrop(candidateColumn)) continue;
      ConnectFourGame candidate = *this;
      candidate.drop(candidateColumn);
      if (opponentThreat && hasSafeMove &&
          candidate.hasImmediateWin(opponent)) {
        continue;
      }
      const int score =
          minimax(candidate, depth - 1, rootPlayer, alpha, kSearchLimit, 1);
      if (bestColumn < 0 || score > bestScore) {
        bestScore = score;
        bestColumn = candidateColumn;
      }
      if (score > alpha) alpha = score;
    }
    if (bestColumn < 0) return false;
    column = bestColumn;
    return true;
  }

 private:
  static constexpr uint64_t kBoardMask = (1ULL << kCellCount) - 1ULL;
  static constexpr int kSearchLimit = 1000000;
  static constexpr int kWinScore = 100000;
  inline static constexpr int kColumnOrder[kColumns] = {3, 2, 4, 1, 5, 0, 6};

  uint64_t red_ = 0;
  uint64_t yellow_ = 0;
  Disc currentPlayer_ = Disc::Red;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kRows && column >= 0 && column < kColumns;
  }

  static bool validPlayer(uint8_t player) {
    return player == static_cast<uint8_t>(Disc::Red) ||
           player == static_cast<uint8_t>(Disc::Yellow);
  }

  static uint64_t cellBit(int row, int column) {
    return 1ULL << (row * kColumns + column);
  }

  static Disc other(Disc disc) {
    return disc == Disc::Red ? Disc::Yellow : Disc::Red;
  }

  static int bitCount(uint64_t bits) {
    int count = 0;
    while (bits != 0) {
      bits &= bits - 1;
      ++count;
    }
    return count;
  }

  static bool hasGravity(uint64_t occupied) {
    for (int column = 0; column < kColumns; ++column) {
      bool foundEmpty = false;
      for (int row = kRows - 1; row >= 0; --row) {
        const bool occupiedCell = (occupied & cellBit(row, column)) != 0;
        if (!occupiedCell) {
          foundEmpty = true;
        } else if (foundEmpty) {
          return false;
        }
      }
    }
    return true;
  }

  static bool hasWin(uint64_t discs) {
    constexpr int directions[][2] = {
        {0, 1},
        {1, 0},
        {1, 1},
        {1, -1},
    };
    for (int row = 0; row < kRows; ++row) {
      for (int column = 0; column < kColumns; ++column) {
        if ((discs & cellBit(row, column)) == 0) continue;
        for (const auto& direction : directions) {
          const int endRow = row + direction[0] * 3;
          const int endColumn = column + direction[1] * 3;
          if (!validCell(endRow, endColumn)) continue;
          bool complete = true;
          for (int offset = 1; offset < 4; ++offset) {
            if ((discs &
                 cellBit(row + direction[0] * offset,
                         column + direction[1] * offset)) == 0) {
              complete = false;
              break;
            }
          }
          if (complete) return true;
        }
      }
    }
    return false;
  }

  static bool validWinningLastMove(uint64_t winnerDiscs,
                                   uint64_t opponentDiscs) {
    const uint64_t occupied = winnerDiscs | opponentDiscs;
    for (int column = 0; column < kColumns; ++column) {
      for (int row = 0; row < kRows; ++row) {
        const uint64_t bit = cellBit(row, column);
        if ((occupied & bit) == 0) continue;
        if ((winnerDiscs & bit) != 0 &&
            !hasWin(winnerDiscs & ~bit) && !hasWin(opponentDiscs)) {
          return true;
        }
        break;
      }
    }
    return false;
  }

  uint64_t landingBit(int column) const {
    const uint64_t occupied = red_ | yellow_;
    for (int row = kRows - 1; row >= 0; --row) {
      const uint64_t bit = cellBit(row, column);
      if ((occupied & bit) == 0) return bit;
    }
    return 0;
  }

  bool isWinningDrop(Disc player, int column) const {
    if (column < 0 || column >= kColumns ||
        ((red_ | yellow_) & cellBit(0, column)) != 0) {
      return false;
    }
    const uint64_t placed = landingBit(column);
    const uint64_t discs = player == Disc::Red ? red_ : yellow_;
    return hasWin(discs | placed);
  }

  bool hasImmediateWin(Disc player) const {
    for (int column = 0; column < kColumns; ++column) {
      if (isWinningDrop(player, column)) return true;
    }
    return false;
  }

  static int evaluate(const ConnectFourGame& game, Disc rootPlayer) {
    const uint64_t root =
        rootPlayer == Disc::Red ? game.red_ : game.yellow_;
    const uint64_t opponent =
        rootPlayer == Disc::Red ? game.yellow_ : game.red_;
    int score = 0;

    for (int row = 0; row < kRows; ++row) {
      const uint64_t centerBit = cellBit(row, kColumns / 2);
      if ((root & centerBit) != 0) {
        score += 6;
      } else if ((opponent & centerBit) != 0) {
        score -= 6;
      }
    }

    constexpr int directions[][2] = {
        {0, 1},
        {1, 0},
        {1, 1},
        {1, -1},
    };
    constexpr int weights[] = {0, 1, 10, 70, kWinScore};
    for (int row = 0; row < kRows; ++row) {
      for (int column = 0; column < kColumns; ++column) {
        for (const auto& direction : directions) {
          const int endRow = row + direction[0] * 3;
          const int endColumn = column + direction[1] * 3;
          if (!validCell(endRow, endColumn)) continue;
          int rootCount = 0;
          int opponentCount = 0;
          for (int offset = 0; offset < 4; ++offset) {
            const uint64_t bit =
                cellBit(row + direction[0] * offset,
                        column + direction[1] * offset);
            if ((root & bit) != 0) {
              ++rootCount;
            } else if ((opponent & bit) != 0) {
              ++opponentCount;
            }
          }
          if (opponentCount == 0) {
            score += weights[rootCount];
          } else if (rootCount == 0) {
            score -= weights[opponentCount];
          }
        }
      }
    }
    return score;
  }

  static int minimax(const ConnectFourGame& game, int depth, Disc rootPlayer,
                     int alpha, int beta, int ply) {
    const Disc winningDisc = game.winner();
    if (winningDisc != Disc::Empty) {
      return winningDisc == rootPlayer ? kWinScore - ply
                                       : -kWinScore + ply;
    }
    if (game.moveCount() == kCellCount) return 0;
    if (depth == 0) return evaluate(game, rootPlayer);

    const bool maximizing = game.currentPlayer_ == rootPlayer;
    int best = maximizing ? -kSearchLimit : kSearchLimit;
    for (int orderIndex = 0; orderIndex < kColumns; ++orderIndex) {
      const int column = kColumnOrder[orderIndex];
      if (!game.canDrop(column)) continue;
      ConnectFourGame candidate = game;
      candidate.drop(column);
      const int score =
          minimax(candidate, depth - 1, rootPlayer, alpha, beta, ply + 1);
      if (maximizing) {
        if (score > best) best = score;
        if (best > alpha) alpha = best;
      } else {
        if (score < best) best = score;
        if (best < beta) beta = best;
      }
      if (beta <= alpha) break;
    }
    return best;
  }
};
