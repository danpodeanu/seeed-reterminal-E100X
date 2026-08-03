#pragma once

#include <stdint.h>

// Pure geometry helpers for photo landscape rotation on E1004. The device
// has a portrait-native 1200 x 1600 panel; when the user installs it
// physically rotated 90 degrees they still want photos to come out upright,
// which we handle by rendering into a landscape (effW x effH) intermediate
// buffer and then rotating pixels into the native framebuffer.
//
// Coordinate systems:
//   * "eff"    = effective (photo) coordinates, x in [0, effW), y in [0, effH).
//                effW = PANEL_HEIGHT, effH = PANEL_WIDTH when landscape.
//   * "native" = native panel coordinates, x in [0, PANEL_WIDTH),
//                y in [0, PANEL_HEIGHT). This is what the driver blits.
//
// Design constraint: the geometry is invertible, so the browser uploader
// can bake the same rotation into its BMP raster. See
// common-sd-web/src/sd_web_portal_photo_upload_page.cpp encode4bitBmp().
//
// Kept independent of Arduino.h / config.h so the native test harness can
// exercise it directly. Values match ::config::Orientation.

namespace photo_geom {

enum Orientation : uint8_t {
  kNative     = 0,
  kRotateCW   = 1,
  kRotateCCW  = 2,
};

inline void effToNative(uint8_t orient,
                        int effW, int effH,
                        int eX, int eY,
                        int& nX, int& nY) {
  switch (orient) {
    case kRotateCW:
      // Photo rotated 90 CCW for a CW-installed panel.
      nX = eY;
      nY = effW - 1 - eX;
      return;
    case kRotateCCW:
      // Photo rotated 90 CW for a CCW-installed panel.
      nX = effH - 1 - eY;
      nY = eX;
      return;
    case kNative:
    default:
      nX = eX;
      nY = eY;
      return;
  }
}

}  // namespace photo_geom

