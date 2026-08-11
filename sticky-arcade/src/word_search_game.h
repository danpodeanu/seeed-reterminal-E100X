#pragma once

#include <stdint.h>

class WordSearchGame {
 public:
  static constexpr int kSize = 9;
  static constexpr int kWordCount = 6;

  struct Snapshot {
    uint32_t seed;
    uint32_t checksum;
    uint8_t foundMask;
    uint8_t version;
  };

  void start(uint32_t seed) {
    generate(seed == 0 ? kDefaultSeed : seed);
    foundMask_ = 0;
  }

  void reset() { foundMask_ = 0; }

  char at(int row, int column) const {
    return validCell(row, column) ? grid_[row * kSize + column] : '\0';
  }

  const char* word(int index) const {
    return index >= 0 && index < kWordCount
               ? kWordBank[wordIndices_[index]]
               : "";
  }

  bool found(int index) const {
    return index >= 0 && index < kWordCount &&
           (foundMask_ & (1U << index)) != 0;
  }

  uint8_t foundCount() const {
    uint8_t count = 0;
    uint8_t remaining = foundMask_;
    while (remaining != 0) {
      remaining = static_cast<uint8_t>(remaining & (remaining - 1U));
      ++count;
    }
    return count;
  }

  bool solved() const { return foundCount() == kWordCount; }

  bool submit(int startRow, int startColumn, int endRow, int endColumn) {
    if (!validCell(startRow, startColumn) ||
        !validCell(endRow, endColumn)) {
      return false;
    }

    const int rowDifference = endRow - startRow;
    const int columnDifference = endColumn - startColumn;
    const int rowDistance =
        rowDifference < 0 ? -rowDifference : rowDifference;
    const int columnDistance =
        columnDifference < 0 ? -columnDifference : columnDifference;
    if ((rowDistance == 0 && columnDistance == 0) ||
        (rowDistance != 0 && columnDistance != 0 &&
         rowDistance != columnDistance)) {
      return false;
    }

    const int rowStep =
        rowDifference == 0 ? 0 : rowDifference > 0 ? 1 : -1;
    const int columnStep =
        columnDifference == 0 ? 0 : columnDifference > 0 ? 1 : -1;
    const int length =
        (rowDistance > columnDistance ? rowDistance : columnDistance) + 1;

    for (int index = 0; index < kWordCount; ++index) {
      if (found(index)) continue;
      const char* candidate = word(index);
      if (wordLength(candidate) != length) continue;
      bool forward = true;
      bool backward = true;
      for (int offset = 0; offset < length; ++offset) {
        const char letter =
            at(startRow + rowStep * offset,
               startColumn + columnStep * offset);
        if (letter != candidate[offset]) forward = false;
        if (letter != candidate[length - offset - 1]) backward = false;
      }
      if (forward || backward) {
        foundMask_ =
            static_cast<uint8_t>(foundMask_ | (1U << index));
        return true;
      }
    }
    return false;
  }

  Snapshot snapshot() const {
    return {seed_, puzzleChecksum(), foundMask_, kSnapshotVersion};
  }

  bool restore(const Snapshot& snapshot) {
    if (snapshot.seed == 0 || snapshot.version != kSnapshotVersion ||
        (snapshot.foundMask & ~kFoundMask) != 0) {
      return false;
    }

    WordSearchGame candidate;
    candidate.generate(snapshot.seed);
    if (candidate.puzzleChecksum() != snapshot.checksum) return false;
    candidate.foundMask_ = snapshot.foundMask;
    *this = candidate;
    return true;
  }

 private:
  static constexpr int kCellCount = kSize * kSize;
  static constexpr int kDirectionCount = 8;
  static constexpr int kCandidateCount =
      kCellCount * kDirectionCount;
  static constexpr int kBankCount = 24;
  static constexpr uint8_t kFoundMask = (1U << kWordCount) - 1U;
  static constexpr uint8_t kSnapshotVersion = 1;
  static constexpr uint32_t kDefaultSeed = 0xA5C31927UL;

  inline static constexpr const char* kWordBank[kBankCount] = {
      "ARCADE",   "BONUS",  "COIN",    "COMBO",   "GAME",   "LEVEL",
      "PIXEL",    "SCORE",  "TOKEN",   "QUEST",   "LASER",  "PLAYER",
      "POWER",    "RETRO",  "STAGE",   "START",   "JOYSTICK",
      "CHAMPION", "VICTORY", "PUZZLE", "ROCKET",  "TARGET", "TURBO",
      "WIZARD",
  };
  inline static constexpr int8_t kDirections[kDirectionCount][2] = {
      {0, 1},  {0, -1}, {1, 0},  {-1, 0},
      {1, 1},  {-1, -1}, {1, -1}, {-1, 1},
  };
  inline static constexpr int kCandidateSteps[] = {
      1,  5,  7,  11, 13, 17, 19, 23,
      25, 29, 31, 35, 37, 41, 43, 47,
  };

  char grid_[kCellCount] = {};
  uint8_t wordIndices_[kWordCount] = {};
  uint8_t foundMask_ = 0;
  uint32_t seed_ = 0;

  static bool validCell(int row, int column) {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
  }

  static int wordLength(const char* value) {
    int length = 0;
    while (value[length] != '\0') ++length;
    return length;
  }

