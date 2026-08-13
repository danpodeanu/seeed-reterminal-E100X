#pragma once

#include <stddef.h>
#include <stdint.h>

#include <limits>

namespace sticky_fiddle {

class BubbleWrap {
 public:
  static constexpr int kColumns = 6;
  static constexpr int kRows = 8;
  static constexpr int kCount = kColumns * kRows;

  bool pop(int row, int column) {
    if (!valid(row, column)) return false;
    const uint64_t mask = uint64_t{1} << index(row, column);
    if ((popped_ & mask) != 0) return false;
    popped_ |= mask;
    return true;
  }

  bool popped(int row, int column) const {
    return valid(row, column) &&
           (popped_ & (uint64_t{1} << index(row, column))) != 0;
  }

  bool allPopped() const {
    return (popped_ & kValidMask) == kValidMask;
  }

  void reset() { popped_ = 0; }
  uint64_t snapshot() const { return popped_; }
  void restore(uint64_t snapshot) { popped_ = snapshot & kValidMask; }

 private:
  static constexpr uint64_t kValidMask =
      kCount == 64 ? ~uint64_t{0} : (uint64_t{1} << kCount) - 1;

  static constexpr bool valid(int row, int column) {
    return row >= 0 && row < kRows && column >= 0 && column < kColumns;
  }

  static constexpr int index(int row, int column) {
    return row * kColumns + column;
  }

  uint64_t popped_ = 0;
};

struct RakeSegment {
  int16_t x1 = 0;
  int16_t y1 = 0;
  int16_t x2 = 0;
  int16_t y2 = 0;
};

class ZenRake {
 public:
  static constexpr size_t kMaximumSegments = 96;

  bool add(int x1, int y1, int x2, int y2) {
    if (count_ >= kMaximumSegments || (x1 == x2 && y1 == y2)) return false;
    segments_[count_++] = {
        static_cast<int16_t>(x1), static_cast<int16_t>(y1),
        static_cast<int16_t>(x2), static_cast<int16_t>(y2)};
    return true;
  }

  size_t count() const { return count_; }
  const RakeSegment& segment(size_t index) const { return segments_[index]; }
  void reset() { count_ = 0; }

  void restore(const RakeSegment* segments, size_t count) {
    count_ = count > kMaximumSegments ? kMaximumSegments : count;
    for (size_t index = 0; index < count_; ++index) {
      segments_[index] = segments[index];
    }
  }

 private:
  RakeSegment segments_[kMaximumSegments] = {};
  size_t count_ = 0;
};

class FlipDots {
 public:
  static constexpr int kColumns = 8;
  static constexpr int kRows = 12;
  static constexpr int kCount = kColumns * kRows;
  static constexpr int kWordCount = (kCount + 31) / 32;

  bool toggle(int row, int column) {
    if (!valid(row, column)) return false;
    const int bit = index(row, column);
    words_[bit / 32] ^= uint32_t{1} << (bit % 32);
    return true;
  }

  bool on(int row, int column) const {
    if (!valid(row, column)) return false;
    const int bit = index(row, column);
    return (words_[bit / 32] & (uint32_t{1} << (bit % 32))) != 0;
  }

  void reset() {
    for (uint32_t& word : words_) word = 0;
  }

  const uint32_t* snapshot() const { return words_; }
  void restore(const uint32_t* words) {
    for (int index = 0; index < kWordCount; ++index) words_[index] = words[index];
    words_[kWordCount - 1] &= 0xFFFFFFFFUL >> (kWordCount * 32 - kCount);
  }

 private:
  static constexpr bool valid(int row, int column) {
    return row >= 0 && row < kRows && column >= 0 && column < kColumns;
  }

  static constexpr int index(int row, int column) {
    return row * kColumns + column;
  }

  uint32_t words_[kWordCount] = {};
};

struct Ripple {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t age = 0;
};

class RipplePond {
 public:
  static constexpr size_t kMaximumRipples = 8;
  static constexpr uint8_t kMaximumAge = 4;

  void add(int x, int y) {
    if (count_ == kMaximumRipples) {
      for (size_t index = 1; index < count_; ++index) {
        ripples_[index - 1] = ripples_[index];
      }
      --count_;
    }
    ripples_[count_++] = {
        static_cast<int16_t>(x), static_cast<int16_t>(y), 0};
  }

  bool advance() {
    if (count_ == 0) return false;
    size_t write = 0;
    for (size_t read = 0; read < count_; ++read) {
      Ripple ripple = ripples_[read];
      ++ripple.age;
      if (ripple.age < kMaximumAge) ripples_[write++] = ripple;
    }
    count_ = write;
    return true;
  }

  size_t count() const { return count_; }
  const Ripple& ripple(size_t index) const { return ripples_[index]; }
  void reset() { count_ = 0; }

  void restore(const Ripple* ripples, size_t count) {
    count_ = 0;
    const size_t capped = count > kMaximumRipples ? kMaximumRipples : count;
    for (size_t index = 0; index < capped; ++index) {
      if (ripples[index].age < kMaximumAge) ripples_[count_++] = ripples[index];
    }
  }

 private:
  Ripple ripples_[kMaximumRipples] = {};
  size_t count_ = 0;
};

class PointlessCounter {
 public:
  bool increment() {
    if (value_ == std::numeric_limits<uint32_t>::max()) return false;
    ++value_;
    return true;
  }

  uint32_t value() const { return value_; }
  void reset() { value_ = 0; }
  void restore(uint32_t value) { value_ = value; }

 private:
  uint32_t value_ = 0;
};

class Squish {
 public:
  static constexpr uint8_t kMaximumLevel = 5;

  bool inflate() {
    if (level_ >= kMaximumLevel) return false;
    ++level_;
    return true;
  }

  bool relax() {
    if (level_ == 0) return false;
    --level_;
    return true;
  }

  uint8_t level() const { return level_; }
  void reset() { level_ = 0; }
  void restore(uint8_t level) {
    level_ = level > kMaximumLevel ? kMaximumLevel : level;
  }

 private:
  uint8_t level_ = 0;
};

}  // namespace sticky_fiddle

