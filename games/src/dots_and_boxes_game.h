#pragma once

#include <stdint.h>

class DotsAndBoxesGame {
 public:
  static constexpr int kBoxSize = 4;
  static constexpr int kBoxesPerSide = kBoxSize;
  static constexpr int kDotsPerSide = kBoxesPerSide + 1;
  static constexpr int kHorizontalRows = kDotsPerSide;
  static constexpr int kHorizontalColumns = kBoxesPerSide;
  static constexpr int kVerticalRows = kBoxesPerSide;
  static constexpr int kVerticalColumns = kDotsPerSide;
  static constexpr int kBoxCount = kBoxesPerSide * kBoxesPerSide;
  static constexpr int kHorizontalEdgeCount =
      kHorizontalRows * kHorizontalColumns;
  static constexpr int kVerticalEdgeCount = kVerticalRows * kVerticalColumns;
  static constexpr int kTotalEdgeCount =
      kHorizontalEdgeCount + kVerticalEdgeCount;

  enum class Player : uint8_t {
    None = 0,
    Player1 = 1,
    Player2 = 2,
  };

  struct Snapshot {
    uint32_t horizontalEdges;
    uint32_t verticalEdges;
    uint16_t player1Boxes;
    uint16_t player2Boxes;
    uint8_t currentPlayer;
  };

  void start() { reset(); }

  void reset() {
    horizontalEdges_ = 0;
    verticalEdges_ = 0;
    player1Boxes_ = 0;
    player2Boxes_ = 0;
    currentPlayer_ = Player::Player1;
  }

  bool placeHorizontal(int row, int column) {
    if (!validHorizontalEdge(row, column) || gameOver()) return false;
    const uint32_t bit = horizontalBit(row, column);
    if ((horizontalEdges_ & bit) != 0) return false;

    horizontalEdges_ |= bit;
    uint8_t completedBoxes = 0;
    completedBoxes += claimBoxIfCompleted(row - 1, column);
    completedBoxes += claimBoxIfCompleted(row, column);
    if (completedBoxes == 0) currentPlayer_ = otherPlayer(currentPlayer_);
    return true;
  }

  bool placeVertical(int row, int column) {
    if (!validVerticalEdge(row, column) || gameOver()) return false;
    const uint32_t bit = verticalBit(row, column);
    if ((verticalEdges_ & bit) != 0) return false;

    verticalEdges_ |= bit;
    uint8_t completedBoxes = 0;
    completedBoxes += claimBoxIfCompleted(row, column - 1);
    completedBoxes += claimBoxIfCompleted(row, column);
    if (completedBoxes == 0) currentPlayer_ = otherPlayer(currentPlayer_);
    return true;
  }

  bool isHorizontalPlaced(int row, int column) const {
    return validHorizontalEdge(row, column) &&
           (horizontalEdges_ & horizontalBit(row, column)) != 0;
  }

  bool horizontalEdge(int row, int column) const {
    return isHorizontalPlaced(row, column);
  }

  bool isVerticalPlaced(int row, int column) const {
    return validVerticalEdge(row, column) &&
           (verticalEdges_ & verticalBit(row, column)) != 0;
  }

  bool verticalEdge(int row, int column) const {
    return isVerticalPlaced(row, column);
  }

  Player ownerAt(int row, int column) const {
    if (!validBox(row, column)) return Player::None;
    const uint16_t bit = boxBit(row, column);
    if ((player1Boxes_ & bit) != 0) return Player::Player1;
    if ((player2Boxes_ & bit) != 0) return Player::Player2;
    return Player::None;
  }

  Player owner(int row, int column) const { return ownerAt(row, column); }

  Player currentPlayer() const { return currentPlayer_; }

  uint8_t score(Player player) const {
    return static_cast<uint8_t>(bitCount(player == Player::Player1
                                             ? player1Boxes_
                                             : player == Player::Player2
                                                   ? player2Boxes_
                                                   : 0));
  }

  uint8_t player1Score() const { return score(Player::Player1); }
  uint8_t player2Score() const { return score(Player::Player2); }

  uint8_t moveCount() const {
    return static_cast<uint8_t>(bitCount(horizontalEdges_) +
                                bitCount(verticalEdges_));
  }

  uint8_t moves() const { return moveCount(); }

  bool gameOver() const { return moveCount() == kTotalEdgeCount; }

  Player winner() const {
    if (!gameOver()) return Player::None;
    const uint8_t score1 = player1Score();
    const uint8_t score2 = player2Score();
    return score1 > score2
               ? Player::Player1
               : score2 > score1 ? Player::Player2 : Player::None;
  }

