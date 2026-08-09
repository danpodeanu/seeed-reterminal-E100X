#pragma once

#include <stdint.h>

class KlondikeGame {
 public:
  static constexpr int kSuitCount = 4;
  static constexpr int kRankCount = 13;
  static constexpr int kCardCount = kSuitCount * kRankCount;
  static constexpr int kTableauCount = 7;
  static constexpr uint8_t kNoCard = 0xFF;

  enum class Source : uint8_t {
    None,
    Waste,
    Foundation,
    Tableau,
  };

  enum class StockResult : uint8_t {
    NoChange,
    Drawn,
    Recycled,
  };

  struct Snapshot {
    uint32_t seed;
    uint16_t moves;
    uint8_t stock[kCardCount];
    uint8_t waste[kCardCount];
    uint8_t tableau[kTableauCount][kCardCount];
    uint8_t stockCount;
    uint8_t wasteCount;
    uint8_t tableauCount[kTableauCount];
    uint8_t faceUpStart[kTableauCount];
    uint8_t foundationCount[kSuitCount];
    uint8_t selectionSource;
    uint8_t selectionPile;
    uint8_t selectionCardIndex;
  };

  KlondikeGame() { start(1); }

  void start(uint32_t seed) {
    seed_ = seed;
    moves_ = 0;
    stockCount_ = 0;
    wasteCount_ = 0;
    for (int suit = 0; suit < kSuitCount; ++suit) {
      foundationCount_[suit] = 0;
    }
    for (int column = 0; column < kTableauCount; ++column) {
      tableauCount_[column] = 0;
      faceUpStart_[column] = 0;
      for (int index = 0; index < kCardCount; ++index) {
        tableau_[column][index] = 0;
      }
    }
    for (int index = 0; index < kCardCount; ++index) {
      stock_[index] = 0;
      waste_[index] = 0;
    }
    clearSelection();

    uint8_t deck[kCardCount] = {};
    for (int index = 0; index < kCardCount; ++index) {
      deck[index] = static_cast<uint8_t>(index);
    }
    uint32_t randomState = seed == 0 ? 0xA341316CUL : seed;
    for (int index = kCardCount - 1; index > 0; --index) {
      const int other =
          static_cast<int>(nextRandom(randomState) % (index + 1U));
      const uint8_t temporary = deck[index];
      deck[index] = deck[other];
      deck[other] = temporary;
    }

    int nextCard = 0;
    for (int column = 0; column < kTableauCount; ++column) {
      for (int row = 0; row <= column; ++row) {
        tableau_[column][row] = deck[nextCard++];
      }
      tableauCount_[column] = static_cast<uint8_t>(column + 1);
      faceUpStart_[column] = static_cast<uint8_t>(column);
    }
    while (nextCard < kCardCount) {
      stock_[stockCount_++] = deck[nextCard++];
    }
  }

  void reset() { start(seed_); }

  StockResult drawStock() {
    if (solved()) return StockResult::NoChange;
    clearSelection();
    if (stockCount_ > 0) {
      waste_[wasteCount_++] = stock_[--stockCount_];
      incrementMoves();
      return StockResult::Drawn;
    }
    if (wasteCount_ == 0) return StockResult::NoChange;
    while (wasteCount_ > 0) {
      stock_[stockCount_++] = waste_[--wasteCount_];
    }
    incrementMoves();
    return StockResult::Recycled;
  }

  bool selectWaste() {
    if (wasteCount_ == 0 || solved()) return false;
    if (selectionSource_ == Source::Waste) {
      clearSelection();
      return true;
    }
    selectionSource_ = Source::Waste;
    selectionPile_ = 0;
    selectionCardIndex_ = static_cast<uint8_t>(wasteCount_ - 1);
    return true;
  }

  bool selectFoundation(int suit) {
    if (suit < 0 || suit >= kSuitCount || foundationCount_[suit] == 0 ||
        solved()) {
      return false;
    }
    if (selectionSource_ == Source::Foundation &&
        selectionPile_ == static_cast<uint8_t>(suit)) {
      clearSelection();
      return true;
    }
    selectionSource_ = Source::Foundation;
    selectionPile_ = static_cast<uint8_t>(suit);
    selectionCardIndex_ =
        static_cast<uint8_t>(foundationCount_[suit] - 1);
    return true;
  }

