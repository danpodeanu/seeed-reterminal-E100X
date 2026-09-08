#pragma once

// Guard E1003 panel refreshes against incomplete frames. The controller uses
// the sensed panel temperature, one 4bpp upload, and one GC16. POWER_SEQ brings
// the bias rails up before the waveform and leaves them time to settle, while
// the ESP32 runs at lower frequency to preserve battery-side current margin.
// The rails are explicitly sequenced off after LUT-idle completion.
// Earlier INIT and white-GC16 preclean experiments both produced inverted or
// corrupted battery-powered frames.
// Monochrome status screens use the same explicit bias sequence around their
// 1-bpp GC16 update so VCOM setup never has to leave the panel rails enabled.
//
// The guard is a no-op on other panels: E1001/E1002/E1004/E1005 haven't
// exhibited the freeze in field use, and the fast paths on those
// panels have tighter timings that would risk false-positive resets.

#if RETERMINAL_MODEL == 1003
#include <Arduino.h>
#include <esp32-hal-cpu.h>
#include <esp_task_wdt.h>
#include "app_logger.h"
#include "board_pins.h"
#include "charger.h"
#include "e1003_panel_power.h"
#include "panel_traits.h"

namespace panel_watchdog {

constexpr uint32_t kRetainedTraceMagic = 0xE1003B52;
constexpr uint32_t kRefreshCpuMhz = 80;
constexpr uint32_t kStagedRefreshThresholdMv = 3800;

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
  uint16_t refreshCpuMhz;
  uint16_t biasPrechargeMs;
  uint8_t powerSource;
  uint8_t chargerSystemStatus;
  uint8_t chargerInputStatus;
  uint8_t reserved;
};

static RTC_DATA_ATTR RetainedVoltageTrace retainedVoltageTrace = {};

inline uint8_t encodePowerSource(const charger::Status& power) {
  if (!power.valid) return 0;
  return power.state == charger::State::Connected ? 2 : 1;
}

inline const char* powerSourceName(uint8_t source) {
  return source == 2 ? "external" : (source == 1 ? "battery" : "unknown");
}

inline const char* refreshProfileName(uint16_t biasPrechargeMs) {
  return biasPrechargeMs == 0 ? "direct-GC16" : "staged-GC16";
}

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
      "power=%s charger=0x%02X/0x%02X cpu=%uMHz precharge=%ums "
      "profile=%s\n",
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
      static_cast<unsigned>(retainedVoltageTrace.waveformTemperatureC),
      powerSourceName(retainedVoltageTrace.powerSource),
      static_cast<unsigned>(retainedVoltageTrace.chargerSystemStatus),
      static_cast<unsigned>(retainedVoltageTrace.chargerInputStatus),
      static_cast<unsigned>(retainedVoltageTrace.refreshCpuMhz),
      static_cast<unsigned>(retainedVoltageTrace.biasPrechargeMs),
      refreshProfileName(retainedVoltageTrace.biasPrechargeMs));
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

  void checkpoint() { sample(); }

  void finish(uint16_t vcomMv, uint16_t waveformMode,
              const char* waveformName, uint32_t durationMs,
              uint16_t waveformTemperatureC, const charger::Status& power,
              uint16_t refreshCpuMhz, uint16_t biasPrechargeMs) {
    sample();
    if (!valid) {
      LOG.printf("[panel] E1003 %s battery trace unavailable\n",
                 waveformName);
      return;
    }
    retainedVoltageTrace = {
        kRetainedTraceMagic, beforeMv, minimumMv, afterMv, durationMs,
        vcomMv, waveformMode, waveformTemperatureC,
        static_cast<uint16_t>(panel_traits::E1003_VCOM_SET_SELECTOR),
        refreshCpuMhz, biasPrechargeMs,
        encodePowerSource(power), power.systemStatus, power.inputStatus, 0};
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
inline void triggerWaveform(Panel& panel, uint16_t width, uint16_t height,
                            uint16_t mode, bool oneBpp) {
  if (oneBpp) {
    panel.tconDisplayArea1bpp(0, 0, width, height, mode, 0x00, 0xFF);
  } else {
    panel.tconDisplayArea(0, 0, width, height, mode);
    panel.tconWaitForDisplayReady();
  }
}

template <typename Panel>
inline void runDirectWaveform(Panel& panel, uint16_t width, uint16_t height,
                              uint16_t mode, const char* name,
                              uint16_t vcomMv,
                              uint16_t waveformTemperatureC,
                              const charger::Status& power, bool oneBpp) {
  VoltageTrace voltage;
  voltage.begin();
  e1003_panel_power::setBiasPower(panel, true);
  const uint32_t startedAt = millis();
  triggerWaveform(panel, width, height, mode, oneBpp);
  const uint32_t durationMs = millis() - startedAt;
  e1003_panel_power::setBiasPower(panel, false);
  delay(e1003_panel_power::kBiasDischargeMs);
  voltage.finish(vcomMv, mode, name, durationMs, waveformTemperatureC, power,
                 static_cast<uint16_t>(getCpuFrequencyMhz()), 0);
  LOG.printf(
      "[panel] E1003 %s direct completion wait finished after %lu ms\n",
      name, static_cast<unsigned long>(durationMs));
}

template <typename Panel>
inline void runStagedWaveform(Panel& panel, uint16_t width, uint16_t height,
                             uint16_t mode, const char* name,
                             uint16_t vcomMv,
                             uint16_t waveformTemperatureC,
                             const charger::Status& power, bool oneBpp) {
  VoltageTrace voltage;
  voltage.begin();
  const uint32_t stagedAt = millis();
  const uint32_t originalCpuMhz = getCpuFrequencyMhz();

  LOG.printf(
      "[panel] E1003 staging %s: bias precharge=%lums, cpu target=%luMHz\n",
      name,
      static_cast<unsigned long>(e1003_panel_power::kBiasPrechargeMs),
      static_cast<unsigned long>(kRefreshCpuMhz));
  LOG.flush();
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);

  if (originalCpuMhz > kRefreshCpuMhz) {
    setCpuFrequencyMhz(kRefreshCpuMhz);
  }
  const uint16_t refreshCpuMhz =
      static_cast<uint16_t>(getCpuFrequencyMhz());

  e1003_panel_power::setBiasPower(panel, true);
  delay(e1003_panel_power::kBiasPrechargeMs);
  voltage.checkpoint();

  const uint32_t waveformStartedAt = millis();
  triggerWaveform(panel, width, height, mode, oneBpp);
  const uint32_t waveformDurationMs = millis() - waveformStartedAt;
  voltage.checkpoint();

  e1003_panel_power::setBiasPower(panel, false);
  delay(e1003_panel_power::kBiasDischargeMs);
  const uint32_t stagedDurationMs = millis() - stagedAt;

  bool cpuRestored = true;
  if (refreshCpuMhz != originalCpuMhz) {
    cpuRestored = setCpuFrequencyMhz(originalCpuMhz);
  }
  voltage.finish(vcomMv, mode, name, stagedDurationMs,
                 waveformTemperatureC, power, refreshCpuMhz,
                 static_cast<uint16_t>(e1003_panel_power::kBiasPrechargeMs));
  LOG.printf(
      "[panel] E1003 %s staged refresh finished: waveform=%lums "
      "total=%lums cpu=%uMHz\n",
      name,
      static_cast<unsigned long>(waveformDurationMs),
      static_cast<unsigned long>(stagedDurationMs),
      static_cast<unsigned>(refreshCpuMhz));
  if (originalCpuMhz > kRefreshCpuMhz &&
      refreshCpuMhz != kRefreshCpuMhz) {
    LOG.printf(
        "[panel] WARNING: E1003 CPU remained at %uMHz during refresh\n",
        static_cast<unsigned>(refreshCpuMhz));
  }
  if (!cpuRestored) {
    LOG.printf("[panel] ERROR: could not restore CPU frequency to %luMHz\n",
               static_cast<unsigned long>(originalCpuMhz));
  }
}

