#include "epaper_setup.h"

#include <Arduino.h>
#include <driver/gpio.h>

#include "app_logger.h"
#include "board_pins.h"
#include "peripheral_power.h"

namespace epaper_setup {

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

  pinMode(board::PIN_PANEL_BIAS_ENABLE, OUTPUT);
  pinMode(board::PIN_PANEL_CONTROLLER_ENABLE, OUTPUT);
  digitalWrite(board::PIN_PANEL_BIAS_ENABLE, LOW);
  digitalWrite(board::PIN_PANEL_CONTROLLER_ENABLE, LOW);
  delay(100);

  digitalWrite(board::PIN_PANEL_CONTROLLER_ENABLE, HIGH);
  delay(50);
  digitalWrite(board::PIN_PANEL_BIAS_ENABLE, HIGH);
  delay(20);
  LOG.println("[panel] E1003 display rails power-cycled");
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

  pinMode(board::PIN_PANEL_BIAS_ENABLE, OUTPUT);
  pinMode(board::PIN_PANEL_CONTROLLER_ENABLE, OUTPUT);
  digitalWrite(board::PIN_PANEL_BIAS_ENABLE, LOW);
  delay(10);
  digitalWrite(board::PIN_PANEL_CONTROLLER_ENABLE, LOW);
  gpio_hold_en(biasEnable);
  gpio_hold_en(controllerEnable);
  gpio_deep_sleep_hold_en();
  LOG.println("[panel] E1003 display rails powered down");
#endif
}

void finalize(SPIClass& panelSpi) {
  prepare();
  panelSpi.end();
  panelSpi.begin(board::PIN_SD_SCK, board::PIN_SD_MISO, board::PIN_SD_MOSI, -1);
}

}  // namespace epaper_setup