  bool selectTableau(int column, int cardIndex) {
    if (!validTableauCard(column, cardIndex) ||
        cardIndex < faceUpStart_[column] || solved()) {
      return false;
    }
    if (selectionSource_ == Source::Tableau &&
        selectionPile_ == static_cast<uint8_t>(column) &&
        selectionCardIndex_ == static_cast<uint8_t>(cardIndex)) {
      clearSelection();
      return true;
    }
    selectionSource_ = Source::Tableau;
    selectionPile_ = static_cast<uint8_t>(column);
    selectionCardIndex_ = static_cast<uint8_t>(cardIndex);
    return true;
  }

  bool moveSelectionToTableau(int destinationColumn) {
    if (!hasSelection() || destinationColumn < 0 ||
        destinationColumn >= kTableauCount ||
        (selectionSource_ == Source::Tableau &&
         selectionPile_ == static_cast<uint8_t>(destinationColumn))) {
      return false;
    }

    const uint8_t firstCard = selectedCard();
    if (firstCard == kNoCard) return false;
    const uint8_t destinationCount = tableauCount_[destinationColumn];
    if (destinationCount == 0) {
      if (rank(firstCard) != kRankCount - 1) return false;
    } else if (!canStack(tableau_[destinationColumn][destinationCount - 1],
                         firstCard)) {
      return false;
    }

    uint8_t moving[kCardCount] = {};
    const int movingCount = copySelectedCards(moving);
    if (movingCount <= 0) return false;
    removeSelectedCards();
    for (int index = 0; index < movingCount; ++index) {
      tableau_[destinationColumn][tableauCount_[destinationColumn]++] =
          moving[index];
    }
    if (destinationCount == 0) faceUpStart_[destinationColumn] = 0;
    clearSelection();
    incrementMoves();
    return true;
  }

  bool moveSelectionToFoundation(int suit) {
    if (!hasSelection() || suit < 0 || suit >= kSuitCount ||
        selectedCardCount() != 1 ||
        (selectionSource_ == Source::Foundation &&
         selectionPile_ == static_cast<uint8_t>(suit))) {
      return false;
    }
    const uint8_t card = selectedCard();
    if (card == kNoCard || cardSuit(card) != suit ||
        rank(card) != foundationCount_[suit]) {
      return false;
    }
    removeSelectedCards();
    ++foundationCount_[suit];
    clearSelection();
    incrementMoves();
    return true;
  }

  bool moveSelectionToMatchingFoundation() {
    const uint8_t card = selectedCard();
    return card != kNoCard && moveSelectionToFoundation(cardSuit(card));
  }

  uint32_t seed() const { return seed_; }
  uint16_t moves() const { return moves_; }
  uint8_t stockCount() const { return stockCount_; }
  uint8_t wasteCount() const { return wasteCount_; }
  uint8_t tableauCount(int column) const {
    return validTableauColumn(column) ? tableauCount_[column] : 0;
  }
  uint8_t faceUpStart(int column) const {
    return validTableauColumn(column) ? faceUpStart_[column] : 0;
  }
  uint8_t foundationCount(int suit) const {
    return suit >= 0 && suit < kSuitCount ? foundationCount_[suit] : 0;
  }
  uint8_t wasteTop() const {
    return wasteCount_ > 0 ? waste_[wasteCount_ - 1] : kNoCard;
  }
  uint8_t foundationTop(int suit) const {
    if (suit < 0 || suit >= kSuitCount || foundationCount_[suit] == 0) {
      return kNoCard;
    }
    return static_cast<uint8_t>(suit * kRankCount +
                                foundationCount_[suit] - 1);
  }
  uint8_t tableauCard(int column, int cardIndex) const {
    return validTableauCard(column, cardIndex)
               ? tableau_[column][cardIndex]
               : kNoCard;
  }
  bool tableauCardFaceUp(int column, int cardIndex) const {
    return validTableauCard(column, cardIndex) &&
           cardIndex >= faceUpStart_[column];
  }

  Source selectionSource() const { return selectionSource_; }
  int selectionPile() const {
    return hasSelection() ? static_cast<int>(selectionPile_) : -1;
  }
  int selectionCardIndex() const {
    return hasSelection() ? static_cast<int>(selectionCardIndex_) : -1;
  }
  bool hasSelection() const { return selectionSource_ != Source::None; }
  bool isTableauCardSelected(int column, int cardIndex) const {
    return selectionSource_ == Source::Tableau &&
           selectionPile_ == static_cast<uint8_t>(column) &&
           cardIndex >= selectionCardIndex_ &&
           cardIndex < tableauCount_[column];
  }
  bool wasteSelected() const { return selectionSource_ == Source::Waste; }
  bool foundationSelected(int suit) const {
    return selectionSource_ == Source::Foundation &&
           selectionPile_ == static_cast<uint8_t>(suit);
  }
  void clearSelection() {
    selectionSource_ = Source::None;
    selectionPile_ = 0;
    selectionCardIndex_ = 0;
  }

