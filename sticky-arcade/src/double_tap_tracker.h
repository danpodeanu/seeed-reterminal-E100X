#pragma once

#include <stdint.h>

class DoubleTapTracker {
 public:
  bool registerTap(uint16_t target, uint32_t atMs, uint32_t windowMs) {
    const bool matched =
        active_ && target_ == target && atMs - tappedAtMs_ <= windowMs;
    if (matched) {
      clear();
      return true;
    }
    target_ = target;
    tappedAtMs_ = atMs;
    active_ = true;
    return false;
  }

  void clear() {
    target_ = 0;
    tappedAtMs_ = 0;
    active_ = false;
  }

 private:
  uint16_t target_ = 0;
  uint32_t tappedAtMs_ = 0;
  bool active_ = false;
};
