#pragma once

// Best-effort panel-refresh watchdog. The Spectra Gray16 driver used by
// E1003 occasionally locks up inside epaper.update() when the panel's
// BUSY line never returns to ready (a rare hardware glitch that has
// been observed on scheduled timer refreshes). The device is then stuck
// in a bare `while` loop inside the panel driver - buttons are dead
// and only a hard power cycle recovers.
//
// Seeed_GFX also sends TCON_SLEEP immediately after starting a Gray16 update.
// Usually HRDY remains low until the update completes, but it can briefly be
// high before the LUT engine asserts busy. If sleep lands in that window, the
// panel can remain stuck in an inverted intermediate frame. This helper wakes
// the TCON after update(), waits for LUTAFSR to become idle, then sleeps it
// again under the same watchdog.
//
// The guard is a no-op on other panels: E1001/E1002/E1004/E1005 haven't
// exhibited the freeze in field use, and the fast paths on those
// panels have tighter timings that would risk false-positive resets.

#if RETERMINAL_MODEL == 1003
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "app_logger.h"

namespace panel_watchdog {

// Run `refresh` under a task watchdog. If refresh() takes longer than
// `timeoutSeconds`, the ESP-IDF watchdog panics the CPU and reboots.
// The Arduino-ESP32 core starts a TWDT by default (typically 5 s) that
// only monitors the idle tasks, so we reconfigure to our longer
// timeout, then subscribe the current task. Any esp_task_wdt_* call
// that returns something other than ESP_OK is treated as a soft error
// - we still run the refresh so a WDT hiccup can never brick a wake.
template <typename Panel, typename Refresh>
inline void guard(Panel& panel, Refresh&& refresh,
                  uint32_t timeoutSeconds = 120) {
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
  refresh();
  panel.wake();
  panel.tconWaitForDisplayReady();
  panel.sleep();
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
template <typename Panel, typename Refresh>
inline void guard(Panel& /*panel*/, Refresh&& refresh,
                  uint32_t /*timeoutSeconds*/ = 120) {
  refresh();
}
inline void disarmCurrentTask() {}
}  // namespace panel_watchdog

#endif