  bool solved() const {
    for (int suit = 0; suit < kSuitCount; ++suit) {
      if (foundationCount_[suit] != kRankCount) return false;
    }
    return true;
  }

  static int rank(uint8_t card) {
    return card < kCardCount ? card % kRankCount : -1;
  }
  static int cardSuit(uint8_t card) {
    return card < kCardCount ? card / kRankCount : -1;
  }
  static bool isRed(uint8_t card) {
    const int suit = cardSuit(card);
    return suit == 1 || suit == 2;
  }
  static bool canStack(uint8_t destinationCard, uint8_t movingCard) {
    return destinationCard < kCardCount && movingCard < kCardCount &&
           rank(destinationCard) == rank(movingCard) + 1 &&
           isRed(destinationCard) != isRed(movingCard);
  }

  Snapshot snapshot() const {
    Snapshot result = {};
    result.seed = seed_;
    result.moves = moves_;
    result.stockCount = stockCount_;
    result.wasteCount = wasteCount_;
    result.selectionSource = static_cast<uint8_t>(selectionSource_);
    result.selectionPile = selectionPile_;
    result.selectionCardIndex = selectionCardIndex_;
    for (int index = 0; index < kCardCount; ++index) {
      result.stock[index] = stock_[index];
      result.waste[index] = waste_[index];
    }
    for (int column = 0; column < kTableauCount; ++column) {
      result.tableauCount[column] = tableauCount_[column];
      result.faceUpStart[column] = faceUpStart_[column];
      for (int index = 0; index < kCardCount; ++index) {
        result.tableau[column][index] = tableau_[column][index];
      }
    }
    for (int suit = 0; suit < kSuitCount; ++suit) {
      result.foundationCount[suit] = foundationCount_[suit];
    }
    return result;
  }

  bool restore(const Snapshot& snapshot) {
    if (!validSnapshot(snapshot)) return false;
    seed_ = snapshot.seed;
    moves_ = snapshot.moves;
    stockCount_ = snapshot.stockCount;
    wasteCount_ = snapshot.wasteCount;
    selectionSource_ = static_cast<Source>(snapshot.selectionSource);
    selectionPile_ = snapshot.selectionPile;
    selectionCardIndex_ = snapshot.selectionCardIndex;
    for (int index = 0; index < kCardCount; ++index) {
      stock_[index] = snapshot.stock[index];
      waste_[index] = snapshot.waste[index];
    }
    for (int column = 0; column < kTableauCount; ++column) {
      tableauCount_[column] = snapshot.tableauCount[column];
      faceUpStart_[column] = snapshot.faceUpStart[column];
      for (int index = 0; index < kCardCount; ++index) {
        tableau_[column][index] = snapshot.tableau[column][index];
      }
    }
    for (int suit = 0; suit < kSuitCount; ++suit) {
      foundationCount_[suit] = snapshot.foundationCount[suit];
    }
    return true;
  }

 private:
  uint32_t seed_ = 1;
  uint16_t moves_ = 0;
  uint8_t stock_[kCardCount] = {};
  uint8_t waste_[kCardCount] = {};
  uint8_t tableau_[kTableauCount][kCardCount] = {};
  uint8_t stockCount_ = 0;
  uint8_t wasteCount_ = 0;
  uint8_t tableauCount_[kTableauCount] = {};
  uint8_t faceUpStart_[kTableauCount] = {};
  uint8_t foundationCount_[kSuitCount] = {};
  Source selectionSource_ = Source::None;
  uint8_t selectionPile_ = 0;
  uint8_t selectionCardIndex_ = 0;

  static uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  static bool validTableauColumn(int column) {
    return column >= 0 && column < kTableauCount;
  }

  bool validTableauCard(int column, int cardIndex) const {
    return validTableauColumn(column) && cardIndex >= 0 &&
           cardIndex < tableauCount_[column];
  }

  int selectedCardCount() const {
    if (!hasSelection()) return 0;
    if (selectionSource_ == Source::Tableau) {
      return tableauCount_[selectionPile_] - selectionCardIndex_;
    }
    return 1;
  }

