#pragma once

// Guard panel refreshes against controller stalls. Seeed_GFX's E1003 Gray16
// path starts the IT8951 waveform and immediately sends TCON_SLEEP without
// waiting for LUTAFSR to return idle. If sleep lands before the waveform
// completes, the panel can remain in a dark/inverted intermediate frame.
// The E1003 path below reproduces the driver's full-frame upload, requires the
// LUT engine to become active, then waits for completion before the first sleep
// command. Monochrome refreshes keep using update(), whose 1-bpp driver path
// already performs a completion wait.
//
// The guard is a no-op on other panels: E1001/E1002/E1004/E1005 haven't
// exhibited the freeze in field use, and the fast paths on those
// panels have tighter timings that would risk false-positive resets.

#if RETERMINAL_MODEL == 1003
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "app_logger.h"

namespace panel_watchdog {

template <typename Panel>
inline void refreshPanel(Panel& panel) {
  if (panel.getColorDepth() != 4) {
    panel.update();
    return;
  }

  const uint16_t width = static_cast<uint16_t>(panel.width());
  const uint16_t height = static_cast<uint16_t>(panel.height());
  const auto* framebuffer =
      static_cast<const uint8_t*>(panel.getPointer());

  panel.wake();
  panel.setTconWindowsData(0, 0, width - 1, height - 1);
  panel.tconLoadImage(framebuffer, 0, 0, width, height, false);
  constexpr uint16_t kLutStatusRegister = 0x1224;
  constexpr uint32_t kLutStartTimeoutMs = 250;
  const uint32_t waveformStartedAt = millis();
  panel.tconDisplayArea(0, 0, width, height, 0x02);

  uint16_t lutStatus = 0;
  do {
    delay(1);
    lutStatus = panel.tconReadReg(kLutStatusRegister);
  } while (lutStatus == 0 &&
           millis() - waveformStartedAt < kLutStartTimeoutMs);

  if (lutStatus == 0) {
    LOG.printf(
        "[panel] ERROR: E1003 GC16 waveform did not become active within "
        "%lu ms; leaving IT8951 awake\n",
        static_cast<unsigned long>(kLutStartTimeoutMs));
    return;
  }

  const uint32_t waveformActiveAt = millis();
  do {
    delay(1);
    lutStatus = panel.tconReadReg(kLutStatusRegister);
  } while (lutStatus != 0);
  LOG.printf(
      "[panel] E1003 GC16 waveform active after %lu ms, complete after "
      "%lu ms\n",
      static_cast<unsigned long>(waveformActiveAt - waveformStartedAt),
      static_cast<unsigned long>(millis() - waveformStartedAt));
  panel.sleep();
}

// Run the panel refresh under a task watchdog. If it takes longer than
// `timeoutSeconds`, the ESP-IDF watchdog panics the CPU and reboots.
// The Arduino-ESP32 core starts a TWDT by default (typically 5 s) that
// only monitors the idle tasks, so we reconfigure to our longer
// timeout, then subscribe the current task. Any esp_task_wdt_* call
// that returns something other than ESP_OK is treated as a soft error
// - we still run the refresh so a WDT hiccup can never brick a wake.
template <typename Panel>
inline void refresh(Panel& panel, uint32_t timeoutSeconds = 120) {
  const esp_task_wdt_config_t cfg = {
      /*timeout_ms=*/timeoutSeconds * 1000U,
      /*idle_core_mask=*/0,        // don't monitor idle tasks
      /*trigger_panic=*/true,
  };
  // reconfigure() if the Arduino core already initialised the WDT;
  // fall through to init() otherwise.
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) {
    esp_task_wdt_init(&cfg);
  }
  const bool subscribed = esp_task_wdt_add(nullptr) == ESP_OK;
  LOG.printf("[wdt] panel refresh guarded, panic reset in %us%s\n",
             (unsigned)timeoutSeconds,
             subscribed ? "" : " (subscribe failed)");
  refreshPanel(panel);
  // Always try to unsubscribe on exit - not just when our add()
  // returned ESP_OK. arduino-esp32's startup can auto-subscribe the
  // loopTask under some menuconfigs; in that case our add() returns
  // ESP_ERR_INVALID_STATE ("already subscribed") but the task is
  // still bound to the reconfigured 120 s timeout we just installed.
  // If we then skip the delete, any long-running code path after the
  // refresh - most notably the config portal, whose infinite HTTP
  // loop never returns to Arduino's loop() (where arduino-esp32
  // would feed the WDT) - panics the CPU ~120 s in. Deleting
  // unconditionally makes the guard scope tight: the WDT is only
  // watching us while refresh() runs; anything after that is on its
  // own timing. Ignore the return value - "not subscribed" is fine.
  esp_task_wdt_delete(nullptr);
  // Deliberately leave TWDT running - reverting to the Arduino default
  // requires re-init and it's harmless to leave a 120 s guard armed
  // through sleep prep; the next wake reconfigures it anyway.
}

// Explicitly drop the current task's TWDT subscription. Call before
// entering any long-running loop that doesn't return through Arduino's
// loop() (e.g. the config portal's `while (true) { config_portal::loop();
// ... }` in the viewers). Belt-and-braces defence against a future
// refresh path forgetting to unsubscribe; safe to call even if the
// task was never subscribed. No-op on non-E1003 targets.
inline void disarmCurrentTask() {
  esp_task_wdt_delete(nullptr);
}

}  // namespace panel_watchdog

#else  // Non-E1003 panels: no-op wrapper.

namespace panel_watchdog {
template <typename Panel>
inline void refresh(Panel& panel, uint32_t /*timeoutSeconds*/ = 120) {
  panel.update();
}
inline void disarmCurrentTask() {}
}  // namespace panel_watchdog

#endif
