#pragma once

// Guard E1003 panel refreshes against incomplete frames. The controller profile
// and refresh order mirror the FastEPD revision used by Seeed's production
// E1003 firmware: fixed 14C waveform selection, one 4bpp upload, one GC16,
// LUT-idle completion, then standby. Earlier INIT and white-GC16 preclean
// experiments both produced inverted or corrupted battery-powered frames.
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
#include "panel_traits.h"

namespace panel_watchdog {

constexpr uint32_t kRetainedTraceMagic = 0xE1003B51;

struct RetainedVoltageTrace {
  uint32_t magic;
  uint32_t beforeMv;
  uint32_t minimumMv;
  uint32_t afterMv;
  uint32_t durationMs;
  uint16_t vcomMv;
  uint16_t waveformMode;
  uint16_t waveformTemperatureC;
  uint16_t vcomSelector;
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
      "endpoint-min=%lu.%03luV after=%lu.%03luV sag=%lumV "
      "duration=%lums vcom=-%u.%03uV selector=0x%04X temp=%uC "
      "profile=FastEPD\n",
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
      static_cast<unsigned>(retainedVoltageTrace.vcomMv / 1000),
      static_cast<unsigned>(retainedVoltageTrace.vcomMv % 1000),
      static_cast<unsigned>(retainedVoltageTrace.vcomSelector),
      static_cast<unsigned>(retainedVoltageTrace.waveformTemperatureC));
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
  static constexpr uint32_t kSamplesPerReading = 4;

  bool valid = false;
  uint32_t beforeMv = 0;
  uint32_t minimumMv = UINT32_MAX;
  uint32_t afterMv = 0;

  void begin() {
    retainedVoltageTrace.magic = 0;
    sample();
  }

  void finish(uint16_t vcomMv, uint16_t waveformMode,
              const char* waveformName, uint32_t durationMs) {
    sample();
    if (!valid) {
      LOG.printf("[panel] E1003 %s battery trace unavailable\n",
                 waveformName);
      return;
    }
    retainedVoltageTrace = {
        kRetainedTraceMagic, beforeMv, minimumMv, afterMv, durationMs,
        vcomMv, waveformMode,
        static_cast<uint16_t>(panel_traits::E1003_WAVEFORM_TEMP_C),
        static_cast<uint16_t>(panel_traits::E1003_VCOM_SET_SELECTOR)};
    LOG.printf(
        "[panel] E1003 %s battery before=%lu.%03luV "
        "endpoint-min=%lu.%03luV after=%lu.%03luV sag=%lumV\n",
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
  void sample() {
    uint32_t cellMv = 0;
    if (!sampleBatteryCellMv(cellMv, kSamplesPerReading)) return;

    if (!valid) beforeMv = cellMv;
    valid = true;
    afterMv = cellMv;
    if (cellMv < minimumMv) minimumMv = cellMv;
  }
};

template <typename Panel>
inline void runWaveform(Panel& panel, uint16_t width, uint16_t height,
                        uint16_t mode, const char* name, uint16_t vcomMv) {
  VoltageTrace voltage;
  voltage.begin();
  const uint32_t startedAt = millis();
  panel.tconDisplayArea(0, 0, width, height, mode);
  panel.tconWaitForDisplayReady();
  const uint32_t durationMs = millis() - startedAt;
  voltage.finish(vcomMv, mode, name, durationMs);
  LOG.printf(
      "[panel] E1003 %s FastEPD completion wait finished after %lu ms\n",
      name,
      static_cast<unsigned long>(durationMs));
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
  panel.setTconTemp(
      static_cast<uint16_t>(panel_traits::E1003_WAVEFORM_TEMP_C));
  const uint16_t waveformTemperatureC = panel.getTconTemp();
  const uint16_t vcomMv = panel.getTconVcom();
  LOG.printf(
      "[panel] E1003 FastEPD profile VCOM=-%u.%03uV selector=0x%04X "
      "temperature=%uC\n",
      static_cast<unsigned>(vcomMv / 1000),
      static_cast<unsigned>(vcomMv % 1000),
      static_cast<unsigned>(panel_traits::E1003_VCOM_SET_SELECTOR),
      static_cast<unsigned>(waveformTemperatureC));
  if (vcomMv != panel_traits::E1003_VCOM_MV ||
      waveformTemperatureC != panel_traits::E1003_WAVEFORM_TEMP_C) {
    LOG.println(
        "[panel] ERROR: E1003 FastEPD controller profile readback mismatch");
  }

  constexpr uint16_t kOneBppModeRegister = 0x113A;
  panel.tconWaitForDisplayReady();
  panel.tconWriteReg(
      kOneBppModeRegister,
      panel.tconReadReg(kOneBppModeRegister) & ~(static_cast<uint16_t>(1) << 2));
  LOG.println(
      "[panel] E1003 uploading image for FastEPD single GC16 refresh");
  panel.setTconWindowsData(0, 0, width - 1, height - 1);
  panel.tconLoadImage(framebuffer, 0, 0, width, height, false);
  runWaveform(panel, width, height, 0x02, "GC16", vcomMv);
  panel.tconStandby();
  delay(10);
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