  uint8_t selectedCard() const {
    switch (selectionSource_) {
      case Source::Waste:
        return wasteCount_ > 0 ? waste_[wasteCount_ - 1] : kNoCard;
      case Source::Foundation:
        return foundationTop(selectionPile_);
      case Source::Tableau:
        return validTableauCard(selectionPile_, selectionCardIndex_)
                   ? tableau_[selectionPile_][selectionCardIndex_]
                   : kNoCard;
      case Source::None:
        break;
    }
    return kNoCard;
  }

  int copySelectedCards(uint8_t* destination) const {
    const int count = selectedCardCount();
    if (count == 0) return 0;
    if (selectionSource_ == Source::Tableau) {
      for (int index = 0; index < count; ++index) {
        destination[index] =
            tableau_[selectionPile_][selectionCardIndex_ + index];
      }
    } else {
      destination[0] = selectedCard();
    }
    return count;
  }

  void removeSelectedCards() {
    if (selectionSource_ == Source::Waste) {
      if (wasteCount_ > 0) --wasteCount_;
      return;
    }
    if (selectionSource_ == Source::Foundation) {
      if (selectionPile_ < kSuitCount &&
          foundationCount_[selectionPile_] > 0) {
        --foundationCount_[selectionPile_];
      }
      return;
    }
    if (selectionSource_ != Source::Tableau ||
        selectionPile_ >= kTableauCount) {
      return;
    }
    tableauCount_[selectionPile_] = selectionCardIndex_;
    if (tableauCount_[selectionPile_] == 0) {
      faceUpStart_[selectionPile_] = 0;
    } else if (faceUpStart_[selectionPile_] >=
               tableauCount_[selectionPile_]) {
      faceUpStart_[selectionPile_] =
          static_cast<uint8_t>(tableauCount_[selectionPile_] - 1);
    }
  }

  void incrementMoves() {
    if (moves_ != UINT16_MAX) ++moves_;
  }

  static bool addCardToSet(uint8_t card, bool (&seen)[kCardCount],
                           int& total) {
    if (card >= kCardCount || seen[card]) return false;
    seen[card] = true;
    ++total;
    return true;
  }

  static bool validSnapshot(const Snapshot& snapshot) {
    if (snapshot.stockCount > kCardCount ||
        snapshot.wasteCount > kCardCount ||
        snapshot.selectionSource >
            static_cast<uint8_t>(Source::Tableau)) {
      return false;
    }

    bool seen[kCardCount] = {};
    int total = 0;
    for (int suit = 0; suit < kSuitCount; ++suit) {
      if (snapshot.foundationCount[suit] > kRankCount) return false;
      for (int cardRank = 0; cardRank < snapshot.foundationCount[suit];
           ++cardRank) {
        if (!addCardToSet(
                static_cast<uint8_t>(suit * kRankCount + cardRank), seen,
                total)) {
          return false;
        }
      }
    }
    for (int index = 0; index < snapshot.stockCount; ++index) {
      if (!addCardToSet(snapshot.stock[index], seen, total)) return false;
    }
    for (int index = 0; index < snapshot.wasteCount; ++index) {
      if (!addCardToSet(snapshot.waste[index], seen, total)) return false;
    }
    for (int column = 0; column < kTableauCount; ++column) {
      const int count = snapshot.tableauCount[column];
      const int faceUpStart = snapshot.faceUpStart[column];
      if (count > kCardCount ||
          (count == 0 ? faceUpStart != 0 : faceUpStart >= count)) {
        return false;
      }
      for (int index = 0; index < count; ++index) {
        if (!addCardToSet(snapshot.tableau[column][index], seen, total)) {
          return false;
        }
      }
      for (int index = faceUpStart + 1; index < count; ++index) {
        if (!canStack(snapshot.tableau[column][index - 1],
                      snapshot.tableau[column][index])) {
          return false;
        }
      }
    }
    if (total != kCardCount) return false;

    const Source source = static_cast<Source>(snapshot.selectionSource);
    if (source == Source::None) return true;
    if (source == Source::Waste) return snapshot.wasteCount > 0;
    if (source == Source::Foundation) {
      return snapshot.selectionPile < kSuitCount &&
             snapshot.foundationCount[snapshot.selectionPile] > 0;
    }
    return snapshot.selectionPile < kTableauCount &&
           snapshot.selectionCardIndex >=
               snapshot.faceUpStart[snapshot.selectionPile] &&
           snapshot.selectionCardIndex <
               snapshot.tableauCount[snapshot.selectionPile];
  }
};
