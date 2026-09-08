#pragma once

#include <stdint.h>

namespace e1003_panel_power {

inline constexpr uint16_t kPowerSequenceCommand = 0x0038;
inline constexpr uint16_t kPowerOff = 0x0000;
inline constexpr uint16_t kPowerOn = 0x0001;
inline constexpr uint32_t kBiasPrechargeMs = 500;
inline constexpr uint32_t kBiasDischargeMs = 500;

// POWER_SEQ is the IT8951-managed interface to the panel-bias PMIC. Keeping
// external GPIO11 asserted while this command runs preserves the PMIC's rail
// ordering; GPIO11 is cut only later during whole-board sleep.
template <typename Panel>
inline void setBiasPower(Panel& panel, bool enabled) {
  panel.tconWaitForReady();
  panel.tconWriteCmdCode(kPowerSequenceCommand);
  panel.tconWirteData(enabled ? kPowerOn : kPowerOff);
  panel.tconWaitForReady();
}

}  // namespace e1003_panel_power
