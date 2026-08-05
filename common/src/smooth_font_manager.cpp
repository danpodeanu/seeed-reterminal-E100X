#include "smooth_font_manager.h"

#include <Arduino.h>
#include <SD.h>

#include "app_logger.h"
#include "sd_card.h"

namespace smooth_fonts {

Manager::Manager(TFT_eSPI& display) : display_(display) {}

bool Manager::sizeVerified(int size) const {
  for (int verified : verifiedSizes_) {
    if (verified == size) return true;
  }
  return false;
}

void Manager::rememberVerifiedSize(int size) {
  for (int& verified : verifiedSizes_) {
    if (verified == size) return;
    if (verified == 0) {
      verified = size;
      return;
    }
  }
}

void Manager::unload() {
  if (currentSize_ == 0) return;
  display_.unloadFont();
  currentSize_ = 0;
}

void Manager::selectGfx(const GFXfont* font) {
  unload();
  display_.setFreeFont(font);
}

Selection Manager::select(int size, const GFXfont* fallback, bool sdReady) {
  if (!sdReady) {
    selectGfx(fallback);
    return Selection::GfxFallback;
  }
  if (currentSize_ == size) return Selection::Smooth;

  unload();
  const String path = String("/fonts/sans_bold_") + size + ".vlw";
  if (!sizeVerified(size) && !sd_card::fileExists(path)) {
    LOG.printf("[font] %s probe failed; falling back to GFX font for this call\n",
               path.c_str());
    display_.setFreeFont(fallback);
    return Selection::GfxFallback;
  }

  display_.setFreeFont(nullptr);
  const uint32_t started = millis();
  display_.loadFont(String("fonts/sans_bold_") + size, SD);
  currentSize_ = size;
  rememberVerifiedSize(size);
  LOG.printf(
      "[font] loaded sans_bold_%d in %lu ms "
      "(yAdvance=%u ascent=%u descent=%u)\n",
      size, static_cast<unsigned long>(millis() - started),
      static_cast<unsigned>(display_.gFont.yAdvance),
      static_cast<unsigned>(display_.gFont.ascent),
      static_cast<unsigned>(display_.gFont.descent));
  return Selection::Smooth;
}

}  // namespace smooth_fonts