template <typename Panel>
inline void refreshPanel(Panel& panel) {
  reportRetainedTraceOnce();

  const uint16_t width = static_cast<uint16_t>(panel.width());
  const uint16_t height = static_cast<uint16_t>(panel.height());
  const bool oneBpp = panel.getColorDepth() != 4;
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

  LOG.println("[panel] E1003 waking controller");
  panel.wake();
  LOG.println("[panel] E1003 controller awake; reading profile");
  const uint16_t waveformTemperatureC = panel.getTconTemp();
  const uint16_t vcomMv = panel.getTconVcom();
  LOG.printf(
      "[panel] E1003 profile VCOM=-%u.%03uV selector=0x%04X "
      "temperature=%uC (sensor)\n",
      static_cast<unsigned>(vcomMv / 1000),
      static_cast<unsigned>(vcomMv % 1000),
      static_cast<unsigned>(panel_traits::E1003_VCOM_SET_SELECTOR),
      static_cast<unsigned>(waveformTemperatureC));
  if (vcomMv != panel_traits::E1003_VCOM_MV) {
    LOG.println(
        "[panel] ERROR: E1003 VCOM controller profile readback mismatch");
  }
  if (waveformTemperatureC > 50) {
    LOG.println(
        "[panel] WARNING: E1003 waveform temperature is outside panel range");
  }

  panel.tconWaitForDisplayReady();
  panel.setTconWindowsData(0, 0, width - 1, height - 1);
  if (oneBpp) {
    LOG.println(
        "[panel] E1003 uploading 1bpp image for sequenced GC16 refresh");
    panel.tconLoad1bppImage(framebuffer, 0, 0, width, height, false);
  } else {
    constexpr uint16_t kOneBppModeRegister = 0x113A;
    panel.tconWriteReg(
        kOneBppModeRegister,
        panel.tconReadReg(kOneBppModeRegister) &
            ~(static_cast<uint16_t>(1) << 2));
    LOG.println(
        "[panel] E1003 uploading 4bpp image for sequenced GC16 refresh");
    panel.tconLoadImage(framebuffer, 0, 0, width, height, false);
  }
  const bool useStagedRefresh =
      batteryValid && batteryMv < kStagedRefreshThresholdMv &&
      (!power.valid || power.state == charger::State::Disconnected);
  LOG.printf("[panel] E1003 GC16 power strategy=%s (threshold=%lu.%03luV)\n",
             useStagedRefresh ? "staged" : "direct",
             static_cast<unsigned long>(kStagedRefreshThresholdMv / 1000),
             static_cast<unsigned long>(kStagedRefreshThresholdMv % 1000));
  if (useStagedRefresh) {
    runStagedWaveform(panel, width, height, 0x02, "GC16", vcomMv,
                      waveformTemperatureC, power, oneBpp);
  } else {
    runDirectWaveform(panel, width, height, 0x02, "GC16", vcomMv,
                      waveformTemperatureC, power, oneBpp);
  }
  // The stock 1-bpp path enters SLEEP directly. STANDBY followed by SLEEP
  // leaves this controller unable to service the later same-boot Gray16 wake.
  if (!oneBpp) {
    panel.tconStandby();
    delay(10);
  }
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
