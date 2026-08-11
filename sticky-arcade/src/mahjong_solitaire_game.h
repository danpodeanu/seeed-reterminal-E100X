#pragma once

#include <stdint.h>

class MahjongSolitaireGame {
 public:
  static constexpr int kTileCount = 144;
  static constexpr int kTileTypeCount = 36;
  static constexpr int kTilesPerType = 4;
  static constexpr int kPairCount = kTileCount / 2;
  static constexpr uint8_t kNoSelection = 0xFF;

  struct Position {
    uint8_t column;
    uint8_t row;
    uint8_t layer;
  };

  enum class TapResult : uint8_t {
    NoChange,
    Blocked,
    Selected,
    ClearedSelection,
    Reselected,
    Removed,
    Won,
  };

  struct Snapshot {
    uint32_t seed;
    uint64_t occupied[3];
    uint16_t moves;
    uint8_t selectedIndex;
  };

  MahjongSolitaireGame() { start(1); }

  void start(uint32_t seed) {
    seed_ = seed;
    occupied_[0] = UINT64_MAX;
    occupied_[1] = UINT64_MAX;
    occupied_[2] = 0xFFFFULL;
    moves_ = 0;
    selectedIndex_ = kNoSelection;
    generateTiles();
  }

  void reset() { start(seed_); }

  TapResult tap(int index) {
    if (!validIndex(index) || !occupied(index)) return TapResult::NoChange;
    if (!free(index)) return TapResult::Blocked;
    if (selectedIndex_ == kNoSelection) {
      selectedIndex_ = static_cast<uint8_t>(index);
      return TapResult::Selected;
    }
    if (selectedIndex_ == index) {
      selectedIndex_ = kNoSelection;
      return TapResult::ClearedSelection;
    }
    if (!free(selectedIndex_)) {
      selectedIndex_ = static_cast<uint8_t>(index);
      return TapResult::Reselected;
    }
    if (tileType_[selectedIndex_] != tileType_[index]) {
      selectedIndex_ = static_cast<uint8_t>(index);
      return TapResult::Reselected;
    }

    setOccupied(selectedIndex_, false);
    setOccupied(index, false);
    selectedIndex_ = kNoSelection;
    if (moves_ != UINT16_MAX) ++moves_;
    return solved() ? TapResult::Won : TapResult::Removed;
  }

  uint32_t seed() const { return seed_; }
  uint16_t moves() const { return moves_; }
  uint8_t selectedIndex() const { return selectedIndex_; }
  bool selected(int index) const {
    return selectedIndex_ != kNoSelection && selectedIndex_ == index;
  }
  bool occupied(int index) const {
    if (!validIndex(index)) return false;
    return (occupied_[index / 64] & (1ULL << (index % 64))) != 0;
  }

  bool free(int index) const {
    if (!occupied(index)) return false;
    const Position current = position(index);
    bool leftBlocked = false;
    bool rightBlocked = false;
    for (int other = 0; other < kTileCount; ++other) {
      if (other == index || !occupied(other)) continue;
      const Position candidate = position(other);
      if (candidate.layer > current.layer &&
          candidate.column == current.column &&
          candidate.row == current.row) {
        return false;
      }
      if (candidate.layer != current.layer ||
          candidate.row != current.row) {
        continue;
      }
      if (candidate.column + 1 == current.column) leftBlocked = true;
      if (candidate.column == current.column + 1) rightBlocked = true;
    }
    return !leftBlocked || !rightBlocked;
  }

  bool hasMoves() const {
    for (int first = 0; first < kTileCount; ++first) {
      if (!free(first)) continue;
      for (int second = first + 1; second < kTileCount; ++second) {
        if (free(second) && tileType_[first] == tileType_[second]) return true;
      }
    }
    return false;
  }

  bool solved() const {
    return occupied_[0] == 0 && occupied_[1] == 0 && occupied_[2] == 0;
  }

  int remaining() const {
    int count = 0;
    for (int index = 0; index < kTileCount; ++index) {
      if (occupied(index)) ++count;
    }
    return count;
  }

  uint8_t tileType(int index) const {
    return validIndex(index) ? tileType_[index] : 0;
  }
  uint8_t tileSuit(int index) const {
    return static_cast<uint8_t>(tileType(index) / 9);
  }
  uint8_t tileRank(int index) const {
    return static_cast<uint8_t>(tileType(index) % 9 + 1);
  }