  bool tied() const {
    return gameOver() && player1Score() == player2Score();
  }

  Snapshot snapshot() const {
    return {
        horizontalEdges_,
        verticalEdges_,
        player1Boxes_,
        player2Boxes_,
        static_cast<uint8_t>(currentPlayer_),
    };
  }

  bool restore(const Snapshot& snapshot) {
    if ((snapshot.horizontalEdges & ~kHorizontalMask) != 0 ||
        (snapshot.verticalEdges & ~kVerticalMask) != 0 ||
        (snapshot.player1Boxes & snapshot.player2Boxes) != 0 ||
        !validPlayer(snapshot.currentPlayer)) {
      return false;
    }

    for (int row = 0; row < kBoxesPerSide; ++row) {
      for (int column = 0; column < kBoxesPerSide; ++column) {
        const bool complete = isBoxComplete(snapshot.horizontalEdges,
                                            snapshot.verticalEdges, row, column);
        const uint16_t bit = boxBit(row, column);
        const bool ownedByPlayer1 = (snapshot.player1Boxes & bit) != 0;
        const bool ownedByPlayer2 = (snapshot.player2Boxes & bit) != 0;
        const bool owned = ownedByPlayer1 || ownedByPlayer2;
        if (complete != owned) return false;
      }
    }

    horizontalEdges_ = snapshot.horizontalEdges;
    verticalEdges_ = snapshot.verticalEdges;
    player1Boxes_ = snapshot.player1Boxes;
    player2Boxes_ = snapshot.player2Boxes;
    currentPlayer_ = static_cast<Player>(snapshot.currentPlayer);
    return true;
  }

 private:
  static constexpr uint32_t kHorizontalMask =
      (1UL << kHorizontalEdgeCount) - 1UL;
  static constexpr uint32_t kVerticalMask = (1UL << kVerticalEdgeCount) - 1UL;

  uint32_t horizontalEdges_ = 0;
  uint32_t verticalEdges_ = 0;
  uint16_t player1Boxes_ = 0;
  uint16_t player2Boxes_ = 0;
  Player currentPlayer_ = Player::Player1;

  static bool validHorizontalEdge(int row, int column) {
    return row >= 0 && row < kHorizontalRows && column >= 0 &&
           column < kHorizontalColumns;
  }

  static bool validVerticalEdge(int row, int column) {
    return row >= 0 && row < kVerticalRows && column >= 0 &&
           column < kVerticalColumns;
  }

  static bool validBox(int row, int column) {
    return row >= 0 && row < kBoxesPerSide && column >= 0 &&
           column < kBoxesPerSide;
  }

  static bool validPlayer(uint8_t player) {
    return player == static_cast<uint8_t>(Player::Player1) ||
           player == static_cast<uint8_t>(Player::Player2);
  }

  static uint32_t horizontalBit(int row, int column) {
    return 1UL << (row * kHorizontalColumns + column);
  }

  static uint32_t verticalBit(int row, int column) {
    return 1UL << (row * kVerticalColumns + column);
  }

  static uint16_t boxBit(int row, int column) {
    return static_cast<uint16_t>(1U << (row * kBoxesPerSide + column));
  }

  static Player otherPlayer(Player player) {
    return player == Player::Player1 ? Player::Player2 : Player::Player1;
  }

  static int bitCount(uint32_t value) {
    int count = 0;
    while (value != 0) {
      value &= value - 1;
      ++count;
    }
    return count;
  }

  static bool isBoxComplete(uint32_t horizontalEdges, uint32_t verticalEdges,
                            int row, int column) {
    return validBox(row, column) &&
           (horizontalEdges & horizontalBit(row, column)) != 0 &&
           (horizontalEdges & horizontalBit(row + 1, column)) != 0 &&
           (verticalEdges & verticalBit(row, column)) != 0 &&
           (verticalEdges & verticalBit(row, column + 1)) != 0;
  }

  uint8_t claimBoxIfCompleted(int row, int column) {
    if (!validBox(row, column) || !isBoxComplete(horizontalEdges_, verticalEdges_,
                                                 row, column) ||
        ownerAt(row, column) != Player::None) {
      return 0;
    }

    const uint16_t bit = boxBit(row, column);
    if (currentPlayer_ == Player::Player1) {
      player1Boxes_ |= bit;
    } else {
      player2Boxes_ |= bit;
    }
    return 1;
  }
};
