#include "epaper_setup.h"

#include <Arduino.h>

#include "board_pins.h"

namespace epaper_setup {

void prepare() {
  pinMode(board::PIN_SD_ENABLE, OUTPUT);
  digitalWrite(board::PIN_SD_ENABLE, HIGH);
  delay(10);
}

void finalize(SPIClass& panelSpi) {
  prepare();
  panelSpi.end();
  panelSpi.begin(board::PIN_SD_SCK, board::PIN_SD_MISO, board::PIN_SD_MOSI, -1);
}

}  // namespace epaper_setup