  static Position position(int index) {
    if (index < 0 || index >= kTileCount) return {0, 0, 0};
    if (index < 96) {
      return {static_cast<uint8_t>(index % 12),
              static_cast<uint8_t>(index / 12), 0};
    }
    if (index < 128) {
      const int local = index - 96;
      return {static_cast<uint8_t>(2 + local % 8),
              static_cast<uint8_t>(2 + local / 8), 1};
    }
    if (index < 140) {
      const int local = index - 128;
      return {static_cast<uint8_t>(4 + local % 4),
              static_cast<uint8_t>(2 + local / 4), 2};
    }
    const int local = index - 140;
    return {static_cast<uint8_t>(5 + local % 2),
            static_cast<uint8_t>(3 + local / 2), 3};
  }

  Snapshot snapshot() const {
    return {seed_, {occupied_[0], occupied_[1], occupied_[2]}, moves_,
            selectedIndex_};
  }

  bool restore(const Snapshot& snapshot) {
    if ((snapshot.occupied[2] & ~0xFFFFULL) != 0) return false;
    MahjongSolitaireGame candidate;
    candidate.seed_ = snapshot.seed;
    candidate.occupied_[0] = snapshot.occupied[0];
    candidate.occupied_[1] = snapshot.occupied[1];
    candidate.occupied_[2] = snapshot.occupied[2];
    candidate.moves_ = snapshot.moves;
    candidate.selectedIndex_ = snapshot.selectedIndex;
    candidate.generateTiles();

    const int removed = kTileCount - candidate.remaining();
    if ((removed & 1) != 0 || candidate.moves_ != removed / 2) return false;
    uint8_t remainingByType[kTileTypeCount] = {};
    for (int index = 0; index < kTileCount; ++index) {
      if (candidate.occupied(index)) {
        ++remainingByType[candidate.tileType_[index]];
      }
    }
    for (int type = 0; type < kTileTypeCount; ++type) {
      if (remainingByType[type] > kTilesPerType ||
          ((kTilesPerType - remainingByType[type]) & 1U) != 0) {
        return false;
      }
    }
    if (candidate.selectedIndex_ != kNoSelection &&
        (!candidate.occupied(candidate.selectedIndex_) ||
         !candidate.free(candidate.selectedIndex_))) {
      return false;
    }
    *this = candidate;
    return true;
  }

 private:
  uint32_t seed_ = 1;
  uint64_t occupied_[3] = {UINT64_MAX, UINT64_MAX, 0xFFFFULL};
  uint16_t moves_ = 0;
  uint8_t selectedIndex_ = kNoSelection;
  uint8_t tileType_[kTileCount] = {};

  static bool validIndex(int index) {
    return index >= 0 && index < kTileCount;
  }

  void setOccupied(int index, bool value) {
    const uint64_t bit = 1ULL << (index % 64);
    if (value) {
      occupied_[index / 64] |= bit;
    } else {
      occupied_[index / 64] &= ~bit;
    }
  }

  static uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  static int layerStart(int layer) {
    return layer == 0 ? 0 : layer == 1 ? 96 : layer == 2 ? 128 : 140;
  }

  static int layerWidth(int layer) {
    return layer == 0 ? 12 : layer == 1 ? 8 : layer == 2 ? 4 : 2;
  }

  static int layerRows(int layer) {
    return layer == 0 ? 8 : layer == 1 ? 4 : layer == 2 ? 3 : 2;
  }

  static int layerIndex(int layer, int row, int column) {
    return layerStart(layer) + row * layerWidth(layer) + column;
  }

  void generateTiles() {
    uint8_t pairTypes[kPairCount] = {};
    int pairIndex = 0;
    for (int type = 0; type < kTileTypeCount; ++type) {
      pairTypes[pairIndex++] = static_cast<uint8_t>(type);
      pairTypes[pairIndex++] = static_cast<uint8_t>(type);
    }

    uint32_t randomState = seed_ == 0 ? 0xC8013EA4UL : seed_;
    for (int index = kPairCount - 1; index > 0; --index) {
      const int other =
          static_cast<int>(nextRandom(randomState) % (index + 1U));
      const uint8_t temporary = pairTypes[index];
      pairTypes[index] = pairTypes[other];
      pairTypes[other] = temporary;
    }

    pairIndex = 0;
    for (int layer = 3; layer >= 0; --layer) {
      const int width = layerWidth(layer);
      for (int row = 0; row < layerRows(layer); ++row) {
        for (int left = 0; left < width / 2; ++left) {
          const int right = width - left - 1;
          const uint8_t type = pairTypes[pairIndex++];
          tileType_[layerIndex(layer, row, left)] = type;
          tileType_[layerIndex(layer, row, right)] = type;
        }
      }
    }
  }
};
