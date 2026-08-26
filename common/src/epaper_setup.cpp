#include "epaper_setup.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "app_logger.h"
#include "board_pins.h"
#include "peripheral_power.h"

namespace epaper_setup {

#if RETERMINAL_MODEL == 1003
namespace {

constexpr uint32_t kPanelDischargeMs = 500;
constexpr uint32_t kPanelPowerSettleMs = 500;

const int kControllerInputPins[] = {
    board::PIN_PANEL_CS,
    board::PIN_PANEL_RESET,
    board::PIN_SD_SCK,
    board::PIN_SD_MOSI,
};

void releasePanelSignalHolds() {
  for (const int pin : kControllerInputPins) {
    gpio_hold_dis(static_cast<gpio_num_t>(pin));
  }
  gpio_hold_dis(static_cast<gpio_num_t>(board::PIN_SD_MISO));
}

void quiescePanelSignals() {
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  for (const int pin : kControllerInputPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  pinMode(board::PIN_SD_MISO, INPUT_PULLDOWN);
  pinMode(board::PIN_PANEL_READY, INPUT_PULLDOWN);
}

void driveSharedMisoLow() {
  // The powered SD side can pull shared MISO high and back-power an unpowered
  // IT8951. Drive it low only while the controller is reset and its rail is off.
  pinMode(board::PIN_SD_MISO, OUTPUT);
  digitalWrite(board::PIN_SD_MISO, LOW);
}

void holdPanelSignalsLow() {
  for (const int pin : kControllerInputPins) {
    gpio_hold_en(static_cast<gpio_num_t>(pin));
  }
  gpio_hold_en(static_cast<gpio_num_t>(board::PIN_SD_MISO));
}

}  // namespace
#endif

void prepare() {
  peripheral_power::enable();
  delay(10);
}

void resetPanelPower() {
#if RETERMINAL_MODEL == 1003
  const gpio_num_t biasEnable =
      static_cast<gpio_num_t>(board::PIN_PANEL_BIAS_ENABLE);
  const gpio_num_t controllerEnable =
      static_cast<gpio_num_t>(board::PIN_PANEL_CONTROLLER_ENABLE);
  gpio_hold_dis(biasEnable);
  gpio_hold_dis(controllerEnable);
  releasePanelSignalHolds();

  quiescePanelSignals();
  pinMode(board::PIN_PANEL_BIAS_ENABLE, OUTPUT);
  pinMode(board::PIN_PANEL_CONTROLLER_ENABLE, OUTPUT);
  digitalWrite(board::PIN_PANEL_BIAS_ENABLE, LOW);
  digitalWrite(board::PIN_PANEL_CONTROLLER_ENABLE, LOW);
  driveSharedMisoLow();
  delay(kPanelDischargeMs);

  pinMode(board::PIN_SD_MISO, INPUT_PULLDOWN);
  digitalWrite(board::PIN_PANEL_CONTROLLER_ENABLE, HIGH);
  digitalWrite(board::PIN_PANEL_BIAS_ENABLE, HIGH);
  delay(kPanelPowerSettleMs);
  digitalWrite(board::PIN_PANEL_CS, HIGH);
  LOG.println(
      "[panel] E1003 controller discharged; display rails settled");
#endif
}

void shutdownPanelPower() {
#if RETERMINAL_MODEL == 1003
  const gpio_num_t biasEnable =
      static_cast<gpio_num_t>(board::PIN_PANEL_BIAS_ENABLE);
  const gpio_num_t controllerEnable =
      static_cast<gpio_num_t>(board::PIN_PANEL_CONTROLLER_ENABLE);
  gpio_hold_dis(biasEnable);
  gpio_hold_dis(controllerEnable);
  releasePanelSignalHolds();

  quiescePanelSignals();
  pinMode(board::PIN_PANEL_BIAS_ENABLE, OUTPUT);
  pinMode(board::PIN_PANEL_CONTROLLER_ENABLE, OUTPUT);
  digitalWrite(board::PIN_PANEL_BIAS_ENABLE, LOW);
  delay(10);
  digitalWrite(board::PIN_PANEL_CONTROLLER_ENABLE, LOW);
  delay(1);
  driveSharedMisoLow();
  gpio_hold_en(biasEnable);
  gpio_hold_en(controllerEnable);
  holdPanelSignalsLow();
  gpio_deep_sleep_hold_en();
  LOG.println("[panel] E1003 controller signals and display rails held low");
#endif
}

void finalize(SPIClass& panelSpi) {
  prepare();
  panelSpi.end();
  panelSpi.begin(board::PIN_SD_SCK, board::PIN_SD_MISO, board::PIN_SD_MOSI, -1);
}

}  // namespace epaper_setup
