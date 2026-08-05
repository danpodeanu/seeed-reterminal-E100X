#pragma once

#include <TFT_eSPI.h>

namespace smooth_fonts {

enum class Selection {
  Smooth,
  GfxFallback,
};

class Manager {
 public:
  explicit Manager(TFT_eSPI& display);

  Selection select(int size, const GFXfont* fallback, bool sdReady);
  void selectGfx(const GFXfont* font);
  void unload();

  bool smoothLoaded() const { return currentSize_ != 0; }
  int currentSize() const { return currentSize_; }

 private:
  bool sizeVerified(int size) const;
  void rememberVerifiedSize(int size);

  TFT_eSPI& display_;
  int currentSize_ = 0;
  int verifiedSizes_[4] = {0, 0, 0, 0};
};

}  // namespace smooth_fonts
