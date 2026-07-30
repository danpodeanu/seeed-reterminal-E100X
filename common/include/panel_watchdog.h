#pragma once

// Best-effort panel-refresh watchdog. The Spectra Gray16 driver used by
// E1003 occasionally locks up inside epaper.update() when the panel's
// BUSY line never returns to ready (a rare hardware glitch that has
// been observed on scheduled timer refreshes). The device is then stuck
// in a bare `while` loop inside the panel driver - buttons are dead
// and only a hard power cycle recovers.
//
// This helper arms the ESP-IDF task watchdog around a single panel
// refresh so that if the driver blocks longer than `timeoutSeconds`,
// the watchdog panics and reboots the device. On the next boot the
// viewer just picks up where it left off. On a normal refresh (Gray16
// takes ~15-20 s on E1003) the wrapper is essentially free.
//
// The guard is a no-op on other panels: E1001/E1002/E1004 haven't
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
template <typename Refresh>
inline void guard(Refresh&& refresh, uint32_t timeoutSeconds = 120) {
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
  if (subscribed) esp_task_wdt_delete(nullptr);
  // Deliberately leave TWDT running - reverting to the Arduino default
  // requires re-init and it's harmless to leave a 120 s guard armed
  // through sleep prep; the next wake reconfigures it anyway.
}

}  // namespace panel_watchdog

#else  // Non-E1003 panels: no-op wrapper.

namespace panel_watchdog {
template <typename Refresh>
inline void guard(Refresh&& refresh, uint32_t /*timeoutSeconds*/ = 120) {
  refresh();
}
}  // namespace panel_watchdog

#endif