  static uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  void generate(uint32_t seed) {
    seed_ = seed;
    foundMask_ = 0;
    for (char& cell : grid_) cell = '\0';

    uint32_t randomState = seed;
    uint8_t bankOrder[kBankCount] = {};
    for (int index = 0; index < kBankCount; ++index) {
      bankOrder[index] = static_cast<uint8_t>(index);
    }
    for (int index = kBankCount - 1; index > 0; --index) {
      const int swapIndex =
          static_cast<int>(nextRandom(randomState) %
                           static_cast<uint32_t>(index + 1));
      const uint8_t temporary = bankOrder[index];
      bankOrder[index] = bankOrder[swapIndex];
      bankOrder[swapIndex] = temporary;
    }
    for (int index = 0; index < kWordCount; ++index) {
      wordIndices_[index] = bankOrder[index];
    }

    uint8_t placementOrder[kWordCount] = {};
    for (int index = 0; index < kWordCount; ++index) {
      placementOrder[index] = static_cast<uint8_t>(index);
    }
    for (int index = 1; index < kWordCount; ++index) {
      const uint8_t value = placementOrder[index];
      int destination = index;
      while (destination > 0 &&
             wordLength(word(value)) >
                 wordLength(word(placementOrder[destination - 1]))) {
        placementOrder[destination] = placementOrder[destination - 1];
        --destination;
      }
      placementOrder[destination] = value;
    }

    int candidateStarts[kWordCount] = {};
    int candidateSteps[kWordCount] = {};
    constexpr int stepCount =
        sizeof(kCandidateSteps) / sizeof(kCandidateSteps[0]);
    for (int index = 0; index < kWordCount; ++index) {
      candidateStarts[index] =
          static_cast<int>(nextRandom(randomState) % kCandidateCount);
      candidateSteps[index] =
          kCandidateSteps[nextRandom(randomState) % stepCount];
    }

    uint8_t useCounts[kCellCount] = {};
    int searchBudget = 12000;
    if (!placeWords(0, placementOrder, candidateStarts, candidateSteps,
                    useCounts, searchBudget)) {
      placeFallback(randomState, useCounts);
    }

    for (char& cell : grid_) {
      if (cell == '\0') {
        cell = static_cast<char>(
            'A' + nextRandom(randomState) % static_cast<uint32_t>(26));
      }
    }
  }

  bool placeWords(int depth, const uint8_t placementOrder[kWordCount],
                  const int candidateStarts[kWordCount],
                  const int candidateSteps[kWordCount],
                  uint8_t useCounts[kCellCount], int& searchBudget) {
    if (depth == kWordCount) return true;
    if (searchBudget <= 0) return false;

    const int wordIndex = placementOrder[depth];
    const char* value = word(wordIndex);
    const int length = wordLength(value);
    for (int attempt = 0; attempt < kCandidateCount; ++attempt) {
      if (--searchBudget < 0) return false;
      const int candidate =
          (candidateStarts[depth] + attempt * candidateSteps[depth]) %
          kCandidateCount;
      const int cell = candidate / kDirectionCount;
      const int direction = candidate % kDirectionCount;
      const int row = cell / kSize;
      const int column = cell % kSize;
      const int rowStep = kDirections[direction][0];
      const int columnStep = kDirections[direction][1];
      const int endRow = row + rowStep * (length - 1);
      const int endColumn = column + columnStep * (length - 1);
      if (!validCell(endRow, endColumn) ||
          !canPlace(value, length, row, column, rowStep, columnStep)) {
        continue;
      }

      setPlacement(value, length, row, column, rowStep, columnStep,
                   useCounts, true);
      if (placeWords(depth + 1, placementOrder, candidateStarts,
                     candidateSteps, useCounts, searchBudget)) {
        return true;
      }
      setPlacement(value, length, row, column, rowStep, columnStep,
                   useCounts, false);
    }
    return false;
  }

  bool canPlace(const char* value, int length, int row, int column,
                int rowStep, int columnStep) const {
    for (int offset = 0; offset < length; ++offset) {
      const char existing =
          grid_[(row + rowStep * offset) * kSize +
                column + columnStep * offset];
      if (existing != '\0' && existing != value[offset]) return false;
    }
    return true;
  }

  void setPlacement(const char* value, int length, int row, int column,
                    int rowStep, int columnStep,
                    uint8_t useCounts[kCellCount], bool place) {
    for (int offset = 0; offset < length; ++offset) {
      const int cell =
          (row + rowStep * offset) * kSize + column + columnStep * offset;
      if (place) {
        grid_[cell] = value[offset];
        ++useCounts[cell];
      } else {
        --useCounts[cell];
        if (useCounts[cell] == 0) grid_[cell] = '\0';
      }
    }
  }

  void placeFallback(uint32_t& randomState,
                     uint8_t useCounts[kCellCount]) {
    for (int index = 0; index < kCellCount; ++index) {
      grid_[index] = '\0';
      useCounts[index] = 0;
    }
    for (int index = 0; index < kWordCount; ++index) {
      const char* value = word(index);
      const int length = wordLength(value);
      const bool reverse = (nextRandom(randomState) & 1U) != 0;
      const int column = reverse ? kSize - 1 : 0;
      const int columnStep = reverse ? -1 : 1;
      setPlacement(value, length, index, column, 0, columnStep, useCounts,
                   true);
    }
  }

  uint32_t puzzleChecksum() const {
    uint32_t checksum = 2166136261UL;
    for (char cell : grid_) {
      checksum ^= static_cast<uint8_t>(cell);
      checksum *= 16777619UL;
    }
    for (uint8_t index : wordIndices_) {
      checksum ^= index;
      checksum *= 16777619UL;
    }
    checksum ^= seed_;
    checksum *= 16777619UL;
    return checksum;
  }
};
