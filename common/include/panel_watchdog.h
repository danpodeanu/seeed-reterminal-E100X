#pragma once

// Guard E1003 panel refreshes against incomplete and stale optical state.
// The IT8951 documentation requires INIT after the controller has been fully
// powered down because its memory no longer represents the retained panel
// image. Upload once, run INIT to reset the optical state, then run GC16 from
// the same controller buffer. Wait until each waveform has demonstrably
// completed, or apply a conservative minimum awake interval when this IT8951
// firmware does not expose LUT status.
// Monochrome refreshes keep using update(), whose 1-bpp driver path already
// performs a completion wait.
//
// The guard is a no-op on other panels: E1001/E1002/E1004/E1005 haven't
// exhibited the freeze in field use, and the fast paths on those
// panels have tighter timings that would risk false-positive resets.

#if RETERMINAL_MODEL == 1003
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "app_logger.h"
#include "board_pins.h"
#include "charger.h"

namespace panel_watchdog {

constexpr uint32_t kRetainedTraceMagic = 0xE1003B48;

struct RetainedVoltageTrace {
  uint32_t magic;
  uint32_t beforeMv;
  uint32_t minimumMv;
  uint32_t afterMv;
  uint32_t durationMs;
  uint16_t waveformMode;
  bool statusObserved;
};

static RTC_DATA_ATTR RetainedVoltageTrace retainedVoltageTrace = {};

inline void reportRetainedTraceOnce() {
  static bool reported = false;
  if (reported) return;
  reported = true;
  if (retainedVoltageTrace.magic != kRetainedTraceMagic) return;

  const char* waveformName =
      retainedVoltageTrace.waveformMode == 0x00
          ? "INIT"
          : (retainedVoltageTrace.waveformMode == 0x02 ? "GC16" : "unknown");
  LOG.printf(
      "[panel] previous E1003 %s battery before=%lu.%03luV "
      "min=%lu.%03luV after=%lu.%03luV sag=%lumV duration=%lums "
      "status=%s\n",
      waveformName,
      static_cast<unsigned long>(retainedVoltageTrace.beforeMv / 1000),
      static_cast<unsigned long>(retainedVoltageTrace.beforeMv % 1000),
      static_cast<unsigned long>(retainedVoltageTrace.minimumMv / 1000),
      static_cast<unsigned long>(retainedVoltageTrace.minimumMv % 1000),
      static_cast<unsigned long>(retainedVoltageTrace.afterMv / 1000),
      static_cast<unsigned long>(retainedVoltageTrace.afterMv % 1000),
      static_cast<unsigned long>(
          retainedVoltageTrace.beforeMv > retainedVoltageTrace.minimumMv
              ? retainedVoltageTrace.beforeMv -
                    retainedVoltageTrace.minimumMv
              : 0),
      static_cast<unsigned long>(retainedVoltageTrace.durationMs),
      retainedVoltageTrace.statusObserved ? "observed" : "timed-fallback");
  retainedVoltageTrace.magic = 0;
}

inline bool sampleBatteryCellMv(uint32_t& cellMv,
                                uint32_t sampleCount = 4) {
  pinMode(board::PIN_BATTERY_ENABLE, OUTPUT);
  if (digitalRead(board::PIN_BATTERY_ENABLE) != HIGH) {
    digitalWrite(board::PIN_BATTERY_ENABLE, HIGH);
    delay(20);
  }
  analogReadResolution(12);
  analogSetPinAttenuation(board::PIN_BATTERY_ADC, ADC_11db);

  uint32_t totalMv = 0;
  for (uint32_t i = 0; i < sampleCount; ++i) {
    totalMv += analogReadMilliVolts(board::PIN_BATTERY_ADC);
  }
  cellMv = (totalMv / sampleCount) * 2;
  return cellMv >= 2500 && cellMv <= 5000;
}

struct VoltageTrace {
  static constexpr uint32_t kSampleIntervalMs = 50;
  static constexpr uint32_t kSamplesPerReading = 4;

  bool valid = false;
  uint32_t beforeMv = 0;
  uint32_t minimumMv = UINT32_MAX;
  uint32_t afterMv = 0;
  uint32_t nextSampleAt = 0;

  void begin() {
    retainedVoltageTrace.magic = 0;
    sample(true);
  }

  void sampleIfDue() {
    if (static_cast<int32_t>(millis() - nextSampleAt) >= 0) sample(false);
  }

  void finish(uint16_t waveformMode, const char* waveformName,
              bool statusObserved, uint32_t durationMs) {
    sample(true);
    if (!valid) {
      LOG.printf("[panel] E1003 %s battery trace unavailable\n",
                 waveformName);
      return;
    }
    retainedVoltageTrace = {
        kRetainedTraceMagic, beforeMv, minimumMv, afterMv, durationMs,
        waveformMode, statusObserved};
    LOG.printf(
        "[panel] E1003 %s battery before=%lu.%03luV min=%lu.%03luV "
        "after=%lu.%03luV sag=%lumV\n",
        waveformName,
        static_cast<unsigned long>(beforeMv / 1000),
        static_cast<unsigned long>(beforeMv % 1000),
        static_cast<unsigned long>(minimumMv / 1000),
        static_cast<unsigned long>(minimumMv % 1000),
        static_cast<unsigned long>(afterMv / 1000),
        static_cast<unsigned long>(afterMv % 1000),
        static_cast<unsigned long>(beforeMv > minimumMv
                                      ? beforeMv - minimumMv
                                      : 0));
  }

