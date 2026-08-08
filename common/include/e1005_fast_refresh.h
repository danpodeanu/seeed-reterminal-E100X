#pragma once

#include <stddef.h>
#include <stdint.h>

class EPaper;

// Stateful SSD1677 differential refresh for the portrait E1005. Call begin()
// after a full EPaper::update(), mutate the display framebuffer, then refresh()
// the logical rectangle that changed.
class E1005FastRefresh {
 public:
  struct Region {
    int x;
    int y;
    int width;
    int height;
  };

  struct Timing {
    uint32_t prepareUs = 0;
    uint32_t transferUs = 0;
    uint32_t panelUs = 0;
    uint32_t reseedUs = 0;
    uint32_t totalUs = 0;
  };

  enum class Result {
    Ok,
    NotReady,
    InvalidRegion,
    NoFramebuffer,
    AllocationFailed,
    BusyNotAsserted,
    TimedOut,
  };

  explicit E1005FastRefresh(EPaper& display);
  ~E1005FastRefresh();

  E1005FastRefresh(const E1005FastRefresh&) = delete;
  E1005FastRefresh& operator=(const E1005FastRefresh&) = delete;

  Result begin();
  Result refresh(const Region& region, Timing& timing);
  void end();

  bool ready() const { return ready_; }
  static const char* resultMessage(Result result);

 private:
  struct NativeRegion {
    int left;
    int top;
    int width;
    int height;
  };

  static constexpr size_t kFramebufferSize = 48000;

  EPaper& display_;
  uint8_t* previousPlane_ = nullptr;
  uint8_t* nextPlane_ = nullptr;
  bool ready_ = false;

  bool allocateBuffers();
  static NativeRegion nativeRegion(const Region& region);
  static void buildDisplayPlane(const uint8_t* framebuffer,
                                uint8_t* destination);
  void writePanelCommand(uint8_t command, const uint8_t* data = nullptr,
                         size_t length = 0);
  void writeDisplayPlaneWindow(uint8_t command, const uint8_t* data,
                               const NativeRegion& region);
  void setPartialWindow(const NativeRegion& region);
  void seedBaseline(const uint8_t* plane);
};