 private:
  void sample(bool force) {
    const uint32_t now = millis();
    if (!force && static_cast<int32_t>(now - nextSampleAt) < 0) return;

    uint32_t cellMv = 0;
    nextSampleAt = now + kSampleIntervalMs;
    if (!sampleBatteryCellMv(cellMv, kSamplesPerReading)) return;

    if (!valid) beforeMv = cellMv;
    valid = true;
    afterMv = cellMv;
    if (cellMv < minimumMv) minimumMv = cellMv;
  }
};

template <typename Panel>
inline bool runWaveform(Panel& panel, uint16_t width, uint16_t height,
                        uint16_t mode, const char* name) {
  constexpr uint16_t kLutStatusRegister = 0x1224;
  constexpr uint32_t kLutStartTimeoutMs = 250;
  constexpr uint32_t kLutStartPollIntervalMs = 1;
  constexpr uint32_t kLutActivePollIntervalMs = 10;
  constexpr uint32_t kUnobservableWaveformWaitMs = 5000;
  VoltageTrace voltage;
  voltage.begin();
  const uint32_t startedAt = millis();
  panel.tconDisplayArea(0, 0, width, height, mode);

  uint16_t lutStatus = 0;
  do {
    delay(kLutStartPollIntervalMs);
    lutStatus = panel.tconReadReg(kLutStatusRegister);
    voltage.sampleIfDue();
  } while (lutStatus == 0 && millis() - startedAt < kLutStartTimeoutMs);

  if (lutStatus == 0) {
    LOG.printf(
        "[panel] WARNING: E1003 %s status not observable within %lu ms; "
        "holding controller awake for at least %lu ms\n",
        name, static_cast<unsigned long>(kLutStartTimeoutMs),
        static_cast<unsigned long>(kUnobservableWaveformWaitMs));
    while (lutStatus == 0 &&
           millis() - startedAt < kUnobservableWaveformWaitMs) {
      delay(kLutActivePollIntervalMs);
      lutStatus = panel.tconReadReg(kLutStatusRegister);
      voltage.sampleIfDue();
    }
    if (lutStatus == 0) {
      const uint32_t durationMs = millis() - startedAt;
      voltage.finish(mode, name, false, durationMs);
      LOG.printf(
          "[panel] E1003 %s conservative completion wait finished after "
          "%lu ms\n",
          name, static_cast<unsigned long>(durationMs));
      return true;
    }
  }

  const uint32_t activeAt = millis();
  do {
    delay(kLutActivePollIntervalMs);
    lutStatus = panel.tconReadReg(kLutStatusRegister);
    voltage.sampleIfDue();
  } while (lutStatus != 0);
  const uint32_t durationMs = millis() - startedAt;
  voltage.finish(mode, name, true, durationMs);
  LOG.printf(
      "[panel] E1003 %s waveform active after %lu ms, complete after "
      "%lu ms\n",
      name, static_cast<unsigned long>(activeAt - startedAt),
      static_cast<unsigned long>(durationMs));
  return true;
}

template <typename Panel>
inline void refreshPanel(Panel& panel) {
  reportRetainedTraceOnce();
  if (panel.getColorDepth() != 4) {
    panel.update();
    return;
  }

  const uint16_t width = static_cast<uint16_t>(panel.width());
  const uint16_t height = static_cast<uint16_t>(panel.height());
  const auto* framebuffer =
      static_cast<const uint8_t*>(panel.getPointer());

  uint32_t batteryMv = 0;
  const bool batteryValid = sampleBatteryCellMv(batteryMv, 8);
  const charger::Status power = charger::readSy6974b();
  if (batteryValid) {
    LOG.printf("[panel] E1003 refresh preflight=%lu.%03luV power=%s\n",
               static_cast<unsigned long>(batteryMv / 1000),
               static_cast<unsigned long>(batteryMv % 1000),
               power.valid
                   ? (power.state == charger::State::Connected ? "external"
                                                                : "battery")
                   : "unknown");
  }

  panel.wake();
  LOG.println(
      "[panel] E1003 uploading image once for INIT + GC16 refresh");
  panel.setTconWindowsData(0, 0, width - 1, height - 1);
  panel.tconLoadImage(framebuffer, 0, 0, width, height, false);
  if (!runWaveform(panel, width, height, 0x00, "INIT")) return;
  if (!runWaveform(panel, width, height, 0x02, "GC16")) return;
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
inline void refresh(Panel& panel, uint32_t timeoutSeconds = 20) {
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
  // still bound to the reconfigured timeout we just installed.
  // If we then skip the delete, any long-running code path after the
  // refresh - most notably the config portal, whose infinite HTTP
  // loop never returns to Arduino's loop() (where arduino-esp32
  // would feed the WDT) - eventually panics the CPU. Deleting
  // unconditionally makes the guard scope tight: the WDT is only
  // watching us while refresh() runs; anything after that is on its
  // own timing. Ignore the return value - "not subscribed" is fine.
  esp_task_wdt_delete(nullptr);
  // Deliberately leave TWDT running - reverting to the Arduino default
  // requires re-init and it's harmless to leave the longer guard armed
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
